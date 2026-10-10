package hub_test

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func TestImportFolder(t *testing.T) {
	svc, _, admin := newAdmin(t)
	dir := t.TempDir()
	w := func(name, data string) {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(data), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	w("one.nds", "dummy-one")
	w("two.NDS", "dummy-two") // extension match is case-insensitive
	w("dup.nds", "dummy-one")
	w("notes.txt", "x")
	w("empty.nds", "")
	w("big.nds", "0123456789")
	w(".hidden.nds", "dummy-hidden")
	if err := os.Mkdir(filepath.Join(dir, "sub.nds"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "sub.nds", "inner.nds"), []byte("dummy-inner"), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader([]byte("dummy-two")), "pre.nds", "", "", admin.ID); err != nil {
		t.Fatal(err)
	}

	sum, err := svc.ImportFolder(ctx, dir, admin.ID, 9) // 9 byte limit: "big.nds" (10 B) fails
	if err != nil {
		t.Fatal(err)
	}
	// Name order: big (too large), dup (added), empty (fails), notes.txt (unsupported), one (same content as dup),
	// two.NDS (same content as pre.nds).
	if sum.Added != 1 || sum.AlreadyHere != 2 || sum.Unsupported != 1 || len(sum.Failed) != 2 {
		t.Fatalf("%+v", sum)
	}
	reasons := map[string]string{}
	for _, f := range sum.Failed {
		reasons[f.File] = f.Reason
	}
	if reasons["big.nds"] == "" || reasons["empty.nds"] == "" {
		t.Fatalf("%+v", sum.Failed)
	}
	games, _ := svc.ListGames(ctx)
	if len(games) != 2 { // pre + dup
		titles := []string{}
		for _, g := range games {
			titles = append(titles, g.Title)
		}
		t.Fatalf("games %v", titles)
	}
	// Sources untouched.
	ents, _ := os.ReadDir(dir)
	if len(ents) != 8 {
		t.Fatalf("entries %d", len(ents))
	}
	if b, _ := os.ReadFile(filepath.Join(dir, "one.nds")); string(b) != "dummy-one" {
		t.Fatal("source modified")
	}
	// Idempotent.
	sum, err = svc.ImportFolder(ctx, dir, admin.ID, 9)
	if err != nil || sum.Added != 0 {
		t.Fatalf("%+v %v", sum, err)
	}
	if _, err := svc.ImportFolder(ctx, filepath.Join(dir, "missing"), admin.ID, 0); err == nil {
		t.Fatal("missing dir must fail")
	}
}

func TestImportThreeDSDummies(t *testing.T) {
	svc, _, admin := newAdmin(t)
	dir := t.TempDir()
	for _, n := range []string{"a.3ds", "b.CCI", "c.cxi", "d.3dsx", "e.zcci", "f.zcxi", "g.z3dsx", "h.elf", "i.axf", "j.app"} {
		if err := os.WriteFile(filepath.Join(dir, n), []byte("dummy-"+n), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	sum, err := svc.ImportFolder(ctx, dir, admin.ID, 1<<20)
	if err != nil {
		t.Fatal(err)
	}
	if sum.Added != 7 || sum.Unsupported != 3 || len(sum.Failed) != 0 {
		t.Fatalf("%+v", sum)
	}
	games, _ := svc.ListGames(ctx)
	if len(games) != 7 {
		t.Fatal(len(games))
	}
	for _, g := range games {
		if g.System != "3ds" {
			t.Fatalf("%+v", g)
		}
	}
	g, err := svc.AddROM(ctx, bytes.NewReader([]byte("dummy-upload")), "up.3ds", "", "", admin.ID)
	if err != nil || g.System != "3ds" {
		t.Fatalf("%v %+v", err, g)
	}
}
