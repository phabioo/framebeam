package web

import (
	"bytes"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"testing"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// Hub web pages restyled to the v4 design: fragments of the live regions, slot tabs, restore confirmation, tabs of Systems.

func hxHdr(target string) map[string]string {
	return map[string]string{"HX-Request": "true", "HX-Target": target}
}

func TestSavesSlotsFragmentsAndRestoreConfirmation(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	e.svc.SetSaveRetention(7, 14, 6)
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", adminID)
	dev := pairTestDevice(t, e, adminID, "Desktop Living Room")
	putSave(t, e, adminID, dev, g.ID, 0, []byte("first"), hub.SyncFinalSessionEnd) // Rev 1 + v1 session end
	putSave(t, e, adminID, dev, g.ID, 1, []byte("second"), hub.SyncCheckpoint)     // Rev 2
	data := []byte("trial-run")
	if _, err := e.svc.PutSave(bg, hub.PutSaveInput{UserID: adminID, DeviceID: dev, GameID: g.ID, Slot: "trial", BaseRevision: 0,
		SHA256: sha(data), Reason: hub.SyncFinalSessionEnd, Body: bytes.NewReader(data)}); err != nil {
		t.Fatal(err)
	}
	label := "before the lighthouse"
	if _, err := e.svc.CreateSaveSnapshot(bg, adminID, g.ID, "default", &label); err != nil {
		t.Fatal(err)
	}
	base := "/saves/" + adminID + "/" + g.ID + "/default"
	trial := "/saves/" + adminID + "/" + g.ID + "/trial"

	// Full page: one list entry per game with the slot count, slot tabs, retention box with the real rules, "+ New slot" on the tabs.
	rec := c.get(base, nil)
	status(t, rec, 200)
	contains(t, rec, "Synced · today", "2 slots", "✓ Synced", `role="tablist"`, `aria-selected="true"`, "trial", "1 version", "2 versions",
		"◆ Snapshot", "“before the lighthouse”", "Retention", "newest 7", "each day for 14 days", "each week for 6 weeks",
		`hx-trigger="fb:saves from:body"`, `id="saves-list"`, `id="save-detail"`, `aria-current="page"`)
	contains(t, rec, "+ New slot", `id="newslot-form"`)
	notContains(t, rec, "▲ Conflict", "New slot for")

	// Slot switch (htmx target #save-detail): the detail region plus the list as out-of-band part.
	rec = c.get(trial, hxHdr("save-detail"))
	status(t, rec, 200)
	contains(t, rec, `id="save-detail"`, `hx-get="`+trial+`"`, `id="saves-list"`, `hx-swap-oob="true"`, "Session end", "Rev 1")
	notContains(t, rec, "<html", "<aside", "before the lighthouse")
	// The list alone.
	rec = c.get(base, hxHdr("saves-list"))
	status(t, rec, 200)
	contains(t, rec, `id="saves-list"`, "Lumen Drift")
	notContains(t, rec, "<html", `id="save-detail"`, `hx-swap-oob`)

	// History filter: Snapshots shows snapshots and the current version only.
	rec = c.get(base+"?hist=snapshots", hxHdr("save-detail"))
	contains(t, rec, "◆ Snapshot", "✓ Current", `hx-get="`+base+`?hist=snapshots"`)
	notContains(t, rec, "Session end")
	contains(t, c.get(base, nil), "Session end")

	// Restore confirmation: a fragment under the row, then the POST; Cancel answers an empty body.
	rec = c.get(base+"?confirm=1", hxHdr("confirm-v1"))
	status(t, rec, 200)
	contains(t, rec, "Restore v1 as the current version of “default”?", `hx-post="`+base+`/history/1/restore"`, `name="expected_revision" value="2"`,
		"Restore v1", "Cancel", "nothing is overwritten")
	notContains(t, rec, "<html", `id="save-detail"`)
	rec = c.get(base+"?confirm=1&cancel=1", hxHdr("confirm-v1"))
	status(t, rec, 200)
	if rec.Body.Len() != 0 {
		t.Fatalf("cancel: %q", rec.Body.String())
	}
	status(t, c.get(base+"?confirm=99", hxHdr("confirm-v99")), 404)
	// Without htmx the confirmation is part of the page.
	contains(t, c.get(base+"?confirm=1", nil), "Restore v1 as the current version of “default”?", `id="confirm-v1"`)
	notContains(t, c.get(base, nil), "as the current version of")
	// The current version offers no restore.
	cur, _ := e.svc.ListSaveHistory(bg, adminID, g.ID, "default")
	status(t, c.get(base+"?confirm="+strconv.Itoa(cur[0].Version), hxHdr("confirm-v"+strconv.Itoa(cur[0].Version))), 404)
}

func TestLibrarySavesColumnAndLiveRegion(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "harbor.nds", "Harbor Rally", "", adminID)
	e.svc.AddROM(bg, bytes.NewReader([]byte("another-dummy-rom!")), "quiet.nds", "Quiet Game", "", adminID)
	devA, devB := pairTestDevice(t, e, adminID, "Laptop"), pairTestDevice(t, e, adminID, "Desktop")
	putSave(t, e, adminID, devA, g.ID, 0, []byte("old"), hub.SyncCheckpoint)
	putSave(t, e, adminID, devA, g.ID, 1, []byte("hub"), hub.SyncCheckpoint)
	if r := putSave(t, e, adminID, devB, g.ID, 1, []byte("local"), hub.SyncCheckpoint); r.Conflict == nil {
		t.Fatal("expected a conflict")
	}
	rec := c.get("/library", nil)
	contains(t, rec, "Saves", "▲ Conflict", `hx-trigger="fb:library from:body"`, `id="library-results"`, "All systems · 2", `hx-swap="outerHTML"`)
	// The region reloads itself with the current filter and returns only the region (plus the subtitle).
	rec = c.get("/library?q=harbor&system=nds", hxHdr("library-results"))
	status(t, rec, 200)
	contains(t, rec, "Harbor Rally", `hx-get="/library?q=harbor&amp;system=nds"`, `id="library-sub"`)
	notContains(t, rec, "Quiet Game", "<html", "<aside")
	// Only the game with the conflict is marked.
	if n := bytes.Count(c.get("/library", nil).Body.Bytes(), []byte("▲ Conflict")); n != 1 {
		t.Fatalf("%d conflict marks", n)
	}
}

