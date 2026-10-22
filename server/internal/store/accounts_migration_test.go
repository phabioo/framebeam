package store

import (
	"path/filepath"
	"testing"
)

func TestMigration0020AccountsKeepExistingUsersAndDevices(t *testing.T) {
	p := filepath.Join(t.TempDir(), "uzume.db")
	raw := rawDB(t, p)
	if err := migrate(raw, "", 19); err != nil {
		t.Fatal(err)
	}
	for _, q := range []string{
		`INSERT INTO users(id, username, display_name, role, password_hash, created_at) VALUES ('u1', 'admin', 'admin', 'admin', '$argon2id$x', 1)`,
		`INSERT INTO users(id, username, display_name, role, created_at) VALUES ('u2', 'anna', 'Anna', 'user', 2)`,
		`INSERT INTO devices(id, user_id, name, platform, arch, player_version, credential_hash, status, created_at)
			VALUES ('d1', 'u2', 'PC', 'linux', 'x86_64', '0.17.0', 'h', 'trusted', 3)`,
		`INSERT INTO access_tokens(token_hash, device_id, expires_at) VALUES ('t1', 'd1', 99)`,
		`INSERT INTO pairing_requests(id, poll_token_hash, device_id, device_name, platform, arch, player_version, protocol_version, remote_addr, status, created_at, expires_at)
			VALUES ('p1', 'h', 'd2', 'PC', 'linux', 'x86_64', '0.17.0', 1, '192.0.2.1', 'pending', 4, 5)`,
	} {
		if _, err := raw.Exec(q); err != nil {
			t.Fatal(err)
		}
	}
	raw.Close()

	db, err := Open(p)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	var kind string
	var install, confirmed, locked any
	var failures int
	if err := db.QueryRow(`SELECT sign_in_kind, install_id, confirmed_until, confirm_failures, confirm_locked_until FROM devices WHERE id = 'd1'`).
		Scan(&kind, &install, &confirmed, &failures, &locked); err != nil {
		t.Fatal(err)
	}
	if kind != "approval" || install != nil || confirmed != nil || failures != 0 || locked != nil {
		t.Fatalf("device defaults: %q %v %v %d %v", kind, install, confirmed, failures, locked)
	}
	var must, sf int
	var changed, lockedUntil any
	var hash *string
	if err := db.QueryRow(`SELECT password_hash, password_changed_at, must_change_password, sign_in_failures, sign_in_locked_until FROM users WHERE id = 'u1'`).
		Scan(&hash, &changed, &must, &sf, &lockedUntil); err != nil {
		t.Fatal(err)
	}
	if hash == nil || *hash != "$argon2id$x" || changed != nil || must != 0 || sf != 0 || lockedUntil != nil {
		t.Fatalf("admin kept: %v %v %d %d %v", hash, changed, must, sf, lockedUntil)
	}
	if err := db.QueryRow(`SELECT password_hash FROM users WHERE id = 'u2'`).Scan(&hash); err != nil || hash != nil {
		t.Fatalf("a member stays without a password: %v %v", hash, err)
	}
	var tokens int
	if err := db.QueryRow(`SELECT COUNT(*) FROM access_tokens WHERE device_id = 'd1'`).Scan(&tokens); err != nil || tokens != 1 {
		t.Fatalf("tokens %d (%v)", tokens, err)
	}
	if err := db.QueryRow(`SELECT sign_in_kind FROM pairing_requests WHERE id = 'p1'`).Scan(&kind); err != nil || kind != "approval" {
		t.Fatalf("pairing kind %q (%v)", kind, err)
	}
	if _, err := db.Exec(`UPDATE devices SET sign_in_kind = 'other' WHERE id = 'd1'`); err == nil {
		t.Fatal("an unknown sign-in kind must be refused")
	}
	if _, err := db.Exec(`UPDATE devices SET sign_in_kind = 'password', install_id = 'i1' WHERE id = 'd1'`); err != nil {
		t.Fatal(err)
	}
}
