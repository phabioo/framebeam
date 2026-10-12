package hub_test

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

// Synthetic headers only (no real game data); the cases port client/emulation/tests/game_identity_test.cpp.

func putAt(b []byte, off int, v []byte) []byte {
	if len(b) < off+len(v) {
		b = append(b, make([]byte, off+len(v)-len(b))...)
	}
	copy(b[off:], v)
	return b
}

func le(v uint64, n int) []byte {
	r := make([]byte, n)
	for i := 0; i < n; i++ {
		r[i] = byte(v >> (8 * i))
	}
	return r
}

func ndsROM(code string) []byte { return putAt(make([]byte, 0x200), 0x0C, []byte(code)) }

func bareNCCH(id uint64) []byte {
	b := putAt(make([]byte, 0x200), 0x100, []byte("NCCH"))
	return putAt(b, 0x118, le(id, 8))
}

func ncsdROM(id uint64) []byte {
	b := putAt(make([]byte, 0x4000), 0x100, []byte("NCSD"))
	b = putAt(b, 0x120, le(0x10, 4)) // partition 0 at 0x10 * 0x200 = 0x2000
	b = putAt(b, 0x2000+0x100, []byte("NCCH"))
	return putAt(b, 0x2000+0x118, le(id, 8))
}

func writeTemp(t *testing.T, name string, data []byte) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(p, data, 0o644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestReadGameIdentity(t *testing.T) {
	id := func(name string, data []byte, system string) (string, string) {
		return hub.ReadGameIdentity(writeTemp(t, name, data), system)
	}
	if c, k := id("a.nds", ndsROM("ABCD"), "nds"); c != "ABCD" || k != "nds-gamecode" {
		t.Fatalf("nds %q %q", c, k)
	}
	if c, k := id("n.nds", ndsROM("A1B2"), "nds"); c != "A1B2" || k != "nds-gamecode" {
		t.Fatalf("nds digits %q %q", c, k)
	}
	// Invalid NDS: NUL bytes, lower case, other characters, file shorter than the code.
	for _, bad := range [][]byte{make([]byte, 0x200), ndsROM("abcd"), ndsROM("AB D"), ndsROM("AB\x7f\x80"), bytes.Repeat([]byte("x"), 8)} {
		if c, k := id("b.nds", bad, "nds"); c != "" || k != "" {
			t.Fatalf("invalid nds %q %q", c, k)
		}
	}
	// 3DS: bare NCCH, NCSD with partition 0, uppercase zero-padded hex.
	if c, k := id("a.cxi", bareNCCH(0x0004000000123400), "3ds"); c != "0004000000123400" || k != "3ds-titleid" {
		t.Fatalf("ncch %q %q", c, k)
	}
	if c, k := id("a.3ds", ncsdROM(0x000400000ABCDEF0), "3ds"); c != "000400000ABCDEF0" || k != "3ds-titleid" {
		t.Fatalf("ncsd %q %q", c, k)
	}
	if c, k := id("z.cxi", bareNCCH(0x42), "3ds"); c != "0000000000000042" || k != "3ds-titleid" {
		t.Fatalf("padding %q %q", c, k)
	}
	// Invalid 3DS: no magic (encrypted or compressed), partition beyond the file, no partition.
	if c, k := id("zero.3ds", make([]byte, 0x400), "3ds"); c != "" || k != "" {
		t.Fatalf("zero %q %q", c, k)
	}
	b := putAt(make([]byte, 0x400), 0x100, []byte("NCSD"))
	b = putAt(b, 0x120, le(0x1000000, 4))
	if c, k := id("trunc.3ds", b, "3ds"); c != "" || k != "" {
		t.Fatalf("trunc %q %q", c, k)
	}
	b = putAt(b, 0x120, le(0, 4))
	if c, k := id("nopart.3ds", b, "3ds"); c != "" || k != "" {
		t.Fatalf("nopart %q %q", c, k)
	}
	// Partition whose NCCH magic is missing.
	b = putAt(make([]byte, 0x4000), 0x100, []byte("NCSD"))
	b = putAt(b, 0x120, le(0x10, 4))
	if c, k := id("nomagic.3ds", b, "3ds"); c != "" || k != "" {
		t.Fatalf("nomagic %q %q", c, k)
	}
	// Another system, missing file, empty path.
	if c, k := id("c.bin", bytes.Repeat([]byte("A"), 0x200), "gba"); c != "" || k != "" {
		t.Fatalf("gba %q %q", c, k)
	}
	if c, k := hub.ReadGameIdentity(filepath.Join(t.TempDir(), "missing.nds"), "nds"); c != "" || k != "" {
		t.Fatalf("missing %q %q", c, k)
	}
	if c, k := hub.ReadGameIdentity("", "nds"); c != "" || k != "" {
		t.Fatalf("empty path %q %q", c, k)
	}
}

func TestUploadAndImportFillGameCode(t *testing.T) {
	svc, _, admin := newAdmin(t)
	g, err := svc.AddROM(ctx, bytes.NewReader(ndsROM("ABCD")), "one.nds", "", "", admin.ID)
	if err != nil || g.GameCode != "ABCD" || g.GameCodeKind != "nds-gamecode" {
		t.Fatalf("%+v %v", g, err)
	}
	g3, err := svc.AddROM(ctx, bytes.NewReader(ncsdROM(0x0004000000ABCDEF)), "two.3ds", "", "", admin.ID)
	if err != nil || g3.GameCode != "0004000000ABCDEF" || g3.GameCodeKind != "3ds-titleid" {
		t.Fatalf("%+v %v", g3, err)
	}
	// Unreadable header: stored without a code, exposed as empty strings.
	gu, err := svc.AddROM(ctx, bytes.NewReader([]byte("dummy-rom-bytes")), "three.nds", "", "", admin.ID)
	if err != nil || gu.GameCode != "" || gu.GameCodeKind != "" {
		t.Fatalf("%+v %v", gu, err)
	}

	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "imp.cxi"), bareNCCH(0x0004000000FFFF00), 0o644); err != nil {
		t.Fatal(err)
	}
	if sum, err := svc.ImportFolder(ctx, dir, admin.ID, 0); err != nil || sum.Added != 1 {
		t.Fatalf("%+v %v", sum, err)
	}
	games, err := svc.ListGames(ctx)
	if err != nil {
		t.Fatal(err)
	}
	got := map[string][2]string{}
	for _, x := range games {
		got[x.Filename] = [2]string{x.GameCode, x.GameCodeKind}
	}
	want := map[string][2]string{
		"one.nds":   {"ABCD", "nds-gamecode"},
		"two.3ds":   {"0004000000ABCDEF", "3ds-titleid"},
		"three.nds": {"", ""},
		"imp.cxi":   {"0004000000FFFF00", "3ds-titleid"},
	}
	for f, w := range want {
		if got[f] != w {
			t.Fatalf("%s: got %v want %v", f, got[f], w)
		}
	}
}