func TestClientsAndUsersLiveRegions(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	pending(t, e, uuid.NewString())
	rec := c.get("/clients", hxHdr("clients-body"))
	status(t, rec, 200)
	contains(t, rec, `id="clients-body"`, `hx-get="/clients"`, `hx-trigger="fb:clients from:body"`, "Decline", "▲ Awaiting approval", "Lena Gaming PC")
	notContains(t, rec, "<html", "<aside", "every 15s")
	// A sidebar navigation (target #main) returns the page, not the fragment.
	contains(t, c.get("/clients", map[string]string{"HX-Request": "true", "HX-Target": "main"}), "Devices allowed to access this hub")

	rec = c.get("/users", hxHdr("users-table"))
	status(t, rec, 200)
	contains(t, rec, `id="users-table"`, `hx-trigger="fb:users from:body"`, "admin", "Disabled users cannot sign in")
	notContains(t, rec, "<html", "Onboarding invites", `id="users-invites"`)
	postTok(c, c.csrf(), "/users/invites", url.Values{"expiry": {"1h"}, "authorize": {"1"}})
	rec = c.get("/users", hxHdr("users-invites"))
	status(t, rec, 200)
	contains(t, rec, `id="users-invites"`, `hx-trigger="fb:users from:body"`, "Active", "Revoke")
	notContains(t, rec, "<html", `id="users-table"`, "Create invite")
}

