package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/phabioo/uzume/server/internal/backup"
	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/store"
)

// Backup and restore on the command line (ADR 0029 D2):
//
//	uzume-server backup [--out DIR] [--roms|--no-roms]   make a backup now (the server may keep running)
//	uzume-server backup list                             list the backups of the backup folder
//	uzume-server backup config [flags]                   show or change schedule, folder, keep and ROMs
//	uzume-server backup verify <file>                    check every checksum of a backup file
//	uzume-server restore <file>                          restore a backup (the server must be stopped)

// backupExtras are the files outside the data directory that every backup carries (settings of the server).
func backupExtras(cfg *config.Config) []backup.Extra {
	var ex []backup.Extra
	if cfg.TLSCert != "" {
		ex = append(ex, backup.Extra{Name: "tls-cert.pem", Path: cfg.TLSCert})
	}
	if cfg.TLSKey != "" {
		ex = append(ex, backup.Extra{Name: "tls-key.pem", Path: cfg.TLSKey})
	}
	if runtime.GOOS != "windows" {
		ex = append(ex, backup.Extra{Name: "server.env", Path: "/etc/uzume/server.env"})
	}
	return ex
}

// splitArg takes a leading positional argument off args (flags may follow it).
func splitArg(args []string) (string, []string) {
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		return args[0], args[1:]
	}
	return "", args
}

