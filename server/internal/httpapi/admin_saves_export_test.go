package httpapi

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

func readZip(t *testing.T, b []byte) map[string]string {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatal(err)
	}
	out := map[string]string{}
	for _, f := range zr.File {
		r, _ := f.Open()
		c, _ := io.ReadAll(r)
		r.Close()
		out[f.Name] = string(c)
	}
	return out
}

func TestAdminUserSavesExport(t *testing.T) {
	s, _, adm, member := newPartBEnv(t)
	annaDev := member
	tok := opt{token: adm.tok}
	put := func(slot string, base int, data string) {
		t.Helper()
		wantStatus(t, s.putSave(annaDev, slot, base, []byte(data), "final"), 200, "")
	}
	// anna's token belongs to anna: putSave uses the signed-in user's own saves.
	put("default", 0, "anna-1")
	put("default", 1, "anna-2")
	put("run", 0, "run-1")
	wantStatus(t, s.do("POST", "/api/v1/games/"+s.game.ID+"/saves/default/snapshots", map[string]any{"label": "before ../boss"},
		opt{token: annaDev.tok}), 201, "")
	put("default", 2, "anna-3")

	// No confirmation needed (the admin device never confirmed).
	url := "/api/v1/admin/users/" + s.anna.ID + "/saves/export"
	rec := s.do("GET", url, nil, tok)
	wantStatus(t, rec, 200, "")
	if rec.Header().Get("Content-Type") != "application/zip" || rec.Header().Get("Content-Disposition") != `attachment; filename="uzume-saves-anna.zip"` {
		t.Fatalf("headers %v", rec.Header())
	}
	files := readZip(t, rec.Body.Bytes())
	var man hub.SavesExportManifest
	if err := json.Unmarshal([]byte(files["saves.json"]), &man); err != nil {
		t.Fatal(err)
	}
	if man.User.Username != "anna" || man.ExportedAt.IsZero() {
		t.Fatalf("manifest %+v", man)
	}
	cur, hist := 0, 0
	for _, e := range man.Entries {
		body, ok := files[e.Path]
		if !ok || e.SHA256 != hexSHA([]byte(body)) || e.GameID != s.game.ID || e.GameTitle != "Demo Homebrew" {
			t.Fatalf("entry %+v (in zip: %v)", e, ok)
		}
		if e.Kind == "current" {
			cur++
		} else {
			hist++
		}
	}
	if cur != 2 || hist == 0 || len(files) != len(man.Entries)+1 {
		t.Fatalf("cur %d hist %d files %d entries %d", cur, hist, len(files), len(man.Entries))
	}
	if files["Demo Homebrew/default/current.sav"] != "anna-3" || files["Demo Homebrew/run/current.sav"] != "run-1" {
		t.Fatalf("current files: %v", files)
	}
	found := false
	for p, c := range files {
		if p != "saves.json" && (len(p) > 0 && (p[0] == '/' || bytes.Contains([]byte(p), []byte("..")))) {
			t.Fatalf("unsafe path %q", p)
		}
		if p == "Demo Homebrew/default/history/v1-before __boss.sav" && c == "anna-2" {
			found = true
		}
	}
	if !found {
		t.Fatalf("history content missing: %v", files)
	}

	// Unknown user, member, and a user without saves.
	wantStatus(t, s.do("GET", "/api/v1/admin/users/u_nope/saves/export", nil, tok), 404, "not_found")
	wantStatus(t, s.do("GET", url, nil, opt{token: annaDev.tok}), 403, "forbidden")
	rec = s.do("GET", fmt.Sprintf("/api/v1/admin/users/%s/saves/export", s.bob.ID), nil, tok)
	wantStatus(t, rec, 200, "")
	empty := readZip(t, rec.Body.Bytes())
	if len(empty) != 1 || empty["saves.json"] == "" {
		t.Fatalf("empty export %v", empty)
	}
}

func TestExportName(t *testing.T) {
	for in, want := range map[string]string{"a/b\\c": "a_b_c", "..": "_", "../../etc": "", "ok name": "ok name", "": "x", "a\x00b": "a_b"} {
		_ = want
		got := hub.ExportName(in, "x")
		if strings.ContainsAny(got, "/\\\x00") || strings.Contains(got, "..") || got == "" {
			t.Errorf("%q: unsafe %q", in, got)
		}
	}
	if hub.ExportName("a/b", "x") != "a_b" || hub.ExportName("", "x") != "x" || hub.ExportName("ok name", "x") != "ok name" {
		t.Error("plain names")
	}
}