func TestSystemsTabsAndListFragments(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	// Tab switch: the detail region plus the list out-of-band.
	rec := c.get("/systems?sys=nds&tab=clients", hxHdr("systems-detail"))
	status(t, rec, 200)
	contains(t, rec, `id="systems-detail"`, `hx-trigger="fb:systems from:body"`, `hx-get="/systems?sys=nds&amp;tab=clients"`, "Reported by Players",
		`id="systems-list"`, `hx-swap-oob="true"`, `aria-current="true"`)
	notContains(t, rec, "<html", "<aside", "Core package cache")
	rec = c.get("/systems?sys=nds&tab=core", hxHdr("systems-detail"))
	contains(t, rec, "Core package cache", "Display profile", "Input profile")
	// Search: only the list region; the selection stays the one of the page URL.
	rec = c.get("/systems?q=zzz", map[string]string{"HX-Request": "true", "HX-Target": "systems-list", "HX-Current-URL": "http://hub/systems?sys=nds&tab=core"})
	status(t, rec, 200)
	contains(t, rec, `id="systems-list"`, "No systems found.", `hx-get="/systems?q=zzz&amp;sys=nds&amp;tab=core"`)
	notContains(t, rec, "<html", `id="systems-detail"`)
	contains(t, c.get("/systems?q=nintendo", hxHdr("systems-list")), "Nintendo DS")
	// Core differences only warn (ADR 0007 D3): the summary of a system with a differing client is warn colored and allows launching.
	users, _ := e.svc.ListUsers(bg)
	dev := pairTestDevice(t, e, users[0].ID, "Lena PC")
	cores := []hub.CoreReport{{ID: "melonds_ds", Version: "1.3.0"}}
	if _, err := e.svc.Handshake(bg, dev, hub.HandshakeInput{Platform: "windows", Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1,
		MinProtocolVersion: 1, Cores: &cores}); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/systems", nil)
	contains(t, rec, `class="pill warn lg"`, "▲ 1 client differ from the registry · launching stays allowed", `class="pill warn sm">1 client`)
	notContains(t, rec, `pill error`)
}

