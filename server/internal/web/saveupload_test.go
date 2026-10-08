package web

import (
	"bytes"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type uploadFixture struct {
	e       *env
	c       *client
	tok     string
	adminID string
	game    string
	base    string
}

func newUploadFixture(t *testing.T) uploadFixture {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", adminID)
	dev := pairTestDevice(t, e, adminID, "Desktop")
	putSave(t, e, adminID, dev, g.ID, 0, []byte("first"), hub.SyncCheckpoint) // Rev 1
	return uploadFixture{e, c, tok, adminID, g.ID, "/saves/" + adminID + "/" + g.ID + "/default"}
}

func (f uploadFixture) fields(extra map[string]string) map[string]string {
	m := map[string]string{"_csrf": f.tok, "slot": "default", "rev.default": "1"}
	for k, v := range extra {
		m[k] = v
	}
	return m
}

var hx = map[string]string{"HX-Request": "true", "HX-Target": "save-detail"}

func TestSaveUploadPanelWeb(t *testing.T) {
	f := newUploadFixture(t)

	// Closed: the CURRENT bar has the Download link and one "Upload save" button, no panel, no standalone form.
	rec := f.c.get(f.base, nil)
	contains(t, rec, "CURRENT", `href="`+f.base+`/download"`, `href="`+f.base+`?upload=1"`, "Upload save ▾", `<span class="mono">Rev 1</span>`)
	notContains(t, rec, `class="upl-panel"`, `action="/saves/upload"`, "Upload save file", `class="upload upload-inline"`)
	if strings.Count(rec.Body.String(), "Upload save") != 1 {
		t.Fatalf("Upload save appears %d times", strings.Count(rec.Body.String(), "Upload save"))
	}

	// Open: the panel with drop zone (file input), own slot select, note and both buttons; it keeps the htmx refresh off.
	rec = f.c.get(f.base+"?upload=1", nil)
	contains(t, rec, `class="cur-card open"`, "Upload save ▴", `class="upl-panel"`, `action="`+f.base+`/upload"`, `enctype="multipart/form-data"`,
		`hx-encoding="multipart/form-data"`, `type="file" name="file" required`, `name="rev.default" value="1"`,
		`<option value="default" selected>default</option>`, "Into slot",
		"The current version Rev 1 stays in the history as “Before upload”.", ">Cancel</a>", "Upload as new current version")
	notContains(t, rec, `class="save-detail" hx-get=`)
	// htmx: the fragment alone.
	rec = f.c.get(f.base+"?upload=1", hx)
	contains(t, rec, `id="save-detail"`, `class="upl-panel"`)
	notContains(t, rec, "<!doctype")
}

func TestSaveUploadResultsWeb(t *testing.T) {
	f := newUploadFixture(t)
	post := func(fields map[string]string, name string, data []byte, hdr map[string]string) *httptest.ResponseRecorder {
		return f.c.multipartPostH(f.base+"/upload", fields, name, data, hdr)
	}

	// Guards: CSRF, bad revision, unknown slot.
	status(t, f.c.multipartPost(f.base+"/upload", map[string]string{"rev.default": "1"}, "a.sav", []byte("x")), http.StatusForbidden)
	status(t, post(f.fields(map[string]string{"rev.default": "x"}), "a.sav", []byte("x"), nil), http.StatusBadRequest)
	status(t, post(f.fields(map[string]string{"slot": "nope"}), "a.sav", []byte("x"), nil), http.StatusBadRequest)

	// Errors without htmx redirect to the slot with the result key; the page shows it inline in the open panel.
	for _, tc := range []struct {
		fields map[string]string
		name   string
		data   []byte
		key    string
		text   string
	}{
		{map[string]string{"rev.default": "7"}, "a.sav", []byte("x"), "stale", "The slot changed in the meantime."},
		{nil, "a.sav", nil, "emptyfile", "Please select a save file that is not empty."},
		{nil, "", nil, "emptyfile", "Please select a save file that is not empty."},
		{nil, "big.sav", bytes.Repeat([]byte{1}, hub.MaxSaveBytes+1), "toolarge", "The save file is too large (maximum 64 MiB)."},
	} {
		rec := post(f.fields(tc.fields), tc.name, tc.data, nil)
		status(t, rec, http.StatusSeeOther)
		if location(rec) != f.base+"?up="+tc.key {
			t.Fatalf("%s: location %q", tc.key, location(rec))
		}
		contains(t, f.c.get(location(rec), nil), `class="upl-panel"`, `role="alert"`, tc.text)
	}

	// Success with htmx: inline result and undo in the returned detail, no redirect; the URL is pushed.
	rec := post(f.fields(nil), "other.sav", []byte("from another emulator"), hx)
	status(t, rec, http.StatusOK)
	contains(t, rec, `id="save-detail"`, "✓ Uploaded as Rev 2.", "Rev 1 is in the history as “Before upload”. Players get Rev 2 on their next start.",
		"Undo: restore v", `<span class="mono">Rev 2</span>`, "Uploaded", "Hub web interface · admin", "Before upload", "Upload save ▾", `hx-swap-oob="true"`)
	notContains(t, rec, `class="upl-panel"`, "<!doctype")
	if rec.Header().Get("HX-Push-Url") != f.base+"?up=ok" {
		t.Fatalf("push url %q", rec.Header().Get("HX-Push-Url"))
	}
	sl, err := f.e.svc.GetSaveSlot(bg, f.adminID, f.game, "default")
	if err != nil || sl.Current.Revision != 2 || sl.Current.Reason != hub.SyncUpload || sl.Current.DeviceID != hub.WebDeviceID(f.adminID) {
		t.Fatalf("%+v %v", sl.Current, err)
	}
	// The result survives the page refresh (the pushed URL), and the undo uses the existing restore.
	rec = f.c.get(f.base+"?up=ok", nil)
	contains(t, rec, "✓ Uploaded as Rev 2.", `hx-post="`+f.base+`/history/`)
	hist, _ := f.e.svc.ListSaveHistory(bg, f.adminID, f.game, "default")
	var before int
	for _, v := range hist {
		if v.Reason == hub.HistoryBeforeUpload {
			before = v.Version
		}
	}
	if before == 0 {
		t.Fatalf("no before_upload version: %+v", hist)
	}
	status(t, f.c.postForm(f.base+"/history/"+strconv.Itoa(before)+"/restore", url.Values{"_csrf": {f.tok}, "expected_revision": {"2"}}, nil), http.StatusSeeOther)
	sl, _ = f.e.svc.GetSaveSlot(bg, f.adminID, f.game, "default")
	if sl.Current.Revision != 3 || sl.Current.Reason != hub.SyncRestore {
		t.Fatalf("undo: %+v", sl.Current)
	}
	// "ok" is not shown when the current version is no upload.
	notContains(t, f.c.get(f.base+"?up=ok", nil), "✓ Uploaded as")

	// Identical file: no new version, the notice replaces the panel body.
	rec = post(f.fields(map[string]string{"rev.default": "3"}), "same.sav", []byte("first"), hx)
	status(t, rec, http.StatusOK)
	contains(t, rec, "This file matches the current version. Nothing changed.", "same.sav has the same hash as Rev 3", "No new version was created.", ">Close</a>")
	notContains(t, rec, `type="file"`)
	sl, _ = f.e.svc.GetSaveSlot(bg, f.adminID, f.game, "default")
	if sl.Current.Revision != 3 {
		t.Fatalf("no-op changed the slot: %+v", sl.Current)
	}

	// Error with htmx: inline in the open panel.
	rec = post(f.fields(map[string]string{"rev.default": "9"}), "a.sav", []byte("x"), hx)
	contains(t, rec, `class="upl-panel"`, "✕ The slot changed in the meantime.")
}

func TestSaveUploadOtherSlotAndNewSlotWeb(t *testing.T) {
	f := newUploadFixture(t)
	_, err := f.e.svc.CreateSaveSlot(bg, f.adminID, f.game, "default", "speedrun", hub.WebDeviceID(f.adminID))
	if err != nil {
		t.Fatal(err)
	}
	rec := f.c.get(f.base+"?upload=1", nil)
	contains(t, rec, `<option value="default" selected>default</option>`, `<option value="speedrun">speedrun</option>`, `name="rev.speedrun" value="1"`)

	// Into another slot (rev of that slot): the response shows that slot.
	sp := "/saves/" + f.adminID + "/" + f.game + "/speedrun"
	rec = f.c.multipartPostH(f.base+"/upload", f.fields(map[string]string{"slot": "speedrun", "rev.speedrun": "1"}), "x.sav", []byte("speed"), hx)
	status(t, rec, http.StatusOK)
	contains(t, rec, "✓ Uploaded as Rev 2.")
	if rec.Header().Get("HX-Push-Url") != sp+"?up=ok" {
		t.Fatalf("push url %q", rec.Header().Get("HX-Push-Url"))
	}

	// A game without a save: the Library link opens the panel for a new slot "default".
	g2, _ := f.e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom-2")), "two.nds", "Two", "", f.adminID)
	lib := f.c.get("/library", nil)
	contains(t, lib, `href="`+f.base+`?upload=1"`, `href="/saves/`+f.adminID+`/`+g2.ID+`/default?upload=1"`, "Upload save")
	notContains(t, lib, `href="/saves?upload=`)
	np := "/saves/" + f.adminID + "/" + g2.ID + "/default"
	rec = f.c.get(np+"?upload=1", nil)
	contains(t, rec, "Two", `class="upl-panel"`, `name="rev.default" value="0"`, "Upload as new current version")
	notContains(t, rec, "CURRENT")
	status(t, f.c.get(np, nil), http.StatusNotFound)
	rec = f.c.multipartPostH(np+"/upload", map[string]string{"_csrf": f.tok, "slot": "default", "rev.default": "0"}, "n.sav", []byte("brand new"), hx)
	contains(t, rec, "✓ Uploaded as Rev 1.", "Two")
}

func TestSaveUploadAdminOtherUserWeb(t *testing.T) {
	f := newUploadFixture(t)
	other, _ := f.e.svc.CreateUser(bg, "max", "Max")
	dev := pairTestDevice(t, f.e, other.ID, "Laptop")
	putSave(t, f.e, other.ID, dev, f.game, 0, []byte("max save"), hub.SyncCheckpoint)
	op := "/saves/" + other.ID + "/" + f.game + "/default"

	rec := f.c.get(op, nil)
	contains(t, rec, "CURRENT", "Download")
	notContains(t, rec, "Upload save", `class="upl-panel"`)
	notContains(t, f.c.get(op+"?upload=1", nil), `class="upl-panel"`, "Upload as new current version")
	status(t, f.c.multipartPost(op+"/upload", map[string]string{"_csrf": f.tok, "slot": "default", "rev.default": "1"}, "a.sav", []byte("x")), http.StatusForbidden)
	sl, _ := f.e.svc.GetSaveSlot(bg, other.ID, f.game, "default")
	if sl.Current.Revision != 1 {
		t.Fatalf("admin changed another user's slot: %+v", sl.Current)
	}
}

func TestSavesPageHasNoStandaloneUploadWeb(t *testing.T) {
	f := newUploadFixture(t)
	rec := f.c.get("/saves", nil)
	notContains(t, rec, `action="/saves/upload"`, "upload-inline")
	status(t, f.c.multipartPost("/saves/upload", map[string]string{"_csrf": f.tok}, "a.sav", []byte("x")), http.StatusNotFound)
}
