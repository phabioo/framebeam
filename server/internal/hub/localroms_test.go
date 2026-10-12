package hub_test

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

// Synthetic ROM data only: headers built from the documented layouts.

// ndsDump builds an NDS file with a game code, ROM version (byte 0x1E) and a tail that makes the dump unique.
func ndsDump(code string, version byte, tail string) []byte {
	b := ndsROM(code)
	b[0x1E] = version
	return append(b, []byte(tail)...)
}

// ncsdDump builds a 3DS NCSD with a NCCH whose version (u16 LE at NCCH+0x112) is set.
func ncsdDump(id uint64, version uint16, tail string) []byte {
	b := ncsdROM(id)
	b = putAt(b, 0x2000+0x112, le(uint64(version), 2))
	return append(b, []byte(tail)...)
}

func shaOf(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }

func TestReadGameIdentityVersion(t *testing.T) {
	read := func(name string, data []byte, system string) (string, string, *int) {
		return hub.ReadGameIdentityVersion(writeTemp(t, name, data), system)
	}
	want := func(v *int, n int) bool { return v != nil && *v == n }
	if c, k, v := read("a.nds", ndsDump("ABCD", 7, ""), "nds"); c != "ABCD" || k != hub.GameCodeNDS || !want(v, 7) {
		t.Fatalf("nds %q %q %v", c, k, v)
	}
	if _, _, v := read("z.nds", ndsDump("ABCD", 0, ""), "nds"); !want(v, 0) {
		t.Fatalf("version 0 is a version, not unknown: %v", v)
	}
	if c, k, v := read("a.3ds", ncsdDump(0x0004000000123400, 0x0102, ""), "3ds"); c != "0004000000123400" || k != hub.Game3DS || !want(v, 0x0102) {
		t.Fatalf("ncsd %q %q %v", c, k, v)
	}
	b := putAt(bareNCCH(0x0004000000ABCDEF), 0x112, le(0xFFFE, 2))
	if c, _, v := read("a.cxi", b, "3ds"); c != "0004000000ABCDEF" || !want(v, 0xFFFE) {
		t.Fatalf("cxi %q %v", c, v)
	}
	// Unknown identity, unknown system: no version either.
	if c, k, v := read("b.nds", make([]byte, 0x200), "nds"); c != "" || k != "" || v != nil {
		t.Fatalf("bad %q %q %v", c, k, v)
	}
	if _, _, v := read("c.gba", ndsDump("ABCD", 7, ""), "gba"); v != nil {
		t.Fatalf("other system: %v", v)
	}
	// A header that ends before the version byte still has a code, but no version.
	if c, _, v := read("short.nds", []byte("0123456789ABCDEF"), "nds"); c != "CDEF" || v != nil {
		t.Fatalf("short %q %v", c, v)
	}
}

