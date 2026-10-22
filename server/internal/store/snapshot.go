package store

import (
	"database/sql"
	"fmt"
	"io/fs"
	"os"
	"strconv"
	"strings"
)

// KnownSchemaVersion is the newest schema this server binary can migrate to.
func KnownSchemaVersion() int {
	ents, err := fs.ReadDir(migrationsFS, "migrations")
	if err != nil {
		return 0
	}
	maxKnown := 0
	for _, e := range ents {
		num, _, ok := strings.Cut(e.Name(), "_")
		if v, err := strconv.Atoi(num); ok && err == nil {
			maxKnown = max(maxKnown, v)
		}
	}
	return maxKnown
}

// SnapshotTo writes a consistent copy of the database to dest with VACUUM INTO while the server keeps using it.
// dest must not exist.
func SnapshotTo(db *sql.DB, dest string) error {
	if err := os.Remove(dest); err != nil && !os.IsNotExist(err) {
		return err
	}
	if _, err := db.Exec(`VACUUM INTO '` + strings.ReplaceAll(dest, "'", "''") + `'`); err != nil {
		os.Remove(dest)
		return fmt.Errorf("vacuum into: %w", err)
	}
	return os.Chmod(dest, 0o600)
}

// PutSettings writes settings into the database file at path without migrating it (the update rollback prepares a
// restored pre-update database for an older server, which may use an older schema). The settings table exists in
// every schema.
func PutSettings(path string, kv map[string]string) error {
	esc := strings.NewReplacer("%", "%25", "?", "%3F", "#", "%23").Replace(path)
	db, err := sql.Open("sqlite", "file:"+esc+"?_pragma=busy_timeout(5000)")
	if err != nil {
		return err
	}
	defer db.Close()
	db.SetMaxOpenConns(1)
	tx, err := db.Begin()
	if err != nil {
		return err
	}
	for k, v := range kv {
		if _, err := tx.Exec(`INSERT INTO settings(key, value) VALUES (?,?) ON CONFLICT(key) DO UPDATE SET value = excluded.value`, k, v); err != nil {
			tx.Rollback()
			return err
		}
	}
	return tx.Commit()
}

// OpenExisting opens an existing database file without migrating it (for the CLI next to a running server).
func OpenExisting(path string) (*sql.DB, error) {
	if _, err := os.Stat(path); err != nil {
		return nil, err
	}
	esc := strings.NewReplacer("%", "%25", "?", "%3F", "#", "%23").Replace(path)
	db, err := sql.Open("sqlite", "file:"+esc+"?_pragma=foreign_keys(1)&_pragma=busy_timeout(5000)")
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, err
	}
	return db, nil
}
