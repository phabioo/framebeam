// Package store öffnet die SQLite-Datenbank (reiner Go-Treiber) und führt eingebettete Migrationen aus.
package store

import (
	"database/sql"
	"embed"
	"fmt"
	"io/fs"
	"sort"
	"strconv"
	"strings"

	_ "modernc.org/sqlite" // Treiber "sqlite"
)

//go:embed migrations/*.sql
var migrationsFS embed.FS

// Open öffnet (und erzeugt) die Datenbank unter path mit Foreign Keys und WAL und migriert das Schema.
func Open(path string) (*sql.DB, error) {
	esc := strings.NewReplacer("%", "%25", "?", "%3F", "#", "%23").Replace(path)
	dsn := "file:" + esc + "?_pragma=foreign_keys(1)&_pragma=journal_mode(WAL)&_pragma=busy_timeout(5000)"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, err
	}
	// Eine Verbindung: serialisiert Schreibzugriffe, kein SQLITE_BUSY im Prozess.
	db.SetMaxOpenConns(1)
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, err
	}
	if err := Migrate(db); err != nil {
		db.Close()
		return nil, err
	}
	return db, nil
}

// SchemaVersion liefert die aktuell angewendete Schema-Version (0 = leer).
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

// Migrate wendet alle noch fehlenden Migrationen (NNNN_name.sql) je in einer Transaktion an.
func Migrate(db *sql.DB) error {
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
	for _, name := range names {
		num, _, ok := strings.Cut(name, "_")
		v, err := strconv.Atoi(num)
		if !ok || err != nil {
			return fmt.Errorf("migration %q: ungültiger Name", name)
		}
		if v <= cur {
			continue
		}
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
