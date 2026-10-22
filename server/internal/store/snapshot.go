package store

import (
	"database/sql"
	"fmt"
	"io/fs"
	"os"
	"strconv"
	"strings"
)

// KnownSchemaVersion is the newest schema this Hub binary can migrate to.
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

// SnapshotTo writes a consistent copy of the database to dest with VACUUM INTO while the Hub keeps using it.
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

// OpenExisting opens an existing database file without migrating it (for the CLI next to a running Hub).
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
