package hub_test

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

func (e *savesEnv) slotDir(slot string) string {
	return filepath.Join(e.svc.DataDir(), "saves", e.user.ID, e.game.ID, slot)
}

func TestRenameSaveSlotMovesEverything(t *testing.T) {
	e, _ := newComfortEnv(t, nil)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	u, g, web := e.user.ID, e.game.ID, hub.WebDeviceID(e.user.ID)
	if _, err := e.svc.CreateSaveSlot(ctx, u, g, "default", "run", web); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.CreateSaveSnapshot(ctx, u, g, "run", nil); err != nil {
		t.Fatal(err)
	}
	histBefore, _ := e.svc.ListSaveHistory(ctx, u, g, "run")

	sl, err := e.svc.RenameSaveSlot(ctx, hub.RenameInput{UserID: u, DeviceID: web, GameID: g, Slot: "run", NewSlot: "main2"})
	if err != nil {
		t.Fatal(err)
	}
	if sl.Slot != "main2" || sl.Current.Revision != 1 || sl.Current.SHA256 != hexSHA([]byte("one")) {
		t.Fatalf("%+v", sl)
	}
	histAfter, err := e.svc.ListSaveHistory(ctx, u, g, "main2")
	if err != nil || len(histAfter) != len(histBefore) || histAfter[0].Version != histBefore[0].Version {
		t.Fatalf("%+v %v", histAfter, err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, u, g, "run"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("old slot still there: %v", err)
	}
	if _, err := os.Stat(e.slotDir("run")); !os.IsNotExist(err) {
		t.Fatalf("old dir remains: %v", err)
	}
	if !e.fileExistsIn("main2", []byte("one")) {
		t.Fatal("content did not move")
	}
	f, _, err := e.svc.OpenSaveContent(ctx, u, g, "main2")
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
	// The version counter moved with the slot: a new snapshot gets the next number.
	v, err := e.svc.CreateSaveSnapshot(ctx, u, g, "main2", nil)
	if err != nil || v.Version != histBefore[0].Version+1 {
		t.Fatalf("%+v %v", v, err)
	}
}

func TestRenameSaveSlotErrors(t *testing.T) {
	e, _ := newComfortEnv(t, nil)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	u, g, web := e.user.ID, e.game.ID, hub.WebDeviceID(e.user.ID)
	for _, n := range []string{"run", "other"} {
		if _, err := e.svc.CreateSaveSlot(ctx, u, g, "default", n, web); err != nil {
			t.Fatal(err)
		}
	}
	rn := func(from, to string) error {
		_, err := e.svc.RenameSaveSlot(ctx, hub.RenameInput{UserID: u, DeviceID: web, GameID: g, Slot: from, NewSlot: to})
		return err
	}
	cases := []struct {
		from, to string
		want     error
	}{
		{"nothere", "x", hub.ErrNotFound},
		{"Bad/Name", "x", hub.ErrNotFound},
		{"default", "main", hub.ErrSaveSlotProtected},
		{"run", "other", hub.ErrSaveSlotExists},
		{"run", "default", hub.ErrSaveSlotExists},
		{"run", "run", hub.ErrBadRequest},
		{"run", "", hub.ErrBadRequest},
		{"run", "Upper", hub.ErrBadRequest},
		{"run", "nul", hub.ErrBadRequest},
		{"run", "com3", hub.ErrBadRequest},
		{"run", "a b", hub.ErrBadRequest},
	}
	for _, c := range cases {
		if err := rn(c.from, c.to); !errors.Is(err, c.want) {
			t.Errorf("%s -> %q: %v, want %v", c.from, c.to, err, c.want)
		}
	}
	// Leftover content of the target name is never merged into.
	if err := os.MkdirAll(e.slotDir("leftover"), 0o750); err != nil {
		t.Fatal(err)
	}
	if err := rn("run", "leftover"); !errors.Is(err, hub.ErrSaveSlotExists) {
		t.Errorf("leftover: %v", err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, u, g, "run"); err != nil {
		t.Fatalf("failed rename changed the slot: %v", err)
	}
}

func TestDeleteSaveSlot(t *testing.T) {
	e, _ := newComfortEnv(t, nil)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	u, g, web := e.user.ID, e.game.ID, hub.WebDeviceID(e.user.ID)
	if _, err := e.svc.CreateSaveSlot(ctx, u, g, "default", "run", web); err != nil {
		t.Fatal(err)
	}
	del := func(slot string, rev int) error {
		return e.svc.DeleteSaveSlot(ctx, hub.DeleteInput{UserID: u, DeviceID: web, GameID: g, Slot: slot, ConfirmRevision: rev})
	}
	if err := del("nothere", 1); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if err := del("run", 5); !errors.Is(err, hub.ErrSaveConflictStale) {
		t.Fatalf("%v", err)
	}
	if err := del("run", 0); !errors.Is(err, hub.ErrSaveConflictStale) {
		t.Fatalf("%v", err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, u, g, "run"); err != nil || !e.fileExistsIn("run", []byte("one")) {
		t.Fatalf("a refused delete changed the slot: %v", err)
	}
	if err := del("run", 1); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, u, g, "run"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if h, err := e.svc.ListSaveHistory(ctx, u, g, "run"); err == nil && len(h) > 0 {
		t.Fatalf("history remains: %+v", h)
	}
	if _, err := os.Stat(e.slotDir("run")); !os.IsNotExist(err) {
		t.Fatalf("content remains: %v", err)
	}
	// The default slot and its content are untouched.
	if sl, err := e.svc.GetSaveSlot(ctx, u, g, "default"); err != nil || sl.Current.Revision != 1 || !e.fileExists([]byte("one")) {
		t.Fatalf("%+v %v", sl, err)
	}
	// The default slot can be deleted too (stale confirmation refused first); the name starts from scratch afterwards.
	if err := del("default", 9); !errors.Is(err, hub.ErrSaveConflictStale) {
		t.Fatalf("%v", err)
	}
	if err := del("default", 1); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, u, g, "default"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if r := e.put(e.devA, 0, []byte("fresh"), hub.SyncCheckpoint); r.Slot.Current.Revision != 1 {
		t.Fatalf("%+v", r)
	}
	// Re-creating the name starts from scratch.
	sl, err := e.svc.CreateSaveSlot(ctx, u, g, "default", "run", web)
	if err != nil || sl.Current.Revision != 1 {
		t.Fatalf("%+v %v", sl, err)
	}
}

func TestSlotChangesBlockedWhileSessionRuns(t *testing.T) {
	e, _ := newComfortEnv(t, nil)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	u, g, web := e.user.ID, e.game.ID, hub.WebDeviceID(e.user.ID)
	if _, err := e.svc.CreateSaveSlot(ctx, u, g, "default", "run", web); err != nil {
		t.Fatal(err)
	}
	if on, err := e.svc.SessionRunningFor(ctx, u, g); err != nil || on {
		t.Fatalf("%v %v", on, err)
	}
	p := principal(t, e.svc, u, "Host")
	sess, err := e.svc.PublishSession(ctx, p, g, hub.VisibilityPrivate)
	if err != nil {
		t.Fatal(err)
	}
	if on, err := e.svc.SessionRunningFor(ctx, u, g); err != nil || !on {
		t.Fatalf("%v %v", on, err)
	}
	up := func() error {
		_, err := e.svc.UploadSaveFile(ctx, hub.UploadSaveInput{UserID: u, DeviceID: web, GameID: g, Slot: "run", Body: strings.NewReader("new")})
		return err
	}
	if err := up(); !errors.Is(err, hub.ErrSessionRunning) {
		t.Fatalf("upload: %v", err)
	}
	if _, err := e.svc.RenameSaveSlot(ctx, hub.RenameInput{UserID: u, DeviceID: web, GameID: g, Slot: "run", NewSlot: "x"}); !errors.Is(err, hub.ErrSessionRunning) {
		t.Fatalf("rename: %v", err)
	}
	if err := e.svc.DeleteSaveSlot(ctx, hub.DeleteInput{UserID: u, DeviceID: web, GameID: g, Slot: "run", ConfirmRevision: 1}); !errors.Is(err, hub.ErrSessionRunning) {
		t.Fatalf("delete: %v", err)
	}
	if err := e.svc.EndSession(ctx, p, sess.SessionID); err != nil {
		t.Fatal(err)
	}
	if err := up(); err != nil {
		t.Fatalf("upload after the Session: %v", err)
	}
}
