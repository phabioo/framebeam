package web

import (
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

// One "Current" marker: a snapshot of the current checkpoint has the same revision and must not get a second one.
func TestSavesHistoryHasOneCurrent(t *testing.T) {
	f := newUploadFixture(t)
	dev := pairTestDevice(t, f.e, f.adminID, "Laptop")
	putSave(t, f.e, f.adminID, dev, f.game, 1, []byte("second"), hub.SyncFinalSessionEnd) // Rev 2 plus history
	label := "before the lighthouse"
	if _, err := f.e.svc.CreateSaveSnapshot(bg, f.adminID, f.game, "default", &label); err != nil {
		t.Fatal(err)
	}
	rec := f.c.get(f.base, nil)
	status(t, rec, 200)
	body := rec.Body.String()
	if n := strings.Count(body, "✓ Current"); n != 1 {
		t.Fatalf("%d Current markers", n)
	}
	// The marker sits on the original version, not on the snapshot.
	i := strings.Index(body, "✓ Current")
	j := strings.Index(body, "◆ Snapshot")
	if i < 0 || j < 0 || i < j { // newest first: the snapshot row comes before the original version
		t.Fatalf("markers: %d %d", i, j)
	}
	contains(t, rec, "HISTORY · ", " VERSIONS", "Manual snapshot of v", "Delete snapshot…")
	// The CURRENT bar names the history version of the checkpoint.
	contains(t, rec, `<span class="cur-eyebrow">CURRENT</span>`)
}

// The default slot is shown as "Main" (the API name stays "default"); a search narrows the list.
func TestSavesMainLabelAndSearch(t *testing.T) {
	f := newUploadFixture(t)
	rec := f.c.get(f.base, nil)
	contains(t, rec, ">Main <span", `placeholder="Search games or users"`)
	notContains(t, rec, ">default <span")
	// Search by game and by user; a miss shows the empty state with a way back.
	contains(t, f.c.get("/saves?q=lumen", nil), "Lumen Drift")
	rec = f.c.get("/saves?q=zzz", hxHdr("saves-list"))
	status(t, rec, 200)
	contains(t, rec, "No saves for “zzz”", "No game or user matches your search.", "Clear search")
	notContains(t, rec, "Lumen Drift")
	// The search survives a slot or history switch of the detail.
	contains(t, f.c.get(f.base+"?q=lumen", nil), `q=lumen`)
}

// While a Session of the game runs, Restore is disabled with the reason and a banner names the device.
func TestSavesRestoreBlockedWhileSessionRuns(t *testing.T) {
	f := newUploadFixture(t)
	dev := pairTestDevice(t, f.e, f.adminID, "Lena-PC")
	putSave(t, f.e, f.adminID, dev, f.game, 1, []byte("second"), hub.SyncFinalSessionEnd)
	rec := f.c.get(f.base, nil)
	contains(t, rec, `href="`+f.base+`?confirm=`) // possible while nothing runs
	notContains(t, rec, "Restore not possible")
	p := hub.Principal{Device: hub.Device{ID: dev, Name: "Lena-PC"}, User: hub.User{ID: f.adminID}}
	if _, err := f.e.svc.PublishSession(bg, p, f.game, hub.VisibilityPrivate); err != nil {
		t.Fatal(err)
	}
	rec = f.c.get(f.base, nil)
	contains(t, rec, "Restore not possible while a Session of this game is running · Lena-PC is playing Lumen Drift since ", `aria-disabled="true" title="Not possible while Lena-PC is playing"`)
	notContains(t, rec, `href="`+f.base+`?confirm=`)
}
