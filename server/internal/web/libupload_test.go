package web

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

func upJSON(t *testing.T, body string) map[string]any {
	t.Helper()
	m := map[string]any{}
	if err := json.Unmarshal([]byte(body), &m); err != nil {
		t.Fatalf("JSON: %v (%s)", err, body)
	}
	return m
}

func TestResumableWebUpload(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	data := append(make([]byte, 0x200), bytes.Repeat([]byte("web-rom"), 100)...)
	copy(data[0x0C:], "WEBU")
	sum := sha256.Sum256(data)
	sha := hex.EncodeToString(sum[:])
	create := fmt.Sprintf(`{"filename":"web.nds","size":%d,"sha256":%q,"title":"Web Upload"}`, len(data), sha)
	jsonH := map[string]string{"Content-Type": "application/json", "X-CSRF-Token": tok}

	// CSRF: every state-changing route needs the token in X-CSRF-Token.
	status(t, c.do("POST", "/library/uploads", strings.NewReader(create), map[string]string{"Content-Type": "application/json"}), http.StatusForbidden)
	status(t, c.do("POST", "/library/uploads", strings.NewReader(create), map[string]string{"Content-Type": "application/json", "X-CSRF-Token": "wrong"}), http.StatusForbidden)
	status(t, c.do("POST", "/library/uploads/x/abort", nil, nil), http.StatusForbidden)
	// Not signed in: redirect to the login, nothing is created.
	anon := e.client()
	if rec := anon.do("POST", "/library/uploads", strings.NewReader(create), jsonH); rec.Code != http.StatusSeeOther {
		t.Fatalf("anonymous: %d", rec.Code)
	}

	rec := c.do("POST", "/library/uploads", strings.NewReader(create), jsonH)
	status(t, rec, http.StatusOK)
	s := upJSON(t, rec.Body.String())
	id, _ := s["upload_id"].(string)
	if id == "" || s["offset"].(float64) != 0 || s["chunk_size"].(float64) != float64(hub.UploadChunkBytes) {
		t.Fatalf("%v", s)
	}
	chunk := func(from, to int, hdr map[string]string) *httpRecorder {
		h := map[string]string{"Content-Type": "application/octet-stream", "X-CSRF-Token": tok,
			"Content-Range": fmt.Sprintf("bytes %d-%d/%d", from, to-1, len(data))}
		for k, v := range hdr {
			h[k] = v
		}
		return c.do("POST", "/library/uploads/"+id+"/chunk", bytes.NewReader(data[from:to]), h)
	}
	// The raw chunk route also needs the token (and does not parse the binary body as a form).
	status(t, chunk(0, 100, map[string]string{"X-CSRF-Token": ""}), http.StatusForbidden)
	status(t, chunk(100, 200, nil), http.StatusConflict) // not at the offset
	rec = chunk(0, 500, nil)
	status(t, rec, http.StatusOK)
	if r := upJSON(t, rec.Body.String()); r["offset"].(float64) != 500 || r["done"] != nil {
		t.Fatalf("%v", r)
	}
	// A different file with the same name and size never continues that session (HA-4), and a request without a
	// hash is refused.
	other := strings.Repeat("ab", 32)
	rec = c.do("POST", "/library/uploads", strings.NewReader(fmt.Sprintf(`{"filename":"web.nds","size":%d,"sha256":%q}`, len(data), other)), jsonH)
	status(t, rec, http.StatusOK)
	if r := upJSON(t, rec.Body.String()); r["upload_id"] == id || r["offset"].(float64) != 0 {
		t.Fatalf("a different hash resumed the session: %v", r)
	}
	status(t, c.do("POST", "/library/uploads", strings.NewReader(fmt.Sprintf(`{"filename":"web.nds","size":%d}`, len(data))), jsonH), http.StatusBadRequest)
	// "Reload": the same file (same hash) resumes the session at the stored offset.
	rec = c.do("POST", "/library/uploads", strings.NewReader(create), jsonH)
	status(t, rec, http.StatusOK)
	if r := upJSON(t, rec.Body.String()); r["upload_id"] != id || r["offset"].(float64) != 500 {
		t.Fatalf("resume: %v", r)
	}
	rec = c.get("/library/uploads/"+id, nil)
	status(t, rec, http.StatusOK)
	if r := upJSON(t, rec.Body.String()); r["offset"].(float64) != 500 {
		t.Fatalf("%v", r)
	}
	rec = chunk(500, len(data), nil)
	status(t, rec, http.StatusOK)
	r := upJSON(t, rec.Body.String())
	game, _ := r["game"].(map[string]any)
	if r["done"] != true || game == nil || game["title"] != "Web Upload" {
		t.Fatalf("finish: %v", r)
	}
	g, err := e.svc.GetGame(bg, game["id"].(string))
	if err != nil || !g.OnHub || g.ROMSHA256 != sha || g.GameCode != "WEBU" {
		t.Fatalf("%v %+v", err, g)
	}
	// The same file again: refused with a readable message.
	rec = c.do("POST", "/library/uploads", strings.NewReader(fmt.Sprintf(`{"filename":"web.nds","size":%d,"sha256":%q}`, len(data), sha)), jsonH)
	status(t, rec, http.StatusConflict)
	contains(t, rec, "already in the library")
	// Abort.
	rec = c.do("POST", "/library/uploads", strings.NewReader(`{"filename":"other.nds","size":10,"sha256":"`+other+`"}`), jsonH)
	status(t, rec, http.StatusOK)
	id2 := upJSON(t, rec.Body.String())["upload_id"].(string)
	status(t, c.do("POST", "/library/uploads/"+id2+"/abort", nil, map[string]string{"X-CSRF-Token": tok}), http.StatusOK)
	status(t, c.get("/library/uploads/"+id2, nil), http.StatusNotFound)
	// The configured web limit applies.
	e2 := newEnv(t, true, func(cf *Config) { cf.MaxUploadBytes = 1000 })
	c2 := e2.client()
	tok2 := c2.login()
	status(t, c2.do("POST", "/library/uploads", strings.NewReader(`{"filename":"a.nds","size":1001,"sha256":"`+other+`"}`),
		map[string]string{"Content-Type": "application/json", "X-CSRF-Token": tok2}), http.StatusRequestEntityTooLarge)
}

