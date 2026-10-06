package store

import (
	"path/filepath"
	"testing"
)

func TestOpenMigrateIdempotent(t *testing.T) {
	p := filepath.Join(t.TempDir(), "db.sqlite")
	db, err := Open(p)
	if err != nil {
		t.Fatal(err)
	}
	v, _ := SchemaVersion(db)
	if v != 4 {
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
	if v, _ := SchemaVersion(db); v != 4 {
		t.Fatalf("version after restart %d", v)
	}
	if _, err := db.Exec(`INSERT INTO devices(id,user_id,name,platform,arch,player_version,credential_hash,status,created_at) VALUES('d','nouser','n','p','a','v','h','trusted',1)`); err == nil {
		t.Fatal("FK violation must fail")
	}
}
