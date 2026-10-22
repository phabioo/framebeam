package store

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestOpenRefusesNewerSchema(t *testing.T) {
	p := filepath.Join(t.TempDir(), "db.sqlite")
	db, err := Open(p)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := db.Exec(`INSERT INTO schema_version(version) VALUES (9999)`); err != nil {
		t.Fatal(err)
	}
	db.Close()
	db, err = Open(p)
	if err == nil {
		db.Close()
		t.Fatal("a database newer than the binary must be refused")
	}
	if !strings.Contains(err.Error(), "newer than this server") {
		t.Fatalf("error %q", err)
	}
}
