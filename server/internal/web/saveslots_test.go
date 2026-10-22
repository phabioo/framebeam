package web

import (
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

// slotFixture is the upload fixture plus a second slot "run" created from the default one.
func newSlotFixture(t *testing.T) (uploadFixture, string) {
	f := newUploadFixture(t)
	if _, err := f.e.svc.CreateSaveSlot(bg, f.adminID, f.game, "default", "run", hub.WebDeviceID(f.adminID)); err != nil {
		t.Fatal(err)
	}
	return f, "/saves/" + f.adminID + "/" + f.game + "/run"
}

func TestSaveSlotRenameWeb(t *testing.T) {
	f, run := newSlotFixture(t)

	// The main slot offers only delete (with its own confirmation text); another slot offers both.
	rec := f.c.get(f.base, nil)
	contains(t, rec, `class="slot-ops"`, "Delete slot")
	notContains(t, rec, "Rename slot")
	rec = f.c.get(f.base+"?delslot=1", nil)
	contains(t, rec, "Delete the main slot?", "main slot with all its history is deleted for good", "starts it fresh on the next play")
	rec = f.c.get(run, nil)
	contains(t, rec, `class="slot-ops"`, `href="`+run+`?rename=1"`, "Rename slot", `href="`+run+`?delslot=1"`, "Delete slot")
	notContains(t, rec, `action="`+run+`/rename"`)

	// Open form (no htmx refresh while it is open) and htmx fragment.
	rec = f.c.get(run+"?rename=1", nil)
	contains(t, rec, `action="`+run+`/rename"`, `name="name"`, "Lowercase letters, digits, - and _ (up to 32 characters)", "Checkpoint, history and conflicts move with the slot")
	notContains(t, rec, `class="save-detail" hx-get=`)
	rec = f.c.get(run+"?rename=1", hx)
	contains(t, rec, `id="save-detail"`, `action="`+run+`/rename"`)
	notContains(t, rec, "<!doctype")

	// POST: CSRF, invalid / taken names go back to the form with the error and the typed value, unknown slot is 404.
	status(t, f.c.postForm(run+"/rename", url.Values{"name": {"speedrun"}}, nil), http.StatusForbidden)
	rec = f.c.postForm(run+"/rename", url.Values{"name": {"Speed Run"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if loc := location(rec); !strings.HasPrefix(loc, run+"?err=slotname&rename=1&name=") {
		t.Fatalf("location %q", loc)
	}
	contains(t, f.c.get(location(rec), nil), "Use lowercase letters, digits, - and _", `value="Speed Run"`)
	rec = f.c.postForm(run+"/rename", url.Values{"name": {"default"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	contains(t, f.c.get(location(rec), nil), "A slot with this name already exists.")
	rec = f.c.postForm(run+"/rename", url.Values{"name": {"nul"}, "_csrf": {f.tok}}, nil)
	contains(t, f.c.get(location(rec), nil), "Use lowercase letters")
	status(t, f.c.postForm("/saves/"+f.adminID+"/"+f.game+"/nope/rename", url.Values{"name": {"x"}, "_csrf": {f.tok}}, nil), http.StatusNotFound)
	rec = f.c.postForm(f.base+"/rename", url.Values{"name": {"main"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	contains(t, f.c.get(location(rec), nil), "The default slot cannot be renamed.")

	// OK: redirect to the renamed slot with the flash; history moved along.
	rec = f.c.postForm(run+"/rename", url.Values{"name": {" speedrun "}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	next := "/saves/" + f.adminID + "/" + f.game + "/speedrun"
	if location(rec) != next+"?ok=slotrenamed" {
		t.Fatalf("location %q", location(rec))
	}
	rec = f.c.get(next+"?ok=slotrenamed", nil)
	contains(t, rec, "Slot renamed.", "speedrun", "From “default”", "◆ Snapshot")
	status(t, f.c.get(run, nil), http.StatusNotFound)
}

func TestSaveSlotDeleteWeb(t *testing.T) {
	f, run := newSlotFixture(t)

	// The inline confirmation names the latest checkpoint and what goes with it.
	rec := f.c.get(run+"?delslot=1", nil)
	contains(t, rec, "Delete this slot?", "The latest checkpoint is Rev 1 from just now", "1 history version", `action="`+run+`/delete"`,
		`name="expected_revision" value="1"`, "Delete slot and history", "cannot be undone")
	notContains(t, rec, `class="save-detail" hx-get=`)

	// CSRF, bad body, unknown slot.
	status(t, f.c.postForm(run+"/delete", url.Values{"expected_revision": {"1"}}, nil), http.StatusForbidden)
	status(t, f.c.postForm(run+"/delete", url.Values{"expected_revision": {"x"}, "_csrf": {f.tok}}, nil), http.StatusBadRequest)
	status(t, f.c.postForm("/saves/"+f.adminID+"/"+f.game+"/nope/delete", url.Values{"expected_revision": {"1"}, "_csrf": {f.tok}}, nil), http.StatusNotFound)
	// Stale confirmation: the slot changed after the page was shown; nothing is deleted.
	if _, err := f.e.svc.UploadSaveFile(bg, hub.UploadSaveInput{UserID: f.adminID, DeviceID: hub.WebDeviceID(f.adminID), GameID: f.game, Slot: "run",
		Body: strings.NewReader("newer")}); err != nil {
		t.Fatal(err)
	}
	rec = f.c.postForm(run+"/delete", url.Values{"expected_revision": {"1"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	contains(t, f.c.get(location(rec), nil), "The slot changed in the meantime")
	if _, err := f.e.svc.GetSaveSlot(bg, f.adminID, f.game, "run"); err != nil {
		t.Fatalf("stale delete removed the slot: %v", err)
	}

	// OK with the current revision: back to the list with the flash.
	rec = f.c.postForm(run+"/delete", url.Values{"expected_revision": {"2"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if location(rec) != "/saves?ok=slotdeleted" {
		t.Fatalf("location %q", location(rec))
	}
	contains(t, f.c.get("/saves?ok=slotdeleted", nil), "Slot deleted.")
	status(t, f.c.get(run, nil), http.StatusNotFound)
	contains(t, f.c.get(f.base, nil), "CURRENT") // the default slot is untouched

	// The main slot can be deleted too: flash names "Main".
	rec = f.c.postForm(f.base+"/delete", url.Values{"expected_revision": {"1"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if location(rec) != "/saves?ok=maindeleted" {
		t.Fatalf("location %q", location(rec))
	}
	contains(t, f.c.get(location(rec), nil), `Slot &#34;Main&#34; deleted.`)
	status(t, f.c.get(f.base, nil), http.StatusNotFound)
}

func TestSaveSlotAndUploadBlockedBySessionWeb(t *testing.T) {
	f, run := newSlotFixture(t)
	dev := pairTestDevice(t, f.e, f.adminID, "Host")
	p := hub.Principal{Device: hub.Device{ID: dev, Name: "Host"}, User: hub.User{ID: f.adminID}}
	sess, err := f.e.svc.PublishSession(bg, p, f.game, hub.VisibilityPrivate)
	if err != nil {
		t.Fatal(err)
	}

	// Upload button disabled with a hint; rename/delete disabled; the panel URL shows no form.
	rec := f.c.get(f.base, nil)
	contains(t, rec, `<button class="btn small upl-toggle" type="button" disabled`, "A Session of this game is running. Upload after it ends.")
	notContains(t, rec, `href="`+f.base+`?upload=1"`)
	rec = f.c.get(f.base+"?upload=1", nil)
	notContains(t, rec, `class="upl-panel"`, `type="file"`)
	rec = f.c.get(run, nil)
	contains(t, rec, "Renaming and deleting slots is possible after it ends.", "disabled>Rename slot", "disabled>Delete slot")
	notContains(t, rec, `href="`+run+`?rename=1"`)
	notContains(t, f.c.get(run+"?rename=1", nil), `action="`+run+`/rename"`)

	// Direct posts are refused too and nothing changes.
	rec = f.c.multipartPost(f.base+"/upload", f.fields(nil), "a.sav", []byte("sneaky"))
	status(t, rec, http.StatusSeeOther)
	if loc := location(rec); !strings.Contains(loc, "up=running") {
		t.Fatalf("location %q", loc)
	}
	contains(t, f.c.get(location(rec), nil), "A Session of this game is running. Upload after it ends.", "disabled")
	rec = f.c.postForm(run+"/rename", url.Values{"name": {"x"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	contains(t, f.c.get(location(rec), nil), "A Session of this game is running. End it first")
	rec = f.c.postForm(run+"/delete", url.Values{"expected_revision": {"1"}, "_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	contains(t, f.c.get(location(rec), nil), "A Session of this game is running. End it first")
	if sl, _ := f.e.svc.GetSaveSlot(bg, f.adminID, f.game, "default"); sl.Current.Revision != 1 {
		t.Fatalf("upload went through: %+v", sl.Current)
	}

	// After the Session: everything is back.
	if err := f.e.svc.EndSession(bg, p, sess.SessionID); err != nil {
		t.Fatal(err)
	}
	contains(t, f.c.get(f.base, nil), `href="`+f.base+`?upload=1"`)
	contains(t, f.c.get(run, nil), `href="`+run+`?rename=1"`)
}

func TestSaveHistoryDeleteAnyVersionWeb(t *testing.T) {
	f, _ := newSlotFixture(t)
	if _, err := f.e.svc.UploadSaveFile(bg, hub.UploadSaveInput{UserID: f.adminID, DeviceID: hub.WebDeviceID(f.adminID), GameID: f.game, Slot: "default",
		Body: strings.NewReader("newer")}); err != nil {
		t.Fatal(err)
	}
	hist, err := f.e.svc.ListSaveHistory(bg, f.adminID, f.game, "default")
	if err != nil || len(hist) == 0 {
		t.Fatalf("%v %v", hist, err)
	}
	var auto *hub.SaveVersion
	for i := range hist {
		if hist[i].Reason != hub.HistoryManualSnapshot {
			auto = &hist[i]
		}
	}
	if auto == nil {
		t.Fatalf("no non-snapshot version: %+v", hist)
	}
	v := strconv.Itoa(auto.Version)
	contains(t, f.c.get(f.base, nil), "Delete version…")
	rec := f.c.get(f.base+"?delete="+v, hxHdr("confirm-v"+v))
	contains(t, rec, "Delete version v"+v+"?", "removed for good")
	rec = f.c.postForm(f.base+"/history/"+v+"/delete", url.Values{"_csrf": {f.tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if location(rec) != f.base+"?ok=snapdeleted" {
		t.Fatalf("location %q", location(rec))
	}
}