func TestAddROMMergesByIdentity(t *testing.T) {
	svc, _, admin := newAdmin(t)
	a, err := svc.AddROM(ctx, bytes.NewReader(ndsDump("ABCD", 1, "trimmed")), "a.nds", "Game A", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	if a.GameVersion == nil || *a.GameVersion != 1 {
		t.Fatalf("version %v", a.GameVersion)
	}
	// Another dump of the same identity: same game, a second stored file.
	second := ndsDump("ABCD", 1, "untrimmed-dump")
	b, err := svc.AddROM(ctx, bytes.NewReader(second), "a-untrimmed.nds", "ignored", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	if b.ID != a.ID || len(b.ROMs) != 2 || !b.OnHub || b.ROMs[0].SHA256 != a.ROMSHA256 {
		t.Fatalf("merge: %+v", b)
	}
	if f, _, err := svc.OpenROM(ctx, shaOf(second)); err != nil {
		t.Fatalf("second file must be downloadable: %v", err)
	} else {
		f.Close()
	}
	// Another version and another code stay separate games.
	c, _ := svc.AddROM(ctx, bytes.NewReader(ndsDump("ABCD", 2, "v2")), "c.nds", "", "", admin.ID)
	d, _ := svc.AddROM(ctx, bytes.NewReader(ndsDump("WXYZ", 1, "x")), "d.nds", "", "", admin.ID)
	if c.ID == a.ID || d.ID == a.ID || c.ID == d.ID {
		t.Fatal("different version or code must be separate games")
	}
	// The same bytes again is still a conflict naming the game.
	_, err = svc.AddROM(ctx, bytes.NewReader(second), "again.nds", "", "", admin.ID)
	var he *hub.Error
	if !errors.As(err, &he) || !errors.Is(err, hub.ErrConflict) || he.ExistingGameID != a.ID {
		t.Fatalf("duplicate: %v", err)
	}
	// Files without an identity never merge.
	e1, _ := svc.AddROM(ctx, bytes.NewReader([]byte("plain-file-one")), "e1.nds", "", "", admin.ID)
	e2, _ := svc.AddROM(ctx, bytes.NewReader([]byte("plain-file-two")), "e2.nds", "", "", admin.ID)
	if e1.ID == e2.ID {
		t.Fatal("no identity: no merge")
	}
	// Deleting the game removes all its stored files.
	if err := svc.DeleteGame(ctx, a.ID); err != nil {
		t.Fatal(err)
	}
	for _, sha := range []string{a.ROMSHA256, shaOf(second)} {
		if _, err := os.Stat(filepath.Join(svc.DataDir(), "roms", sha[:2], sha)); !errors.Is(err, os.ErrNotExist) {
			t.Fatalf("file %s still there: %v", sha[:8], err)
		}
	}
}

func idsOf(gs []hub.Game) map[string]bool {
	m := map[string]bool{}
	for _, g := range gs {
		m[g.ID] = true
	}
	return m
}

func TestRegisterLocalROMVisibility(t *testing.T) {
	svc, _, admin := newAdmin(t)
	anna, err := svc.CreateUser(ctx, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	bob, _ := svc.CreateUser(ctx, "bob", "Bob")
	data := ndsDump("LOCL", 0, "anna-dump")
	in := hub.RegisterInput{SHA256: shaOf(data), Size: int64(len(data)), Filename: "local.nds", GameCode: "LOCL", GameVersion: ptr(0)}

	g, created, err := svc.RegisterROM(ctx, anna.ID, in)
	if err != nil || !created {
		t.Fatalf("register: %v created=%v", err, created)
	}
	if g.OnHub || len(g.ROMs) != 1 || g.ROMs[0].Stored || g.GameCode != "LOCL" || g.GameVersion == nil || g.Title != "local" {
		t.Fatalf("%+v", g)
	}
	// No file of it is stored: the ROM download is 404; visible to Anna only.
	if _, _, err := svc.OpenROM(ctx, in.SHA256); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("download of a local file: %v", err)
	}
	for _, c := range []struct {
		user hub.User
		want bool
	}{{anna, true}, {bob, false}, {admin, false}} {
		gs, _ := svc.ListGamesFor(ctx, c.user.ID)
		_, err := svc.GetGameFor(ctx, c.user.ID, g.ID)
		if idsOf(gs)[g.ID] != c.want || (err == nil) != c.want {
			t.Fatalf("%s: list=%v get=%v want %v", c.user.Username, idsOf(gs)[g.ID], err, c.want)
		}
	}
	// The admin web library lists everything.
	if all, _ := svc.ListGames(ctx); !idsOf(all)[g.ID] {
		t.Fatal("ListGames must include local-only games")
	}
	// A second user with the same file: same game (match by sha256), 'created' false, now an owner as well.
	g2, created, err := svc.RegisterROM(ctx, bob.ID, in)
	if err != nil || created || g2.ID != g.ID {
		t.Fatalf("same sha: %v %v %v", err, created, g2.ID)
	}
	// A different dump of the same identity: same game, a second (local) file.
	other := ndsDump("LOCL", 0, "bobs-redump")
	g3, created, err := svc.RegisterROM(ctx, bob.ID, hub.RegisterInput{SHA256: shaOf(other), Size: int64(len(other)),
		Filename: "redump.nds", GameCode: "LOCL", GameVersion: ptr(0)})
	if err != nil || created || g3.ID != g.ID || len(g3.ROMs) != 2 {
		t.Fatalf("identity match: %v %v %+v", err, created, g3)
	}
	// Another version is another game.
	g4, created, _ := svc.RegisterROM(ctx, bob.ID, hub.RegisterInput{SHA256: shaOf([]byte("v1")), Size: 2, Filename: "v1.nds",
		GameCode: "LOCL", GameVersion: ptr(1)})
	if !created || g4.ID == g.ID {
		t.Fatalf("other version: %+v", g4)
	}
	// A code that does not fit the system is ignored (hash-only game), as is a code for a system without identity.
	g5, created, _ := svc.RegisterROM(ctx, bob.ID, hub.RegisterInput{SHA256: shaOf([]byte("v5")), Size: 2, Filename: "v5.nds", GameCode: "toolong"})
	if !created || g5.GameCode != "" {
		t.Fatalf("bad code: %+v", g5)
	}
	// Validation.
	for _, bad := range []hub.RegisterInput{
		{SHA256: "xyz", Size: 1, Filename: "a.nds"},
		{SHA256: shaOf([]byte("q")), Size: 0, Filename: "a.nds"},
		{SHA256: shaOf([]byte("q")), Size: 1, Filename: "a.txt"},
		{SHA256: shaOf([]byte("q")), Size: 1, Filename: "a.nds", System: "nope"},
		{SHA256: shaOf([]byte("q")), Size: hub.MaxROMBytes + 1, Filename: "a.nds"},
	} {
		if _, _, err := svc.RegisterROM(ctx, bob.ID, bad); !errors.Is(err, hub.ErrBadRequest) {
			t.Fatalf("%+v: %v", bad, err)
		}
	}

	// Once a file of the game is stored, everyone sees it (the game_id does not change); the local file is still not downloadable.
	up, err := svc.AddROM(ctx, bytes.NewReader(ndsDump("LOCL", 0, "uploaded-by-admin")), "admin.nds", "", "", admin.ID)
	if err != nil || up.ID != g.ID {
		t.Fatalf("upload merges into the local game: %v %v", err, up.ID)
	}
	if _, err := svc.GetGameFor(ctx, admin.ID, g.ID); err != nil {
		t.Fatalf("visible after upload: %v", err)
	}
	if !up.OnHub || up.ROMSHA256 == in.SHA256 {
		t.Fatalf("Game.rom must be the stored file: %+v", up)
	}
	if _, _, err := svc.OpenROM(ctx, in.SHA256); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("local file stays 404: %v", err)
	}

	// Removing Anna's registration: the game stays (stored file), her local file is no longer hers.
	if err := svc.UnregisterROM(ctx, anna.ID, g.ID); err != nil {
		t.Fatal(err)
	}
	owners, _ := svc.GameOwners(ctx)
	if len(owners[g.ID]) != 1 || owners[g.ID][0] != bob.ID {
		t.Fatalf("owners %v", owners[g.ID])
	}
	if err := svc.UnregisterROM(ctx, anna.ID, "00000000-0000-0000-0000-000000000000"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown game: %v", err)
	}
	// A purely local game disappears for the user after unregistering, but the game and its saves stay.
	if err := svc.UnregisterROM(ctx, bob.ID, g4.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetGameFor(ctx, bob.ID, g4.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("after unregister: %v", err)
	}
	if _, err := svc.GetGame(ctx, g4.ID); err != nil {
		t.Fatalf("the game stays: %v", err)
	}
}

func ptr(n int) *int { return &n }

func TestRegisterThenUploadKeepsGameID(t *testing.T) {
	svc, _, admin := newAdmin(t)
	data := bytes.Repeat([]byte("homebrew"), 30) // no identity
	g, _, err := svc.RegisterROM(ctx, admin.ID, hub.RegisterInput{SHA256: shaOf(data), Size: int64(len(data)), Filename: "x.nds"})
	if err != nil {
		t.Fatal(err)
	}
	st, err := svc.AddROM(ctx, bytes.NewReader(data), "x.nds", "", "", admin.ID)
	if err != nil || st.ID != g.ID || !st.OnHub || len(st.ROMs) != 1 || !st.ROMs[0].Stored {
		t.Fatalf("%v %+v", err, st)
	}
}

func chunkUpload(t *testing.T, svc *hub.Service, userID, id string, data []byte, from, to int) (hub.UploadSession, *hub.Game, error) {
	t.Helper()
	return svc.AppendUpload(ctx, userID, id, hub.UploadChunk{Start: int64(from), End: int64(to - 1), Total: int64(len(data))},
		bytes.NewReader(data[from:to]))
}

func TestUploadSessionResumeAndFinish(t *testing.T) {
	dir := t.TempDir()
	svc, _, _ := hubtest.OpenIn(t, dir, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	other, _ := svc.CreateUser(ctx, "anna", "Anna")
	data := ndsDump("UPLD", 3, string(bytes.Repeat([]byte("0123456789"), 40)))

	// A local-only game of the same identity exists before the upload; the upload keeps its game_id.
	local, _, _ := svc.RegisterROM(ctx, other.ID, hub.RegisterInput{SHA256: "11" + shaOf([]byte("x"))[2:], Size: 5, Filename: "l.nds", GameCode: "UPLD", GameVersion: ptr(3)})

	in := hub.UploadInput{Filename: "up.nds", Size: int64(len(data)), SHA256: shaOf(data), Title: "Uploaded"}
	u, err := svc.CreateUpload(ctx, admin.ID, in)
	if err != nil || u.Offset != 0 {
		t.Fatalf("%v %+v", err, u)
	}
	// Creating again returns the same session; so does name+size without a hash.
	if again, _ := svc.CreateUpload(ctx, admin.ID, in); again.ID != u.ID {
		t.Fatal("same sha256 must resume")
	}
	if again, _ := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "up.nds", Size: int64(len(data))}); again.ID != u.ID {
		t.Fatal("same name and size must resume")
	}
	// Another user gets their own session and cannot read or write this one.
	if o, _ := svc.CreateUpload(ctx, other.ID, in); o.ID == u.ID {
		t.Fatal("sessions are per user")
	}
	if _, err := svc.GetUpload(ctx, other.ID, u.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("foreign session: %v", err)
	}

	// Chunk order is enforced.
	if _, _, err := chunkUpload(t, svc, admin.ID, u.ID, data, 100, 200); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("out of order: %v", err)
	}
	if u, _, err = chunkUpload(t, svc, admin.ID, u.ID, data, 0, 150); err != nil || u.Offset != 150 {
		t.Fatalf("first chunk: %v %+v", err, u)
	}
	if _, _, err := chunkUpload(t, svc, admin.ID, u.ID, data, 0, 150); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("repeated chunk: %v", err)
	}
	// A short body is refused and the offset stays.
	if _, _, err := svc.AppendUpload(ctx, admin.ID, u.ID, hub.UploadChunk{Start: 150, End: 249, Total: int64(len(data))}, bytes.NewReader(data[150:200])); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("short chunk: %v", err)
	}
	if cur, _ := svc.GetUpload(ctx, admin.ID, u.ID); cur.Offset != 150 {
		t.Fatalf("offset after failed chunk %d", cur.Offset)
	}

	// "Restart": a new Service on the same data directory continues without re-sending anything.
	svc2, _, _ := hubtest.OpenIn(t, dir, nil)
	cur, err := svc2.GetUpload(ctx, admin.ID, u.ID)
	if err != nil || cur.Offset != 150 {
		t.Fatalf("after restart: %v %+v", err, cur)
	}
	if again, _ := svc2.CreateUpload(ctx, admin.ID, in); again.ID != u.ID || again.Offset != 150 {
		t.Fatalf("resume after restart: %+v", again)
	}
	var game *hub.Game
	for off := 150; off < len(data); off += 100 {
		end := min(off+100, len(data))
		if cur, game, err = chunkUpload(t, svc2, admin.ID, u.ID, data, off, end); err != nil {
			t.Fatal(err)
		}
		if (game != nil) != (end == len(data)) {
			t.Fatalf("game only with the last chunk: end=%d game=%v", end, game)
		}
	}
	if game == nil || game.ID != local.ID || !game.OnHub || game.Title != local.Title || len(game.ROMs) != 2 {
		t.Fatalf("finish must keep the local game: %+v (local %s)", game, local.ID)
	}
	f, _, err := svc2.OpenROM(ctx, shaOf(data))
	if err != nil {
		t.Fatalf("stored file: %v", err)
	}
	got := make([]byte, len(data)+1)
	n, _ := f.Read(got)
	f.Close()
	if !bytes.Equal(got[:n], data) {
		t.Fatal("stored content differs")
	}
	if _, err := svc2.GetUpload(ctx, admin.ID, u.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal("session must be gone after finishing")
	}
	if _, err := os.Stat(filepath.Join(dir, "tmp", "uploads", u.ID)); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("upload data must be gone: %v", err)
	}
	// A stored sha256 is refused up front with the existing game.
	_, err = svc2.CreateUpload(ctx, admin.ID, in)
	var he *hub.Error
	if !errors.As(err, &he) || !errors.Is(err, hub.ErrConflict) || he.ExistingGameID != local.ID {
		t.Fatalf("stored sha: %v", err)
	}
}