func TestBackfillGameCodes(t *testing.T) {
	dir := t.TempDir()
	svc, _, db := hubtest.OpenIn(t, dir, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	good, err := svc.AddROM(ctx, bytes.NewReader(ndsROM("WXYZ")), "good.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	bad, err := svc.AddROM(ctx, bytes.NewReader([]byte("not a header")), "bad.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	// Simulate games from before the migration: no code, never tried.
	if _, err := db.Exec(`UPDATE games SET game_code = '', game_code_kind = ''`); err != nil {
		t.Fatal(err)
	}
	if filled, unreadable := svc.BackfillGameCodes(ctx); filled != 1 || unreadable != 1 {
		t.Fatalf("filled %d unreadable %d", filled, unreadable)
	}
	g, _ := svc.GetGame(ctx, good.ID)
	b, _ := svc.GetGame(ctx, bad.ID)
	if g.GameCode != "WXYZ" || g.GameCodeKind != "nds-gamecode" || b.GameCode != "" || b.GameCodeKind != "" {
		t.Fatalf("%+v %+v", g, b)
	}
	var kind string
	if err := db.QueryRow(`SELECT game_code_kind FROM games WHERE id = ?`, bad.ID).Scan(&kind); err != nil || kind != "none" {
		t.Fatalf("unreadable game must be marked as tried: %q %v", kind, err)
	}

	// The unreadable game is not read again: even when its file now has a valid header, nothing changes.
	rom := filepath.Join(dir, "roms", bad.ROMSHA256[:2], bad.ROMSHA256)
	if err := os.WriteFile(rom, ndsROM("NEWC"), 0o644); err != nil {
		t.Fatal(err)
	}
	if filled, unreadable := svc.BackfillGameCodes(ctx); filled != 0 || unreadable != 0 {
		t.Fatalf("second run filled %d unreadable %d", filled, unreadable)
	}
	// Same on a restart of the Hub (Open runs the backfill).
	svc2, _, _ := hubtest.OpenIn(t, dir, nil)
	b2, _ := svc2.GetGame(ctx, bad.ID)
	if b2.GameCode != "" || b2.GameCodeKind != "" {
		t.Fatalf("%+v", b2)
	}
}
