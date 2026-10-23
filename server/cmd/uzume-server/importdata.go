package main

import (
	"database/sql"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/backup"
	"github.com/phabioo/uzume/server/internal/config"
)

// movedFile is written into the source folder of import-data: the data now lives somewhere else.
const movedFile = "moved.json"

// importSkip are the entries of a data folder that import-data does not copy: the lock file of the server.
var importSkip = map[string]bool{"hub.lock": true}

// refuseExit is the exit status of import-data when it refuses to start (nothing was changed).
const refuseExit = 3

func refuse(format string, a ...any) error {
	return &exitError{code: refuseExit, err: fmt.Errorf(format, a...)}
}

// runImportData implements
//
//	uzume-server import-data --from DIR [-data-dir DEST]
//
// (ADR 0030 D6, "Share with others"): it copies the data folder of a stopped server (the "On this PC" server of the
// app) into DEST, by default the data folder of the service, and fixes the absolute paths stored in the database
// that point into DIR. Nothing is ever deleted or overwritten in DEST; a failed run removes what it created. DIR
// stays as it is, plus a moved.json. Prints DEST on success; exit status 3 means it refused before changing anything.
func runImportData(args []string, out io.Writer) error {
	fl := flag.NewFlagSet("import-data", flag.ContinueOnError)
	cfg := config.Register(fl, os.Getenv)
	from := fl.String("from", "", "data folder of the stopped server to copy (required)")
	if err := fl.Parse(args); err != nil {
		return err
	}
	if *from == "" || fl.NArg() > 0 {
		return &usageError{msg: "usage: uzume-server import-data --from DIR [-data-dir DEST]"}
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	res, err := importData(*from, cfg.DataDir)
	if err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "Copied %d file(s), %s; fixed %d stored path(s).\n", res.Files, backup.FormatBytes(uint64(res.Bytes)), res.PathsFixed)
	fmt.Fprintln(out, res.Dest)
	return nil
}

// importResult is what importData did.
type importResult struct {
	Dest       string
	Files      int
	Bytes      int64
	PathsFixed int
}

// importData does the work of import-data; see runImportData.
func importData(from, dest string) (res importResult, err error) {
	from, err = filepath.Abs(from)
	if err != nil {
		return res, err
	}
	dest, err = filepath.Abs(dest)
	if err != nil {
		return res, err
	}
	res.Dest = dest
	if samePath(from, dest) || isInside(from, dest) || isInside(dest, from) {
		return res, refuse("the source %s and the destination %s are the same folder or one is inside the other", from, dest)
	}
	if st, serr := os.Stat(from); serr != nil || !st.IsDir() {
		return res, refuse("%s is not a folder; nothing was imported", from)
	}
	if !hasDatabase(from) {
		return res, refuse("there is no server database in %s; nothing was imported", from)
	}
	if hasDatabase(dest) {
		return res, refuse("%s already contains a server database; it is never overwritten, nothing was imported", dest)
	}
	// The source must be stopped: the server holds the lock of its data folder for its whole run.
	release, lerr := backup.LockDataDir(from)
	if errors.Is(lerr, backup.ErrLocked) {
		return res, refuse("the server using %s is still running; stop it first, nothing was imported", from)
	}
	if lerr != nil {
		return res, fmt.Errorf("source folder %s: %w", from, lerr)
	}
	defer release()

	c := &copier{}
	defer func() {
		if err != nil {
			c.rollback()
			for _, sfx := range []string{"-wal", "-shm", "-journal"} { // leftovers of checking the copy
				_ = os.Remove(dbPath(dest) + sfx)
			}
		}
	}()
	if err = c.mkdirAll(dest); err != nil {
		return res, fmt.Errorf("create %s: %w", dest, err)
	}
	if err = c.copyTree(from, dest, true); err != nil {
		return res, err
	}
	res.Files, res.Bytes = c.files, c.bytes
	if res.PathsFixed, err = fixCopiedDatabase(dbPath(dest), from, dest); err != nil {
		return res, fmt.Errorf("copied database: %w", err)
	}
	if err = writeMoved(from, dest); err != nil {
		return res, fmt.Errorf("write %s into %s: %w", movedFile, from, err)
	}
	return res, nil
}

// copier copies a tree without ever overwriting and remembers what it created, so it can undo that.
type copier struct {
	created []string // files and folders created, in creation order
	files   int
	bytes   int64
}

func (c *copier) rollback() {
	for i := len(c.created) - 1; i >= 0; i-- {
		_ = os.Remove(c.created[i]) // files, then the (by then empty) folders; a non-empty folder stays
	}
	c.created = nil
}

// mkdirAll is os.MkdirAll that records the folders it creates.
func (c *copier) mkdirAll(dir string) error {
	if st, err := os.Stat(dir); err == nil {
		if !st.IsDir() {
			return fmt.Errorf("%s is not a folder", dir)
		}
		return nil
	}
	if parent := filepath.Dir(dir); parent != dir {
		if err := c.mkdirAll(parent); err != nil {
			return err
		}
	}
	if err := os.Mkdir(dir, 0o750); err != nil {
		return err
	}
	c.created = append(c.created, dir)
	return nil
}