func TestUploadHashMismatchAndLimits(t *testing.T) {
	svc, _, admin := newAdmin(t)
	data := bytes.Repeat([]byte("abcdefgh"), 20)
	u, err := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "m.nds", Size: int64(len(data)), SHA256: shaOf([]byte("something else"))})
	if err != nil {
		t.Fatal(err)
	}
	if _, _, err := chunkUpload(t, svc, admin.ID, u.ID, data, 0, len(data)); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("hash mismatch: %v", err)
	}
	if _, err := svc.GetUpload(ctx, admin.ID, u.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal("a mismatching upload is dropped")
	}
	if gs, _ := svc.ListGames(ctx); len(gs) != 0 {
		t.Fatalf("nothing may be stored: %d", len(gs))
	}

	for _, bad := range []hub.UploadInput{
		{Filename: "", Size: 5},
		{Filename: "a.nds", Size: 0},
		{Filename: "a.txt", Size: 5},
		{Filename: "a.nds", Size: 5, SHA256: "nope"},
		{Filename: "a.nds", Size: 5, System: "nope"},
	} {
		if _, err := svc.CreateUpload(ctx, admin.ID, bad); !errors.Is(err, hub.ErrBadRequest) {
			t.Fatalf("%+v: %v", bad, err)
		}
	}
	if _, err := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "a.nds", Size: hub.MaxROMBytes + 1}); err == nil || !errors.Is(err, &hub.Error{Code: hub.CodePayloadTooLarge}) {
		t.Fatalf("size limit: %v", err)
	}
	// Content-Range must fit the upload; a chunk above 64 MiB is refused before anything is read.
	u2, _ := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "big.nds", Size: 200 << 20})
	if _, _, err := svc.AppendUpload(ctx, admin.ID, u2.ID, hub.UploadChunk{Start: 0, End: 64 << 20, Total: 200 << 20}, bytes.NewReader(nil)); !errors.Is(err, &hub.Error{Code: hub.CodePayloadTooLarge}) {
		t.Fatalf("chunk limit: %v", err)
	}
	if _, _, err := svc.AppendUpload(ctx, admin.ID, u2.ID, hub.UploadChunk{Start: 0, End: 9, Total: 5}, bytes.NewReader(nil)); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("total mismatch: %v", err)
	}
	// Abort removes the session and its data.
	if err := svc.AbortUpload(ctx, admin.ID, u2.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "tmp", "uploads", u2.ID)); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("data: %v", err)
	}
	if err := svc.AbortUpload(ctx, admin.ID, u2.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("second abort: %v", err)
	}
}

