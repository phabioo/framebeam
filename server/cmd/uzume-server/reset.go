package main

import (
	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"

	"github.com/phabioo/uzume/server/internal/backup"
	"github.com/phabioo/uzume/server/internal/config"
)

// resetKeep are the entries of the data folder that `reset` never deletes: the backups (also the safety backup it just
// made), the logs, the lock file this process holds, and on Windows the update request folder the installer created
// with its own permissions.
var resetKeep = map[string]bool{"backups": true, "logs": true, "hub.lock": true, "update-request": true}

// runReset deletes all server data in the data folder and so starts fresh (ADR 0030 D4):
//
//	uzume-server reset [-confirm NAME] [-no-backup]
//
// The server must be stopped (the data folder lock is taken like for restore). The server name has to be typed
// (or given with -confirm) because nothing but a backup brings the data back. Before deleting, a safety backup
// without ROMs goes to the configured backup folder, else <data>/backups. Nothing outside the data folder is touched
// (own certificate and key, the env file, an import folder or ROM library elsewhere). The next start creates a new
// database, a new self-generated certificate (so the security code changes) and prints a new setup code.
func runReset(args []string, in io.Reader, out io.Writer) error {
	fs := flag.NewFlagSet("reset", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	confirm := fs.String("confirm", "", "the server name, instead of typing it")
	noBackup := fs.Bool("no-backup", false, "skip the safety backup")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() > 0 {
		return errors.New("usage: uzume-server reset [-confirm NAME] [-no-backup]")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	st, err := os.Stat(cfg.DataDir)
	if errors.Is(err, os.ErrNotExist) || (err == nil && !st.IsDir()) {
		fmt.Fprintf(out, "Nothing to reset: %s does not exist. The first start creates fresh data.\n", cfg.DataDir)
		return nil
	}
	if err != nil {
		return err
	}
	if err := probeWritable(cfg.DataDir); err != nil {
		return err
	}
	release, err := backup.LockDataDir(cfg.DataDir)
	if errors.Is(err, backup.ErrLocked) {
		return fmt.Errorf("the server is running: stop it first (%s), then reset", stopHint())
	}
	if err != nil {
		return err
	}
	defer release()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// The name to type and the backup folder come from the database, when it can be read.
	name, folder := cfg.Name, ""
	closeDB := func() {}
	defer func() { closeDB() }()
	var safety func(dir string, p func(backup.Progress)) (backup.Result, error)
	if hasDatabase(cfg.DataDir) {
		if svc, closeFn, oerr := openService(ctx, cfg, false); oerr == nil {
			closeDB = closeFn
			name = svc.Info().Name
			folder = svc.BackupSettings(ctx).Folder
			safety = func(dir string, p func(backup.Progress)) (backup.Result, error) {
				return svc.SafetyBackupWithoutROMs(ctx, dir, p)
			}
		} else if !*noBackup {
			return fmt.Errorf("safety backup: cannot open the current data (%w); use -no-backup if you accept that", oerr)
		}
	}
	if name == "" {
		name = "uzume"
	}

	files, size := countTree(cfg.DataDir, resetKeep)
	fmt.Fprintf(out, "This deletes ALL data of the server %q in %s: %d file(s), %s (database, accounts, saves, ROM library, firmware, plugins, certificate).\n",
		name, cfg.DataDir, files, backup.FormatBytes(uint64(size)))
	fmt.Fprintln(out, "Kept: the backups folder, logs, and everything outside the data folder (own certificate, server.env, folders elsewhere).")
	if *noBackup {
		fmt.Fprintln(out, "No safety backup (-no-backup): afterwards only an earlier backup brings the data back.")
	}
	if *confirm != "" {
		if *confirm != name {
			return fmt.Errorf("-confirm %q does not match the server name %q; nothing was changed", *confirm, name)
		}
	} else {
		fmt.Fprintf(out, "Type the server name (%s) to continue: ", name)
		line, _ := bufio.NewReader(in).ReadString('\n')
		if strings.TrimSpace(line) != name {
			return errors.New("canceled: the name did not match; nothing was changed")
		}
	}

	if !*noBackup {
		if safety == nil {
			return errors.New("safety backup: there is no database to back up; use -no-backup if you want to delete the rest anyway")
		}
		dir := folder
		if dir == "" {
			dir = filepath.Join(cfg.DataDir, "backups")
		}
		if dir, err = filepath.Abs(dir); err != nil {
			return err
		}
		if err := os.MkdirAll(dir, 0o750); err != nil {
			return fmt.Errorf("safety backup: %w", err)
		}
		fmt.Fprintln(out, "Making a safety backup (without ROMs) first...")
		res, err := safety(dir, printProgress(out))
		if err != nil {
			return fmt.Errorf("safety backup failed, nothing was deleted: %w", err)
		}
		fmt.Fprintf(out, "Safety backup: %s\n", res.Path)
		fmt.Fprintln(out, "To bring the data back, stop the server and run: uzume-server restore <that file>")
	}

	closeDB() // the database file must be closed before it is deleted (Windows)
	closeDB = func() {}
	keep := map[string]bool{}
	for k := range resetKeep {
		keep[k] = true
	}
	if folder != "" { // a backup folder inside the data folder stays
		if rel, rerr := filepath.Rel(cfg.DataDir, folder); rerr == nil && filepath.IsLocal(rel) {
			keep[strings.Split(filepath.ToSlash(rel), "/")[0]] = true
		}
	}
	deleted, err := deleteDataTree(cfg.DataDir, keep)
	fmt.Fprintf(out, "Deleted %d item(s) from %s.\n", len(deleted), cfg.DataDir)
	if err != nil {
		return fmt.Errorf("the reset is incomplete: %w (stop everything that uses the data folder and run reset again)", err)
	}
	fmt.Fprintln(out, "The server is empty now. The next start creates a new database and a new certificate (so the security code changes) and prints a new setup code.")
	return nil
}

// countTree counts the files and bytes below dir, skipping the top-level entries in skip.
func countTree(dir string, skip map[string]bool) (files int, size int64) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return 0, 0
	}
	for _, e := range entries {
		if skip[e.Name()] {
			continue
		}
		_ = filepath.WalkDir(filepath.Join(dir, e.Name()), func(_ string, d fs.DirEntry, err error) error {
			if err != nil || d.IsDir() {
				return nil
			}
			if info, ierr := d.Info(); ierr == nil {
				files++
				size += info.Size()
			}
			return nil
		})
	}
	return files, size
}

// deleteDataTree removes every top-level entry of dir except those in keep and returns the names it removed.
func deleteDataTree(dir string, keep map[string]bool) (deleted []string, err error) {
	entries, rerr := os.ReadDir(dir)
	if rerr != nil {
		return nil, rerr
	}
	var errs []error
	for _, e := range entries {
		if keep[e.Name()] {
			continue
		}
		if rerr := os.RemoveAll(filepath.Join(dir, e.Name())); rerr != nil {
			errs = append(errs, rerr)
			continue
		}
		deleted = append(deleted, e.Name())
	}
	return deleted, errors.Join(errs...)
}
