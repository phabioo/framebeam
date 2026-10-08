package hub_test

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestDeleteDevice(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	owner := principal(t, e.svc, e.user.ID, "Host")
	s, err := e.svc.PublishSession(ctx, owner, e.game.ID, hub.VisibilityPrivate)
	if err != nil {
		t.Fatal(err)
	}
	other := principal(t, e.svc, e.user.ID, "Viewer")
	if _, err := e.svc.JoinSession(ctx, other, s.SessionID); err != nil {
		t.Fatal(err)
	}
	// A live token of the device to be deleted.
	if err := e.svc.DeleteDevice(ctx, "no-such-device"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown device: %v", err)
	}
	if err := e.svc.DeleteDevice(ctx, owner.Device.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.GetDevice(ctx, owner.Device.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("device still there: %v", err)
	}
	if err := e.svc.DeleteDevice(ctx, owner.Device.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("second delete: %v", err)
	}
	// The Session is gone (ended, then removed with its owner device); the viewer cannot see it any more.
	if _, err := e.svc.GetSession(ctx, other, s.SessionID); err == nil {
		t.Fatal("session still reachable after owner device deletion")
	}
	// Saves of the user and their history survive; a deleted uploader shows as "Deleted device".
	e.put(e.devA, 1, []byte("two"), hub.SyncFinalSessionEnd)
	if err := e.svc.DeleteDevice(ctx, e.devA); err != nil {
		t.Fatal(err)
	}
	slots, err := e.svc.ListSaveSlots(ctx, e.user.ID)
	if err != nil || len(slots) != 1 || slots[0].Current.DeviceID != e.devA || slots[0].Current.DeviceName != "Deleted device" {
		t.Fatalf("%v %+v", err, slots)
	}
	if h := e.history(); len(h) == 0 || h[0].DeviceName != "Deleted device" {
		t.Fatalf("%+v", h)
	}
	if string(e.content()) != "two" {
		t.Fatal("save content lost")
	}
	// The other device continues to work with the existing revision.
	e.put(e.devB, 2, []byte("three"), hub.SyncCheckpoint)
}

func TestDeleteUser(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	admin, _ := svc.CreateAdmin(ctx, "fabio", "secret-1234")
	anna, _ := svc.CreateUser(ctx, "anna", "Anna")
	bob, _ := svc.CreateUser(ctx, "bob", "Bob")
	g, err := svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "", "", anna.ID)
	if err != nil {
		t.Fatal(err)
	}
	annaDev := principal(t, svc, anna.ID, "Anna-PC")
	bobDev := principal(t, svc, bob.ID, "Bob-PC")
	put := func(p hub.Principal, data string) {
		t.Helper()
		if _, err := svc.PutSave(ctx, hub.PutSaveInput{UserID: p.User.ID, DeviceID: p.Device.ID, GameID: g.ID, Slot: "default",
			SHA256: hexSHA([]byte(data)), Reason: hub.SyncCheckpoint, Body: bytes.NewReader([]byte(data))}); err != nil {
			t.Fatal(err)
		}
	}
	put(annaDev, "anna-save")
	put(bobDev, "bob-save")
	blobs := filepath.Join(svc.DataDir(), "saves", anna.ID)
	if _, err := os.Stat(blobs); err != nil {
		t.Fatal(err)
	}
	inv, _, err := svc.CreateInvite(ctx, anna.ID, hub.DefaultInviteTTL, true)
	if err != nil {
		t.Fatal(err)
	}
	s, err := svc.PublishSession(ctx, annaDev, g.ID, hub.VisibilityPrivate)
	if err != nil {
		t.Fatal(err)
	}

	// Refusals: admin (own account and last admin), unknown user, non-admin actor.
	if err := svc.DeleteUser(ctx, admin.ID, admin.ID); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("own account: %v", err)
	}
	if err := svc.DeleteUser(ctx, admin.ID, bob.ID); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("admin by other: %v", err)
	}
	if err := svc.DeleteUser(ctx, anna.ID, bob.ID); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("non-admin actor: %v", err)
	}
	if err := svc.DeleteUser(ctx, "u_nobody", admin.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	if _, err := svc.GetUser(ctx, anna.ID); err != nil {
		t.Fatalf("refusals must not delete: %v", err)
	}

	if err := svc.DeleteUser(ctx, anna.ID, admin.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetUser(ctx, anna.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("user still there: %v", err)
	}
	if _, err := svc.GetDevice(ctx, annaDev.Device.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("device still there: %v", err)
	}
	if _, err := svc.GetSession(ctx, bobDev, s.SessionID); err == nil {
		t.Fatal("session still reachable")
	}
	if invs, _ := svc.ListInvites(ctx, 0); len(invs) != 0 {
		t.Fatalf("invites remain (%s): %+v", inv.ID, invs)
	}
	if _, err := os.Stat(blobs); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("save blobs remain: %v", err)
	}
	slots, err := svc.ListSaveSlots(ctx, "")
	if err != nil || len(slots) != 1 || slots[0].UserID != bob.ID {
		t.Fatalf("%v %+v", err, slots)
	}
	// The uploaded game stays and is reassigned to the deleting admin.
	got, err := svc.GetGame(ctx, g.ID)
	if err != nil || got.UploadedBy != admin.ID {
		t.Fatalf("%v %+v", err, got)
	}
}
