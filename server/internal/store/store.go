// Package store opens the SQLite database (pure Go driver) and runs embedded migrations.
package store

import (
	"database/sql"
	"embed"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	_ "modernc.org/sqlite" // driver "sqlite"
)

//go:embed migrations/*.sql
var migrationsFS embed.FS

// Open opens (and creates) the database at path with foreign keys and WAL and migrates the schema.
//
// Before pending migrations are applied to an existing database, a consistent copy is written to
// <dir of path>/backups (see MigrateWithBackup); a failing backup aborts the start.
func Open(path string) (*sql.DB, error) {
	esc := strings.NewReplacer("%", "%25", "?", "%3F", "#", "%23").Replace(path)
	dsn := "file:" + esc + "?_pragma=foreign_keys(1)&_pragma=journal_mode(WAL)&_pragma=busy_timeout(5000)"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, err
	}
	// Single connection: serializes writes, no SQLITE_BUSY within the process.
	db.SetMaxOpenConns(1)
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, err
	}
	if err := MigrateWithBackup(db, filepath.Join(filepath.Dir(path), "backups")); err != nil {
		db.Close()
		return nil, err
	}
	return db, nil
}

// SchemaVersion returns the currently applied schema version (0 = empty).
func SchemaVersion(db *sql.DB) (int, error) {
	if _, err := db.Exec(`CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)`); err != nil {
		return 0, err
	}
	var v sql.NullInt64
	if err := db.QueryRow(`SELECT MAX(version) FROM schema_version`).Scan(&v); err != nil {
		return 0, err
	}
	return int(v.Int64), nil
}

// Migrate applies all missing migrations (NNNN_name.sql), each in its own transaction, without a backup.
func Migrate(db *sql.DB) error { return migrate(db, "", 0) }

// MigrateWithBackup is Migrate, but before the first pending migration is applied to an existing (non-empty)
// database it writes VACUUM INTO <backupDir>/hub-<from>-to-<to>-<UTC yyyymmddThhmmssZ>.db and keeps the newest
// BackupsKept backups. A failed backup aborts: nothing is migrated.
func MigrateWithBackup(db *sql.DB, backupDir string) error { return migrate(db, backupDir, 0) }

// BackupsKept is the number of database backups that are kept.
const BackupsKept = 5

// nowFunc is the clock (tests).
var nowFunc = time.Now

// migrate applies migrations up to maxVersion (0 = all).
func migrate(db *sql.DB, backupDir string, maxVersion int) error {
	cur, err := SchemaVersion(db)
	if err != nil {
		return err
	}
	entries, err := fs.ReadDir(migrationsFS, "migrations")
	if err != nil {
		return err
	}
	names := make([]string, 0, len(entries))
	for _, e := range entries {
		names = append(names, e.Name())
	}
	sort.Strings(names)
	type mig struct {
		name string
		v    int
	}
	var pending []mig
	for _, name := range names {
		num, _, ok := strings.Cut(name, "_")
		v, err := strconv.Atoi(num)
		if !ok || err != nil {
			return fmt.Errorf("migration %q: invalid name", name)
		}
		if v <= cur || (maxVersion > 0 && v > maxVersion) {
			continue
		}
		pending = append(pending, mig{name, v})
	}
	if len(pending) > 0 && cur > 0 && backupDir != "" {
		if err := backup(db, backupDir, cur, pending[len(pending)-1].v); err != nil {
			return fmt.Errorf("database backup before migrating from schema %d to %d failed (not migrating): %w", cur, pending[len(pending)-1].v, err)
		}
	}
	for _, m := range pending {
		name, v := m.name, m.v
		body, err := migrationsFS.ReadFile("migrations/" + name)
		if err != nil {
			return err
		}
		tx, err := db.Begin()
		if err != nil {
			return err
		}
		if _, err := tx.Exec(string(body)); err != nil {
			tx.Rollback()
			return fmt.Errorf("migration %s: %w", name, err)
		}
		if _, err := tx.Exec(`INSERT INTO schema_version(version) VALUES (?)`, v); err != nil {
			tx.Rollback()
			return err
		}
		if err := tx.Commit(); err != nil {
			return err
		}
	}
	return nil
}

var backupNameRe = regexp.MustCompile(`^hub-\d+-to-\d+-(\d{8}T\d{6}Z)\.db$`)

// backup writes a consistent copy of the database with VACUUM INTO and prunes old backups.
func backup(db *sql.DB, dir string, from, to int) error {
	if err := os.MkdirAll(dir, 0o750); err != nil {
		return err
	}
	name := fmt.Sprintf("hub-%d-to-%d-%s.db", from, to, nowFunc().UTC().Format("20060102T150405Z"))
	target := filepath.Join(dir, name)
	if err := os.Remove(target); err != nil && !os.IsNotExist(err) { // VACUUM INTO needs a missing target
		return err
	}
	if _, err := db.Exec(`VACUUM INTO '` + strings.ReplaceAll(target, "'", "''") + `'`); err != nil {
		os.Remove(target)
		return err
	}
	if err := os.Chmod(target, 0o640); err != nil {
		return err
	}
	return pruneBackups(dir, BackupsKept)
}

// pruneBackups keeps the newest keep files named like a backup (by the timestamp in the name).
func pruneBackups(dir string, keep int) error {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return err
	}
	type b struct{ name, ts string }
	var all []b
	for _, e := range entries {
		if m := backupNameRe.FindStringSubmatch(e.Name()); m != nil && e.Type().IsRegular() {
			all = append(all, b{e.Name(), m[1]})
		}
	}
	sort.Slice(all, func(i, j int) bool {
		if all[i].ts != all[j].ts {
			return all[i].ts < all[j].ts
		}
		return all[i].name < all[j].name
	})
	for i := 0; i+keep < len(all); i++ {
		if err := os.Remove(filepath.Join(dir, all[i].name)); err != nil {
			return err
		}
	}
	return nil
}