type httpRecorder = httptest.ResponseRecorder

func TestLibraryShowsLocalOnlyGames(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	users, _ := e.svc.ListUsers(bg)
	admin := users[0]
	anna, err := e.svc.CreateUser(bg, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	dump := func(tail string) []byte {
		b := make([]byte, 0x200)
		copy(b[0x0C:], "LOCL")
		return append(b, tail...)
	}
	sha := func(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }
	l1 := dump("one")
	g, _, err := e.svc.RegisterROM(bg, anna.ID, hub.RegisterInput{SHA256: sha(l1), Size: int64(len(l1)), Filename: "local.nds", Title: "Only Local", GameCode: "LOCL", GameVersion: ptrInt(0)})
	if err != nil {
		t.Fatal(err)
	}
	stored, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-stored-rom")), "stored.nds", "Stored Game", "", admin.ID)

	rec := c.get("/library", nil)
	status(t, rec, http.StatusOK)
	contains(t, rec, "Only Local", "Only on Anna · not uploaded", "Details", "Stored Game", "data-resumable-upload", "/static/libupload.js")
	if n := strings.Count(rec.Body.String(), "not uploaded"); n != 1 {
		t.Fatalf("local-only line count %d (the stored game must not carry it)", n)
	}
	// A stored second file turns it into a normal game with two files.
	if _, err := e.svc.AddROM(bg, bytes.NewReader(dump("two")), "two.nds", "", "", admin.ID); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/library", nil)
	contains(t, rec, "Only Local", "2 files")
	notContains(t, rec, "not uploaded")
	_ = g
	_ = stored
	// Deleting the game in the web removes it with all its files.
	tok := c.csrf()
	status(t, c.postForm("/library/"+g.ID+"/delete", map[string][]string{"_csrf": {tok}}, nil), http.StatusSeeOther)
	if _, err := e.svc.GetGame(bg, g.ID); err == nil {
		t.Fatal("game must be gone")
	}
	status(t, c.get("/static/libupload.js", nil), http.StatusOK)
}

func ptrInt(n int) *int { return &n }