func runBackup(args []string, out io.Writer) error {
	if len(args) > 0 {
		switch args[0] {
		case "list":
			return runBackupList(args[1:], out)
		case "config":
			return runBackupConfig(args[1:], out)
		case "verify":
			return runBackupVerify(args[1:], out)
		}
	}
	fs := flag.NewFlagSet("backup", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	dir := fs.String("out", "", "target folder (default: the backup folder of the server settings)")
	roms := fs.Bool("roms", false, "include the ROM library (default: the server setting)")
	noRoms := fs.Bool("no-roms", false, "leave the ROM library out")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() > 0 {
		return errors.New("usage: uzume-server backup [--out DIR] [--roms|--no-roms] | list | config | verify <file>")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	var romsOverride *bool
	switch {
	case *roms && *noRoms:
		return errors.New("--roms and --no-roms exclude each other")
	case *roms:
		t := true
		romsOverride = &t
	case *noRoms:
		f := false
		romsOverride = &f
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	svc, closeFn, err := openService(ctx, cfg, false)
	if err != nil {
		return err
	}
	defer closeFn()
	res, err := svc.BackupNow(ctx, *dir, romsOverride, "cli", printProgress(out))
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "\nBackup written: %s (%s, took %s)\n", res.Path, backup.FormatBytes(uint64(res.Size)), res.Took.Round(time.Second))
	if res.Manifest.Skipped > 0 {
		fmt.Fprintf(out, "%d file(s) vanished while the backup ran and are not in it; the next backup has the rest.\n", res.Manifest.Skipped)
	}
	fmt.Fprintln(out, "The file contains secrets (TLS key, password hashes). Keep it as private as the data folder.")
	return nil
}

// printProgress prints each new phase once.
func printProgress(out io.Writer) func(backup.Progress) {
	last := ""
	return func(p backup.Progress) {
		if p.Phase != last {
			last = p.Phase
			fmt.Fprintf(out, "%s...\n", p.Phase)
		}
	}
}

func runBackupList(args []string, out io.Writer) error {
	fs := flag.NewFlagSet("backup list", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	dir := fs.String("dir", "", "folder to list (default: the backup folder of the server settings)")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	var infos []backup.Info
	folder := *dir
	if folder == "" {
		ctx := context.Background()
		svc, closeFn, err := openService(ctx, cfg, false)
		if err != nil {
			return err
		}
		defer closeFn()
		folder = svc.BackupSettings(ctx).Folder
		if folder == "" {
			return errors.New("no backup folder is set: pass --dir DIR or run 'uzume-server backup config --folder DIR'")
		}
	}
	infos, err := backup.List(folder)
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "%d backup(s) in %s\n", len(infos), folder)
	for _, i := range infos {
		tag := ""
		if i.Safety {
			tag = "  [safety backup]"
		}
		if i.Err != "" {
			fmt.Fprintf(out, "%s  %10s  DAMAGED: %s%s\n", i.Name, backup.FormatBytes(uint64(i.Size)), i.Err, tag)
			continue
		}
		fmt.Fprintf(out, "%s  %10s  %s  %s%s\n", i.Name, backup.FormatBytes(uint64(i.Size)), i.Time.Local().Format("2006-01-02 15:04"), i.Manifest.ContentsText(), tag)
	}
	return nil
}

var weekdayNames = []string{"sun", "mon", "tue", "wed", "thu", "fri", "sat"}

func parseWeekday(v string) (int, error) {
	v = strings.ToLower(strings.TrimSpace(v))
	for i, n := range weekdayNames {
		if strings.HasPrefix(v, n) {
			return i, nil
		}
	}
	if n, err := strconv.Atoi(v); err == nil && n >= 0 && n <= 6 {
		return n, nil
	}
	return 0, fmt.Errorf("weekday %q: use sun..sat or 0 (Sunday) to 6", v)
}

func runBackupConfig(args []string, out io.Writer) error {
	fs := flag.NewFlagSet("backup config", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	folder := fs.String("folder", "", "backup folder (empty value turns backups off by unsetting it)")
	schedule := fs.String("schedule", "", "off, daily or weekly")
	weekday := fs.String("weekday", "", "weekday of the weekly backup: sun..sat")
	at := fs.String("time", "", "time of day HH:MM in the server's local time zone")
	keep := fs.Int("keep", 0, "regular backups to keep (1-365)")
	romsFlag := fs.String("roms", "", "include the ROM library: on or off")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	set := map[string]bool{}
	fs.Visit(func(f *flag.Flag) { set[f.Name] = true })
	var u hub.BackupSettingsUpdate
	if set["folder"] {
		u.Folder = folder
	}
	if set["schedule"] {
		u.Mode = schedule
	}
	if set["weekday"] {
		n, err := parseWeekday(*weekday)
		if err != nil {
			return err
		}
		u.Weekday = &n
	}
	if set["time"] {
		u.Time = at
	}
	if set["keep"] {
		u.Keep = keep
	}
	if set["roms"] {
		switch strings.ToLower(*romsFlag) {
		case "on", "true", "1", "yes":
			t := true
			u.IncludeROMs = &t
		case "off", "false", "0", "no":
			f := false
			u.IncludeROMs = &f
		default:
			return errors.New("--roms: use on or off")
		}
	}
	ctx := context.Background()
	svc, closeFn, err := openService(ctx, cfg, false)
	if err != nil {
		return err
	}
	defer closeFn()
	if u != (hub.BackupSettingsUpdate{}) {
		if err := svc.SetBackupSettings(ctx, u); err != nil {
			return err
		}
		fmt.Fprintln(out, "Saved. A running server applies it within 5 minutes.")
	}
	s := svc.BackupSettings(ctx)
	f := s.Folder
	if f == "" {
		f = "(not set: backups are off)"
	}
	sched := s.Mode
	if s.Mode == backup.ModeWeekly {
		sched += " on " + weekdayNames[s.Weekday]
	}
	if s.Mode != backup.ModeOff {
		sched += " at " + s.Time
	}
	fmt.Fprintf(out, "folder:   %s\nschedule: %s\nkeep:     %d\nroms:     %v\n", f, sched, s.Keep, s.IncludeROMs)
	return nil
}

func runBackupVerify(args []string, out io.Writer) error {
	file, args := splitArg(args)
	fs := flag.NewFlagSet("backup verify", flag.ContinueOnError)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if file == "" && fs.NArg() == 1 {
		file = fs.Arg(0)
	}
	if file == "" {
		return errors.New("usage: uzume-server backup verify <file>")
	}
	m, _, err := backup.Extract(context.Background(), file, "", backup.ExtractOptions{VerifyOnly: true})
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "OK: %d file(s), %s, made by server %s (schema %d) on %s\n", len(m.Members), backup.FormatBytes(uint64(m.TotalSize)),
		m.HubVersion, m.SchemaVersion, m.Created.Local().Format("2006-01-02 15:04"))
	return nil
}

// runRestore restores a backup while the server is stopped. A safety backup comes first unless --no-safety.
func runRestore(args []string, out io.Writer) error {
	file, args := splitArg(args)
	fs := flag.NewFlagSet("restore", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	safetyDir := fs.String("safety-dir", "", "folder for the safety backup (default: the backup folder of the server settings)")
	noSafety := fs.Bool("no-safety", false, "skip the safety backup (for a damaged database or a new machine)")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if file == "" && fs.NArg() == 1 {
		file = fs.Arg(0)
	} else if fs.NArg() > 0 {
		file = ""
	}
	if file == "" {
		return errors.New("usage: uzume-server restore <file> [--safety-dir DIR | --no-safety]")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	if err := os.MkdirAll(cfg.DataDir, 0o750); err != nil {
		return err
	}
	release, err := backup.LockDataDir(cfg.DataDir)
	if errors.Is(err, backup.ErrLocked) {
		return fmt.Errorf("the server is running: stop it first (%s), then restore", stopHint())
	}
	if err != nil {
		return err
	}
	defer release()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	m, err := backup.ReadManifest(file)
	if err != nil {
		return err
	}
	if known := store.KnownSchemaVersion(); m.SchemaVersion > known {
		return &backup.NewerError{Schema: m.SchemaVersion, Known: known}
	}
	fmt.Fprintf(out, "Backup of %s made %s by server %s.\n", m.HubName, m.Created.Local().Format("2006-01-02 15:04"), m.HubVersion)

	safetyName := ""
	if !*noSafety {
		dir := *safetyDir
		svc, closeFn, err := openService(ctx, cfg, false)
		if err != nil {
			return fmt.Errorf("safety backup: cannot open the current data (%w); use --no-safety if you accept that", err)
		}
		if dir == "" {
			dir = svc.BackupSettings(ctx).Folder
		}
		if dir == "" {
			closeFn()
			return errors.New("no folder for the safety backup: pass --safety-dir DIR, or --no-safety")
		}
		dir, _ = filepath.Abs(dir)
		fmt.Fprintln(out, "Making a safety backup of the current state first...")
		res, err := svc.SafetyBackup(ctx, dir, printProgress(out))
		closeFn()
		if err != nil {
			return fmt.Errorf("safety backup failed, nothing was changed: %w", err)
		}
		safetyName = res.Name
		fmt.Fprintf(out, "Safety backup: %s\n", res.Path)
	}

	staging := filepath.Join(cfg.DataDir, backup.StagingDir)
	fmt.Fprintln(out, "Verifying and unpacking the backup...")
	_, items, err := backup.Extract(ctx, file, staging, backup.ExtractOptions{MaxSchema: store.KnownSchemaVersion(), Progress: func(p backup.Progress) {}})
	if err != nil {
		return fmt.Errorf("%w; nothing was changed", err)
	}
	if err := backup.MatchOwner(staging, cfg.DataDir); err != nil {
		os.RemoveAll(staging)
		return err
	}
	if err := backup.WritePending(cfg.DataDir, backup.Pending{Staging: staging, Items: items, Backup: filepath.Base(file), BackupCreated: m.Created,
		IncludesROMs: m.IncludesROMs, Safety: safetyName, Requested: time.Now().UTC(), By: "cli"}); err != nil {
		os.RemoveAll(staging)
		return err
	}
	res, err := backup.ApplyPending(cfg.DataDir, time.Now().UTC())
	if err != nil {
		return fmt.Errorf("the restore failed and was rolled back, nothing was changed: %w", err)
	}
	fmt.Fprintf(out, "Restored the backup from %s. Start the server again.\n", res.BackupCreated.Local().Format("2006-01-02 15:04"))
	if safetyName != "" {
		fmt.Fprintf(out, "To undo, restore the safety backup %s.\n", safetyName)
	}
	if _, err := os.Stat(filepath.Join(cfg.DataDir, "restored-config")); err == nil {
		fmt.Fprintf(out, "Files from outside the data folder (own TLS certificate and key, server.env) are in %s; copy them where they belong.\n", filepath.Join(cfg.DataDir, "restored-config"))
	}
	return nil
}
