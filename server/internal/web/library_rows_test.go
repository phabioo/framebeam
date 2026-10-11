package web

import (
	"bytes"
	"net/url"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

func TestLibraryRenameRow(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", users[0].ID)
	hx := map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "library-results"}

	// Row actions and the inline form (fragment and full page).
	contains(t, c.get("/library", nil), `>Rename</a>`, `>Delete</a>`)
	rec := c.get("/library?rename="+g.ID+"&q=lumen", map[string]string{"HX-Request": "true", "HX-Target": "library-results"})
	status(t, rec, 200)
	contains(t, rec, `action="/library/`+g.ID+`/rename"`, `name="title" value="Lumen Drift"`, `name="q" value="lumen"`, "Save title", `name="_csrf"`)
	notContains(t, rec, "<html")
	contains(t, c.get("/library?rename="+g.ID, nil), "Save title") // no-JS: full page

	// Success: the title changes, the panel closes, the hash stays.
	rec = c.postForm("/library/"+g.ID+"/rename", url.Values{"title": {"  Lumen Drift – Remaster\n"}, "q": {""}, "system": {""}}, hx)
	status(t, rec, 200)
	contains(t, rec, "Lumen Drift – Remaster")
	notContains(t, rec, "Save title", "<html")
	got, _ := e.svc.GetGame(bg, g.ID)
	if got.Title != "Lumen Drift – Remaster" || got.ROMSHA256 != g.ROMSHA256 {
		t.Fatalf("%+v", got)
	}

	// Refusals keep the form open with the reason and change nothing.
	rec = c.postForm("/library/"+g.ID+"/rename", url.Values{"title": {" \t "}}, hx)
	status(t, rec, 400)
	contains(t, rec, "must not be empty", "Save title")
	rec = c.postForm("/library/"+g.ID+"/rename", url.Values{"title": {"bad\xff\xfe"}}, hx)
	status(t, rec, 400)
	contains(t, rec, "valid UTF-8")
	if got, _ := e.svc.GetGame(bg, g.ID); got.Title != "Lumen Drift – Remaster" {
		t.Fatal("title changed by a refused rename")
	}

	// Overlong titles are cut like on upload; without htmx the answer is a redirect.
	rec = c.postForm("/library/"+g.ID+"/rename", url.Values{"title": {strings.Repeat("é", 300)}, "_csrf": {tok}}, nil)
	status(t, rec, 303)
	if got, _ := e.svc.GetGame(bg, g.ID); len(got.Title) > hub.MaxGameTitle || got.Title == "" {
		t.Fatalf("len %d", len(got.Title))
	}

	// CSRF is required; an unknown id is not an error page.
	status(t, c.postForm("/library/"+g.ID+"/rename", url.Values{"title": {"X"}}, nil), 403)
	status(t, c.postForm("/library/nope/rename", url.Values{"title": {"X"}, "_csrf": {tok}}, nil), 303)
}

func TestLibraryDeleteRowKeepsSaves(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", adminID)
	dev := pairTestDevice(t, e, adminID, "Desktop")

	// Without saves.
	rec := c.get("/library?delete="+g.ID, map[string]string{"HX-Request": "true", "HX-Target": "library-results"})
	status(t, rec, 200)
	contains(t, rec, "Delete “Lumen Drift” from the library?", "There are no saves", "Delete ROM", `action="/library/`+g.ID+`/delete"`)
	notContains(t, rec, "hx-confirm")

	// With saves: the text says they are kept, and they are.
	putSave(t, e, adminID, dev, g.ID, 0, []byte("first"), hub.SyncFinalSessionEnd)
	rec = c.get("/library?delete="+g.ID, nil)
	contains(t, rec, "1 save slot is kept")
	rec = c.postForm("/library/"+g.ID+"/delete", url.Values{"q": {""}, "system": {""}},
		map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "library-results"})
	status(t, rec, 200)
	contains(t, rec, "No ROMs in the library yet")
	if _, err := e.svc.GetGame(bg, g.ID); err == nil {
		t.Fatal("game not deleted")
	}
	slots, _ := e.svc.ListSaveSlots(bg, adminID)
	if len(slots) != 1 || slots[0].GameID != g.ID {
		t.Fatalf("saves after delete: %+v", slots)
	}
}

func TestLibraryRowActionsNeedAdmin(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client() // not signed in
	c.get("/login", nil)
	status(t, c.postForm("/library/x/rename", url.Values{"title": {"X"}}, nil), 303)
	status(t, c.get("/library?rename=x", nil), 303)
}
