package web

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"net/url"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/phabioo/uzume/server/internal/hub"
)

// R-H1: the row links work without JavaScript, so the filter query must not be percent-encoded inside href.
// R-H5: the delete text of a game that only lives on apps does not claim a server file.
func TestRescanLibraryHrefsAndLocalOnlyDelete(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	users, _ := e.svc.ListUsers(bg)
	anna, _ := e.svc.CreateUser(bg, "anna", "Anna")
	dump := append(make([]byte, 0x200), "local"...)
	copy(dump[0x0C:], "LOCL")
	sum := sha256.Sum256(dump)
	local, _, err := e.svc.RegisterROM(bg, anna.ID, hub.RegisterInput{SHA256: hex.EncodeToString(sum[:]), Size: int64(len(dump)),
		Filename: "local.nds", Title: "Only Local", GameCode: "LOCL", GameVersion: ptrInt(0)})
	if err != nil {
		t.Fatal(err)
	}
	stored, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-stored-rom")), "stored.nds", "Only Stored", "", users[0].ID)

	rec := c.get("/library?q=only", nil)
	status(t, rec, 200)
	for _, id := range []string{local.ID, stored.ID} {
		for _, kind := range []string{"rename", "delete"} {
			contains(t, rec, `href="/library?`+kind+`=`+id+`&amp;q=only"`, `hx-get="/library?`+kind+`=`+id+`&amp;q=only"`)
		}
	}
	contains(t, rec, `href="/library?details=`+local.ID+`&amp;q=only"`)
	notContains(t, rec, "%26", "%3d", "%3D")

	rec = c.get("/library?delete="+local.ID, nil)
	contains(t, rec, "The game is removed from the server library. The files on the apps stay where they are.")
	notContains(t, rec, "The ROM file is removed from this server")
	rec = c.get("/library?delete="+stored.ID, nil)
	contains(t, rec, "The ROM file is removed from this server.")
	// Rename: Save comes before Cancel.
	rec = c.get("/library?rename="+stored.ID, nil)
	if s := rec.Body.String(); strings.Index(s, "Save title") > strings.Index(s, ">Cancel</a>") {
		t.Fatal("Cancel before Save title")
	}
}

// R-H6: unknown pages answer 404 inside the app shell; /api keeps its plain 404; signed-out callers learn nothing.
func TestRescanNotFoundPages(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	for _, p := range []string{"/saves/x/y/z", "/nonexistent", "/runs/nope", "/plugins/no.such/1.0.0", "/library/download/nope"} {
		if rec := c.get(p, nil); rec.Code == 404 && strings.Contains(rec.Body.String(), "Page not found") {
			t.Fatalf("%s: signed out got the shell", p)
		}
	}
	status(t, e.client().get("/nonexistent", nil), 404)
	c.login()
	for _, p := range []string{"/saves/x/y/z", "/nonexistent", "/runs/nope", "/plugins/no.such/1.0.0", "/library/download/nope"} {
		rec := c.get(p, nil)
		status(t, rec, 404)
		contains(t, rec, "Page not found", `href="/library"`, `class="sidebar"`, "Back to Library")
		if ct := rec.Header().Get("Content-Type"); !strings.HasPrefix(ct, "text/html") {
			t.Fatalf("%s: content type %q", p, ct)
		}
	}
	rec := c.get("/api/nonexistent", nil)
	status(t, rec, 404)
	notContains(t, rec, "Page not found", `class="sidebar"`)
}

// R-H17: a validation error keeps the username, never the passwords.
func TestRescanSetupKeepsUsername(t *testing.T) {
	e := newEnv(t, false, nil)
	c := e.client()
	c.get("/setup", nil)
	rec := c.postForm("/setup", url.Values{"username": {"fabio"}, "password": {"secret-12345"}, "password2": {"different-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
	status(t, rec, 400)
	contains(t, rec, "The passwords do not match.", `name="username" value="fabio"`)
	notContains(t, rec, "secret-12345", "different-1234")
}

// R-H18: the saved indicator appears after a save, not on first load.
func TestRescanSettingsSavedIndicator(t *testing.T) {
	e, _ := netEnv(t, nil)
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/general", nil)
	status(t, rec, 200)
	notContains(t, rec, "All changes saved")
	rec = c.postForm("/settings/uploads", url.Values{"enabled": {"1"}}, map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "settings-area"})
	status(t, rec, 200)
	contains(t, rec, "✓ All changes saved")
}

// R-H19 and R-H4: open invites show their creation time, redeemed and revoked ones are listed in the history,
// and the flash sits inside the main column, not over the Invites aside.
func TestRescanInviteHistoryAndFlashPlacement(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-12345")
	_, redeemCode, _ := e.svc.CreateInvite(bg, admin.ID, time.Hour, true)
	revoked, _, _ := e.svc.CreateInvite(bg, admin.ID, time.Hour, false)
	e.svc.CreateInvite(bg, admin.ID, time.Hour, false) // stays open
	if _, err := e.svc.RedeemInvite(bg, hub.RedeemInput{Code: redeemCode, DisplayName: "Lena", DeviceID: uuid.NewString(), DeviceName: "Lena PC",
		Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.3"}); err != nil {
		t.Fatal(err)
	}
	if rec := postTok(c, tok, "/users/invites/"+revoked.ID+"/revoke", nil); rec.Code != 303 {
		t.Fatalf("revoke: %d", rec.Code)
	}
	rec := c.get("/users?ok=revoked", nil)
	status(t, rec, 200)
	contains(t, rec, "Open · 1", "Created ", "Single use", "History", "Redeemed by Lena · ", "Revoked · ", "Invite revoked.")
	if n := strings.Count(rec.Body.String(), `class="invite-row"`); n != 1 {
		t.Fatalf("%d open invites, want 1", n)
	}
	s := rec.Body.String()
	flash, main, side := strings.Index(s, `class="alert ok"`), strings.Index(s, `class="users-main`), strings.Index(s, `class="users-side"`)
	if main < 0 || flash < main || flash > side {
		t.Fatalf("flash %d must sit inside users-main (%d) before the aside (%d)", flash, main, side)
	}
	if strings.Count(s, `class="alert ok"`) != 1 {
		t.Fatal("flash rendered twice")
	}
	// Other pages keep the flash above the content.
	contains(t, c.get("/library?ok=uploaded", nil), `class="alert ok"`)
}

// R-H7, R-H8, R-H9: empty states say one thing once.
func TestRescanEmptyStates(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/runs", nil)
	contains(t, rec, "No runs yet")
	notContains(t, rec, "No runs in this view", "No run selected", `aria-label="Filter"`)
	rec = c.get("/plugins", nil)
	contains(t, rec, "No plugins are offered yet.")
	notContains(t, rec, "<thead>")
	rec = c.get("/systems?sys=nds", nil)
	notContains(t, rec, `class="banner"`, `<span class="count">1</span>`, `<p class="sys-line">No core installed`)
	if n := strings.Count(rec.Body.String(), "No core installed"); n != 1 {
		t.Fatalf("%d x No core installed", n)
	}
}
