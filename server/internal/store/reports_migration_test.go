package store

import (
	"path/filepath"
	"testing"
)

func TestMigration0019ReportsKeepExistingDataAndCascade(t *testing.T) {
	p := filepath.Join(t.TempDir(), "uzume.db")
	raw := rawDB(t, p)
	if err := migrate(raw, "", 18); err != nil {
		t.Fatal(err)
	}
	if _, err := raw.Exec(`INSERT INTO users(id, username, display_name, role, created_at) VALUES ('u1', 'admin', 'admin', 'admin', 1)`); err != nil {
		t.Fatal(err)
	}
	raw.Close()

	db, err := Open(p)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	var n int
	if err := db.QueryRow(`SELECT COUNT(*) FROM users`).Scan(&n); err != nil || n != 1 {
		t.Fatalf("users %d (%v): the migration must keep existing rows", n, err)
	}
	if _, err := db.Exec(`INSERT INTO reports(user_id, user_name, device_id, device_name, title, description, created_at) VALUES ('u1', 'A', 'd1', 'Pc', 't', 'text', 5)`); err != nil {
		t.Fatal(err)
	}
	var st string
	if err := db.QueryRow(`SELECT status FROM reports WHERE id = 1`).Scan(&st); err != nil || st != "new" {
		t.Fatalf("status %q (%v): a report starts as new", st, err)
	}
	if _, err := db.Exec(`UPDATE reports SET status = 'other' WHERE id = 1`); err == nil {
		t.Fatal("an unknown status must be refused")
	}
	if _, err := db.Exec(`INSERT INTO report_files(report_id, name, size) VALUES (1, 'a.txt', 3)`); err != nil {
		t.Fatal(err)
	}
	if _, err := db.Exec(`INSERT INTO report_files(report_id, name, size) VALUES (9, 'a.txt', 3)`); err == nil {
		t.Fatal("a file of a missing report must be refused")
	}
	if _, err := db.Exec(`DELETE FROM reports WHERE id = 1`); err != nil {
		t.Fatal(err)
	}
	if err := db.QueryRow(`SELECT COUNT(*) FROM report_files`).Scan(&n); err != nil || n != 0 {
		t.Fatalf("report_files %d (%v): deleting a report removes its file rows", n, err)
	}
}
