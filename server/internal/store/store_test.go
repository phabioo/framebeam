package store

import (
	"database/sql"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"
)

func TestOpenMigrateIdempotent(t *testing.T) {
	p := filepath.Join(t.TempDir(), "db.sqlite")
	db, err := Open(p)
	if err != nil {
		t.Fatal(err)
	}
	v, _ := SchemaVersion(db)
	if v != 5 {
		t.Fatalf("version %d", v)
	}
	var fk int
	db.QueryRow(`PRAGMA foreign_keys`).Scan(&fk)
	if fk != 1 {
		t.Fatal("foreign keys off")
	}
	db.Close()
	db, err = Open(p) // second start does not migrate again
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	if v, _ := SchemaVersion(db); v != 5 {
		t.Fatalf("version after restart %d", v)
	}
	if _, err := db.Exec(`INSERT INTO devices(id,user_id,name,platform,arch,player_version,credential_hash,status,created_at) VALUES('d','nouser','n','p','a','v','h','trusted',1)`); err == nil {
		t.Fatal("FK violation must fail")
	}
}

func rawDB(t *testing.T, path string) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite", "file:"+path+"?_pragma=foreign_keys(1)")
	if err != nil {
		t.Fatal(err)
	}
	db.SetMaxOpenConns(1)
	return db
}

func backupFiles(t *testing.T, dir string) []string {
	t.Helper()
	es, _ := os.ReadDir(dir)
	var out []string
	for _, e := range es {
		out = append(out, e.Name())
	}
	return out
}

func TestBackupBeforeMigration(t *testing.T) {
	old := nowFunc
	nowFunc = func() time.Time { return time.Date(2026, 10, 7, 12, 30, 45, 0, time.UTC) }
	t.Cleanup(func() { nowFunc = old })
	dir := t.TempDir()
	p := filepath.Join(dir, "framebeam.db")

	// A fresh database is not backed up.
	db, err := Open(p)
	if err != nil {
		t.Fatal(err)
	}
	db.Close()
	if _, err := os.Stat(filepath.Join(dir, "backups")); err == nil {
		t.Fatalf("fresh database must not be backed up: %v", backupFiles(t, filepath.Join(dir, "backups")))
	}
	// An up-to-date database is not backed up either.
	db, _ = Open(p)
	db.Close()
	if _, err := os.Stat(filepath.Join(dir, "backups")); err == nil {
		t.Fatal("no pending migration, no backup")
	}

	// An existing database at schema 3 gets a backup before 4 and 5 are applied.
	p2 := filepath.Join(dir, "old.db")
	raw := rawDB(t, p2)
	if err := migrate(raw, "", 3); err != nil {
		t.Fatal(err)
	}
	raw.Close()
	db, err = Open(p2)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	if v, _ := SchemaVersion(db); v != 5 {
		t.Fatalf("version %d", v)
	}
	bdir := filepath.Join(dir, "backups")
	got := backupFiles(t, bdir)
	if len(got) != 1 || got[0] != "hub-3-to-5-20261007T123045Z.db" {
		t.Fatalf("backups: %v", got)
	}
	bk := rawDB(t, filepath.Join(bdir, got[0]))
	defer bk.Close()
	if v, err := SchemaVersion(bk); err != nil || v != 3 {
		t.Fatalf("backup holds schema %d, %v", v, err)
	}
}

func TestBackupsPrunedToFive(t *testing.T) {
	dir := t.TempDir()
	for _, n := range []string{"hub-1-to-2-20260101T000000Z.db", "hub-2-to-3-20260102T000000Z.db", "hub-3-to-4-20260103T000000Z.db",
		"hub-4-to-5-20260104T000000Z.db", "hub-4-to-5-20260105T000000Z.db", "hub-4-to-5-20260106T000000Z.db", "notes.txt"} {
		os.WriteFile(filepath.Join(dir, n), nil, 0o600)
	}
	if err := pruneBackups(dir, BackupsKept); err != nil {
		t.Fatal(err)
	}
	got := backupFiles(t, dir)
	want := []string{"hub-2-to-3-20260102T000000Z.db", "hub-3-to-4-20260103T000000Z.db", "hub-4-to-5-20260104T000000Z.db",
		"hub-4-to-5-20260105T000000Z.db", "hub-4-to-5-20260106T000000Z.db", "notes.txt"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v", got)
	}
}

func TestBackupFailureAbortsMigration(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "old.db")
	raw := rawDB(t, p)
	if err := migrate(raw, "", 3); err != nil {
		t.Fatal(err)
	}
	// The backup directory cannot be created: a regular file is in the way.
	blocker := filepath.Join(dir, "backups")
	os.WriteFile(blocker, []byte("x"), 0o600)
	err := MigrateWithBackup(raw, blocker)
	if err == nil || !strings.Contains(err.Error(), "backup") {
		t.Fatalf("want a clear backup error, got %v", err)
	}
	if v, _ := SchemaVersion(raw); v != 3 {
		t.Fatalf("migration ran despite the failed backup: schema %d", v)
	}
	raw.Close()
	// Open fails the same way (startup aborts).
	os.Remove(blocker)
	os.WriteFile(blocker, []byte("x"), 0o600)
	if _, err := Open(p); err == nil || !strings.Contains(err.Error(), "backup") {
		t.Fatalf("Open: %v", err)
	}
}
