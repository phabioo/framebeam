package web

import (
	"bytes"
	"net/http"
	"net/url"
	"strconv"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

func TestSaveUploadWeb(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", adminID)
	dev := pairTestDevice(t, e, adminID, "Desktop")
	putSave(t, e, adminID, dev, g.ID, 0, []byte("first"), hub.SyncCheckpoint) // Rev 1
	base := "/saves/" + adminID + "/" + g.ID + "/default"

	// Slot detail: the form is there.
	rec := c.get(base, nil)
	contains(t, rec, `action="`+base+`/upload"`, `enctype="multipart/form-data"`, `name="expected_revision" value="1"`,
		"Upload save file", "The current version moves to the save history.")

	// CSRF, stale revision, empty file, missing file, bad revision, then ok.
	status(t, c.multipartPost(base+"/upload", map[string]string{"expected_revision": "1"}, "a.sav", []byte("x")), http.StatusForbidden)
	rec = c.multipartPost(base+"/upload", map[string]string{"_csrf": tok, "expected_revision": "7"}, "a.sav", []byte("x"))
	status(t, rec, http.StatusSeeOther)
	if location(rec) != base+"?err=stale" {
		t.Fatalf("location %q", location(rec))
	}
	contains(t, c.get(base+"?err=stale", nil), "The slot changed in the meantime")
	rec = c.multipartPost(base+"/upload", map[string]string{"_csrf": tok, "expected_revision": "1"}, "a.sav", nil)
	if location(rec) != base+"?err=emptyfile" {
		t.Fatalf("empty: %d %q", rec.Code, location(rec))
	}
	rec = c.multipartPost(base+"/upload", map[string]string{"_csrf": tok, "expected_revision": "1"}, "", nil)
	if location(rec) != base+"?err=emptyfile" {
		t.Fatalf("no file: %d %q", rec.Code, location(rec))
	}
	status(t, c.multipartPost(base+"/upload", map[string]string{"_csrf": tok, "expected_revision": "x"}, "a.sav", []byte("x")), http.StatusBadRequest)
	big := bytes.Repeat([]byte{1}, hub.MaxSaveBytes+1)
	rec = c.multipartPost(base+"/upload", map[string]string{"_csrf": tok, "expected_revision": "1"}, "big.sav", big)
	if location(rec) != base+"?err=toolarge" {
		t.Fatalf("too large: %d %q", rec.Code, location(rec))
	}
	contains(t, c.get(base+"?err=toolarge", nil), "The save file is too large (maximum 64 MiB).")

	rec = c.multipartPost(base+"/upload", map[string]string{"_csrf": tok, "expected_revision": "1"}, "other.sav", []byte("from another emulator"))
	status(t, rec, http.StatusSeeOther)
	if location(rec) != base+"?ok=uploaded" {
		t.Fatalf("location %q", location(rec))
	}
	rec = c.get(base+"?ok=uploaded", nil)
	contains(t, rec, "Save file uploaded. The previous version is in the history.", "Rev 2", "Uploaded file", "Hub web interface", "Before upload")
	notContains(t, rec, "ROM added")
	sl, err := e.svc.GetSaveSlot(bg, adminID, g.ID, "default")
	if err != nil || sl.Current.Revision != 2 || sl.Current.DeviceID != hub.WebDeviceID(adminID) {
		t.Fatalf("%+v %v", sl.Current, err)
	}
}

func TestSaveUploadListWeb(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", adminID)

	// Library: per-game link; Saves page: collapsible form with the game preselected.
	contains(t, c.get("/library", nil), `href="/saves?upload=`+g.ID+`"`, "Upload save")
	rec := c.get("/saves?upload="+g.ID, nil)
	contains(t, rec, `action="/saves/upload"`, `enctype="multipart/form-data"`, `<details class="upload upload-inline" open>`,
		`<option value="`+g.ID+`" selected>Lumen Drift</option>`, `name="slot" value="default"`)
	notContains(t, c.get("/saves", nil), ` open>`)

	form := func(extra map[string]string) map[string]string {
		f := map[string]string{"_csrf": tok, "user": adminID, "game": g.ID, "slot": "default"}
		for k, v := range extra {
			f[k] = v
		}
		return f
	}
	status(t, c.multipartPost("/saves/upload", map[string]string{"user": adminID, "game": g.ID}, "a.sav", []byte("x")), http.StatusForbidden)
	for _, tc := range []struct {
		extra map[string]string
		name  string
		data  []byte
		key   string
	}{
		{map[string]string{"slot": "Bad Slot"}, "a.sav", []byte("x"), "slotname"},
		{map[string]string{"game": "nope"}, "a.sav", []byte("x"), "nogame"},
		{map[string]string{"user": "nope"}, "a.sav", []byte("x"), "nouser"},
		{nil, "a.sav", nil, "emptyfile"},
	} {
		rec = c.multipartPost("/saves/upload", form(tc.extra), tc.name, tc.data)
		status(t, rec, http.StatusSeeOther)
		if l, _ := url.Parse(location(rec)); l.Path != "/saves" || l.Query().Get("err") != tc.key {
			t.Fatalf("%s: location %q", tc.key, location(rec))
		}
	}

	// New slot (default name when empty), then replace: the old version stays in the history.
	rec = c.multipartPost("/saves/upload", form(map[string]string{"slot": ""}), "a.sav", []byte("one"))
	status(t, rec, http.StatusSeeOther)
	slot := "/saves/" + adminID + "/" + g.ID + "/default"
	if location(rec) != slot+"?ok=uploaded" {
		t.Fatalf("location %q", location(rec))
	}
	sl, err := e.svc.GetSaveSlot(bg, adminID, g.ID, "default")
	if err != nil || sl.Current.Revision != 1 || sl.Current.Reason != hub.SyncUpload {
		t.Fatalf("%+v %v", sl.Current, err)
	}
	rec = c.multipartPost("/saves/upload", form(nil), "b.sav", []byte("two"))
	status(t, rec, http.StatusSeeOther)
	rec = c.multipartPost("/saves/upload", form(map[string]string{"slot": "second"}), "c.sav", []byte("three"))
	if location(rec) != "/saves/"+adminID+"/"+g.ID+"/second?ok=uploaded" {
		t.Fatalf("location %q", location(rec))
	}
	sl, _ = e.svc.GetSaveSlot(bg, adminID, g.ID, "default")
	h, _ := e.svc.ListSaveHistory(bg, adminID, g.ID, "default")
	if sl.Current.Revision != 2 || len(h) != 1 || h[0].Reason != hub.HistoryBeforeUpload {
		t.Fatalf("%+v %+v", sl.Current, h)
	}
	contains(t, c.get(slot+"?ok=uploaded", nil), "Save file uploaded.", "v"+strconv.Itoa(h[0].Version))
}