func TestUploadJanitor(t *testing.T) {
	svc, clk, admin := newAdmin(t)
	old, _ := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "old.nds", Size: 100})
	clk.Advance(6 * 24 * time.Hour)
	fresh, _ := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "fresh.nds", Size: 100})
	clk.Advance(25 * time.Hour) // old: 7 days + 1 h idle, fresh: 25 h
	if err := svc.Cleanup(ctx); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetUpload(ctx, admin.ID, old.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("idle session must be removed: %v", err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "tmp", "uploads", old.ID)); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("idle data must be removed: %v", err)
	}
	if _, err := svc.GetUpload(ctx, admin.ID, fresh.ID); err != nil {
		t.Fatalf("fresh session stays: %v", err)
	}
}

// Sessions survive a restart: the start-up cleanup of tmp/ leaves tmp/uploads alone.
func TestUploadDataSurvivesStartup(t *testing.T) {
	dir := t.TempDir()
	svc, _, _ := hubtest.OpenIn(t, dir, nil)
	admin, _ := svc.CreateAdmin(ctx, "admin", "secret-12345")
	data := bytes.Repeat([]byte("z"), 64)
	u, _ := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "s.nds", Size: int64(len(data))})
	if _, _, err := chunkUpload(t, svc, admin.ID, u.ID, data, 0, 32); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "tmp", "stale"), []byte("x"), 0o600); err != nil {
		t.Fatal(err)
	}
	svc2, _, _ := hubtest.OpenIn(t, dir, nil)
	if _, err := os.Stat(filepath.Join(dir, "tmp", "stale")); err == nil {
		t.Fatal("other tmp files are still removed at start")
	}
	_, g, err := chunkUpload(t, svc2, admin.ID, u.ID, data, 32, 64)
	if err != nil || g == nil || g.ROMSHA256 != shaOf(data) {
		t.Fatalf("resume after restart: %v %v", err, g)
	}
}

func TestBackfillGameVersions(t *testing.T) {
	dir := t.TempDir()
	svc, _, db := hubtest.OpenIn(t, dir, nil)
	admin, _ := svc.CreateAdmin(ctx, "admin", "secret-12345")
	n, _ := svc.AddROM(ctx, bytes.NewReader(ndsDump("BACK", 9, "")), "n.nds", "", "", admin.ID)
	p, _ := svc.AddROM(ctx, bytes.NewReader([]byte("no header")), "p.nds", "", "", admin.ID)
	// Games from before the migration: version NULL.
	if _, err := db.Exec(`UPDATE games SET game_version = NULL`); err != nil {
		t.Fatal(err)
	}
	if filled := svc.BackfillGameVersions(ctx); filled != 1 {
		t.Fatalf("filled %d", filled)
	}
	g, _ := svc.GetGame(ctx, n.ID)
	h, _ := svc.GetGame(ctx, p.ID)
	if g.GameVersion == nil || *g.GameVersion != 9 || h.GameVersion != nil {
		t.Fatalf("%v %v", g.GameVersion, h.GameVersion)
	}
}