func TestSavesDeleteSnapshotAndNewSlotWeb(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	g, _ := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", adminID)
	dev := pairTestDevice(t, e, adminID, "Desktop Living Room")
	putSave(t, e, adminID, dev, g.ID, 0, []byte("first"), hub.SyncFinalSessionEnd) // Rev 1 + v1 session end
	label := "before the lighthouse"
	snap, err := e.svc.CreateSaveSnapshot(bg, adminID, g.ID, "default", &label) // v2
	if err != nil {
		t.Fatal(err)
	}
	bare, err := e.svc.CreateSaveSnapshot(bg, adminID, g.ID, "default", nil) // v3, no label
	if err != nil {
		t.Fatal(err)
	}
	base := "/saves/" + adminID + "/" + g.ID + "/default"
	del := func(v int) string { return base + "/history/" + strconv.Itoa(v) + "/delete" }

	// Delete is offered on snapshots only.
	rec := c.get(base, nil)
	if n := strings.Count(rec.Body.String(), ">Delete</a>"); n != 2 {
		t.Fatalf("%d Delete actions", n)
	}
	// Inline confirmation: fragment under the row, with and without label; Cancel answers an empty body.
	rec = c.get(base+"?delete="+strconv.Itoa(snap.Version), hxHdr("confirm-v"+strconv.Itoa(snap.Version)))
	status(t, rec, 200)
	contains(t, rec, "Delete snapshot v"+strconv.Itoa(snap.Version)+" “before the lighthouse”?", "The snapshot is removed for good. The current version and other versions are not affected.",
		`hx-post="`+del(snap.Version)+`"`, "Cancel", "Delete v"+strconv.Itoa(snap.Version), "btn small danger")
	notContains(t, rec, "<html")
	rec = c.get(base+"?delete="+strconv.Itoa(bare.Version), hxHdr("confirm-v"+strconv.Itoa(bare.Version)))
	contains(t, rec, "Delete snapshot v"+strconv.Itoa(bare.Version)+"?")
	notContains(t, rec, "“")
	rec = c.get(base+"?delete=1&cancel=1", hxHdr("confirm-v1"))
	status(t, rec, 200)
	if strings.TrimSpace(rec.Body.String()) != "" {
		t.Fatalf("cancel body %q", rec.Body.String())
	}
	status(t, c.get(base+"?delete=1", hxHdr("confirm-v1")), 404) // v1 is not a snapshot
	contains(t, c.get(base+"?delete="+strconv.Itoa(snap.Version), nil), "Delete snapshot v"+strconv.Itoa(snap.Version), `id="confirm-v`+strconv.Itoa(snap.Version)+`"`)

	// POST: CSRF, not a snapshot, unknown version, then ok.
	status(t, c.postForm(del(snap.Version), url.Values{}, nil), http.StatusForbidden)
	status(t, c.postForm(del(1), url.Values{"_csrf": {tok}}, nil), http.StatusConflict)
	status(t, c.postForm(del(99), url.Values{"_csrf": {tok}}, nil), http.StatusNotFound)
	status(t, c.postForm(base+"/history/x/delete", url.Values{"_csrf": {tok}}, nil), http.StatusNotFound)
	rec = c.postForm(del(snap.Version), url.Values{"_csrf": {tok}}, map[string]string{"HX-Request": "true"})
	status(t, rec, 200)
	if rec.Header().Get("HX-Redirect") != base+"?ok=snapdeleted" {
		t.Fatalf("HX-Redirect %q", rec.Header().Get("HX-Redirect"))
	}
	rec = c.get(base+"?ok=snapdeleted", nil)
	contains(t, rec, "Snapshot deleted.")
	notContains(t, rec, "before the lighthouse")

	// New slot: the form as a fragment (and inline on the full page), Cancel answers an empty body.
	rec = c.get(base+"?newslot=1", hxHdr("newslot-form"))
	status(t, rec, 200)
	contains(t, rec, "New slot for “Lumen Drift”", "Starts from the current version of “default”. Players can pick the slot in the game details.",
		`name="name"`, `placeholder="Speedrun"`, "up to 32 characters", `hx-post="`+base+`/slots"`, "Cancel", "Create slot")
	notContains(t, rec, "<html")
	contains(t, c.get(base+"?newslot=1", nil), "New slot for “Lumen Drift”", `id="newslot-form"`)
	rec = c.get(base+"?newslot=1&cancel=1", hxHdr("newslot-form"))
	if strings.TrimSpace(rec.Body.String()) != "" {
		t.Fatalf("cancel body %q", rec.Body.String())
	}

	// POST: CSRF, invalid and existing names re-render the form with an error, unknown source slot is 404, then ok.
	slots := base + "/slots"
	status(t, c.postForm(slots, url.Values{"name": {"speedrun"}}, nil), http.StatusForbidden)
	rec = c.postForm(slots, url.Values{"name": {"Speed Run"}, "_csrf": {tok}}, map[string]string{"HX-Request": "true", "HX-Target": "newslot-form"})
	status(t, rec, 200)
	contains(t, rec, "Use lowercase letters, digits, - and _ (up to 32 characters).", `value="Speed Run"`)
	notContains(t, rec, "<html")
	rec = c.postForm(slots, url.Values{"name": {"default"}, "_csrf": {tok}}, nil)
	status(t, rec, 200)
	contains(t, rec, "A slot with this name already exists.", "<html", `value="default"`)
	status(t, c.postForm("/saves/"+adminID+"/"+g.ID+"/nope/slots", url.Values{"name": {"x"}, "_csrf": {tok}}, nil), http.StatusNotFound)
	rec = c.postForm(slots, url.Values{"name": {"speedrun"}, "_csrf": {tok}}, map[string]string{"HX-Request": "true"})
	status(t, rec, 200)
	next := "/saves/" + adminID + "/" + g.ID + "/speedrun"
	if rec.Header().Get("HX-Redirect") != next+"?ok=slotcreated" {
		t.Fatalf("HX-Redirect %q", rec.Header().Get("HX-Redirect"))
	}
	rec = c.get(next+"?ok=slotcreated", nil)
	contains(t, rec, "Slot created from the current version.", "Rev 1", "Hub web interface", "From “default”", "◆ Snapshot", "2 slots")
	// A second creation without htmx redirects (303) to the new tab.
	rec = c.postForm(slots, url.Values{"name": {"second"}, "_csrf": {tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if location(rec) != "/saves/"+adminID+"/"+g.ID+"/second?ok=slotcreated" {
		t.Fatalf("location %q", location(rec))
	}
}