// copyTree copies the entries of from into to; the skipped names (the lock file) are left out at the top level only.
func (c *copier) copyTree(from, to string, top bool) error {
	entries, err := os.ReadDir(from)
	if err != nil {
		return fmt.Errorf("read %s: %w", from, err)
	}
	for _, e := range entries {
		if top && importSkip[e.Name()] {
			continue
		}
		src, dst := filepath.Join(from, e.Name()), filepath.Join(to, e.Name())
		switch {
		case e.IsDir():
			if err := c.mkdirAll(dst); err != nil {
				return err
			}
			if err := c.copyTree(src, dst, false); err != nil {
				return err
			}
		case e.Type().IsRegular():
			if err := c.copyFile(src, dst); err != nil {
				return err
			}
		default:
			return fmt.Errorf("%s is neither a file nor a folder (link or special file); nothing was imported", src)
		}
	}
	return nil
}

func (c *copier) copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	st, err := in.Stat()
	if err != nil {
		return err
	}
	out, err := os.OpenFile(dst, os.O_WRONLY|os.O_CREATE|os.O_EXCL, st.Mode().Perm()|0o600)
	if err != nil {
		if errors.Is(err, fs.ErrExist) {
			return fmt.Errorf("%s already exists; existing data is never overwritten", dst)
		}
		return err
	}
	c.created = append(c.created, dst)
	n, err := io.Copy(out, in)
	if err != nil {
		out.Close()
		return fmt.Errorf("copy %s: %w", src, err)
	}
	if err := out.Close(); err != nil {
		return fmt.Errorf("copy %s: %w", src, err)
	}
	c.files++
	c.bytes += n
	return nil
}

// fixCopiedDatabase checks the copied database and points the absolute paths stored in its settings that lie inside
// from to dest. The schema keeps ROMs, saves, firmware, cores, plugins and backups as paths relative to the data
// folder; only settings values (import folder, backup folder) can hold absolute ones. Returns how many it changed.
func fixCopiedDatabase(path, from, dest string) (int, error) {
	esc := strings.NewReplacer("%", "%25", "?", "%3F", "#", "%23").Replace(path)
	db, err := sql.Open("sqlite", "file:"+esc+"?_pragma=busy_timeout(5000)")
	if err != nil {
		return 0, err
	}
	defer db.Close()
	db.SetMaxOpenConns(1)
	if err := integrityOK(db); err != nil {
		return 0, err
	}
	rows, err := db.Query(`SELECT key, value FROM settings`)
	if err != nil {
		return 0, fmt.Errorf("read settings: %w", err)
	}
	type change struct{ key, val string }
	var changes []change
	for rows.Next() {
		var k, v string
		if err := rows.Scan(&k, &v); err != nil {
			rows.Close()
			return 0, err
		}
		if nv, ok := rebasePath(v, from, dest, runtime.GOOS == "windows"); ok {
			changes = append(changes, change{k, nv})
		}
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return 0, err
	}
	rows.Close()
	for _, ch := range changes {
		if _, err := db.Exec(`UPDATE settings SET value = ? WHERE key = ?`, ch.val, ch.key); err != nil {
			return 0, fmt.Errorf("update setting: %w", err)
		}
	}
	if err := integrityOK(db); err != nil {
		return 0, err
	}
	_, _ = db.Exec(`PRAGMA wal_checkpoint(TRUNCATE)`)
	return len(changes), nil
}

func integrityOK(db *sql.DB) error {
	var res string
	if err := db.QueryRow(`PRAGMA integrity_check`).Scan(&res); err != nil {
		return fmt.Errorf("integrity check: %w", err)
	}
	if res != "ok" {
		return fmt.Errorf("integrity check failed: %s", res)
	}
	return nil
}

// rebasePath maps a stored value that is an absolute path inside from to the same place inside to. Other values
// (including paths elsewhere) are left alone. fold compares case-insensitively (Windows).
func rebasePath(v, from, to string, fold bool) (string, bool) {
	if v == "" || !filepath.IsAbs(v) {
		return "", false
	}
	c := filepath.Clean(v)
	if len(c) < len(from) {
		return "", false
	}
	head, rest := c[:len(from)], c[len(from):]
	if head != from && !(fold && strings.EqualFold(head, from)) {
		return "", false
	}
	if rest != "" && rest[0] != filepath.Separator {
		return "", false
	}
	return filepath.Join(to, rest), true
}

func samePath(a, b string) bool {
	if runtime.GOOS == "windows" {
		return strings.EqualFold(a, b)
	}
	return a == b
}

// isInside reports whether child lies below parent.
func isInside(parent, child string) bool {
	rel, err := filepath.Rel(parent, child)
	if err != nil || (runtime.GOOS == "windows" && !samePath(filepath.VolumeName(parent), filepath.VolumeName(child))) {
		return false
	}
	return rel != "." && filepath.IsLocal(rel)
}

// writeMoved leaves a note in the old folder: the data now lives in dest.
func writeMoved(from, dest string) error {
	b, err := json.Marshal(struct {
		To string `json:"to"`
		At string `json:"at"`
	}{dest, time.Now().UTC().Format(time.RFC3339)})
	if err != nil {
		return err
	}
	tmp := filepath.Join(from, movedFile+".tmp")
	if err := os.WriteFile(tmp, append(b, '\n'), 0o640); err != nil {
		return err
	}
	if err := os.Rename(tmp, filepath.Join(from, movedFile)); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}
