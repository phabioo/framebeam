package hub_test

import (
	"bytes"
	"errors"
	"path/filepath"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
	"github.com/phabioo/framebeam/server/internal/store"
)

func principal(t *testing.T, svc *hub.Service, userID, name string) hub.Principal {
	t.Helper()
	id := pairDevice(t, svc, userID, name)
	d, err := svc.GetDevice(ctx, id)
	if err != nil {
		t.Fatal(err)
	}
	u, err := svc.GetUser(ctx, userID)
	if err != nil {
		t.Fatal(err)
	}
	return hub.Principal{Device: d, User: u}
}

func TestSessionACLService(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	admin, _ := svc.CreateAdmin(ctx, "fabio", "secret-12345")
	anna, _ := svc.CreateUser(ctx, "anna", "Anna")
	g, err := svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	owner, own2, foreign := principal(t, svc, admin.ID, "Desktop"), principal(t, svc, admin.ID, "Laptop"), principal(t, svc, anna.ID, "Anna-PC")

	s, err := svc.PublishSession(ctx, owner, g.ID, hub.VisibilityPrivate)
	if err != nil || !s.IsOwner || s.Viewers == nil {
		t.Fatalf("%v %+v", err, s)
	}
	if _, err := svc.PublishSession(ctx, owner, g.ID, "bogus"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("bad visibility: %v", err)
	}
	if _, err := svc.GetSession(ctx, foreign, s.SessionID); !errors.Is(err, hub.ErrSessionForbidden) {
		t.Fatalf("foreign user, private: %v", err)
	}
	if _, err := svc.JoinSession(ctx, own2, s.SessionID); err != nil {
		t.Fatal(err)
	}
	// invite_only: invited foreign user may join, declining closes the door again.
	if _, err := svc.SetSessionVisibility(ctx, owner, s.SessionID, hub.VisibilityInviteOnly); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.JoinSession(ctx, foreign, s.SessionID); !errors.Is(err, hub.ErrSessionForbidden) {
		t.Fatalf("uninvited: %v", err)
	}
	if _, err := svc.InviteUser(ctx, owner, s.SessionID, anna.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.InviteUser(ctx, own2, s.SessionID, anna.ID); !errors.Is(err, hub.ErrSessionForbidden) {
		t.Fatalf("non-owner device invites: %v", err)
	}
	if err := svc.DeclineInvite(ctx, foreign, s.SessionID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.JoinSession(ctx, foreign, s.SessionID); !errors.Is(err, hub.ErrSessionForbidden) {
		t.Fatalf("declined: %v", err)
	}
	got, err := svc.GetSession(ctx, owner, s.SessionID)
	if err != nil || got.Invites == nil || (*got.Invites)[0].State != "declined" {
		t.Fatalf("%v %+v", err, got)
	}
	if err := svc.EndSession(ctx, owner, s.SessionID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetSession(ctx, owner, s.SessionID); !errors.Is(err, hub.ErrSessionEnded) {
		t.Fatalf("ended: %v", err)
	}
}

func TestRecoveredSessionEndsAfterGraceWithoutOwner(t *testing.T) {
	dir := t.TempDir()
	db, err := store.Open(filepath.Join(dir, "framebeam.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	open := func(grace time.Duration) *hub.Service {
		svc, err := hub.Open(ctx, db, hub.Options{DataDir: dir, HubVersion: "t", OwnerGrace: grace})
		if err != nil {
			t.Fatal(err)
		}
		return svc
	}
	svc := open(time.Hour)
	admin, _ := svc.CreateAdmin(ctx, "fabio", "secret-12345")
	g, _ := svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "", "", admin.ID)
	owner := principal(t, svc, admin.ID, "Desktop")
	s, err := svc.PublishSession(ctx, owner, g.ID, hub.VisibilityHubUsers)
	if err != nil {
		t.Fatal(err)
	}
	// "Restart": a new service on the same database; the Session is still active until the grace period is over.
	svc2 := open(300 * time.Millisecond)
	if _, err := svc2.GetSession(ctx, owner, s.SessionID); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(3 * time.Second)
	for {
		if _, err := svc2.GetSession(ctx, owner, s.SessionID); errors.Is(err, hub.ErrSessionEnded) {
			return
		}
		if time.Now().After(deadline) {
			t.Fatal("recovered Session without owner connection did not end")
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func TestStaleOwnerGraceCallbackIsIgnored(t *testing.T) {
	svc, _ := hubtest.New(t, func(o *hub.Options) { o.OwnerGrace = time.Hour })
	admin, _ := svc.CreateAdmin(ctx, "fabio", "secret-12345")
	g, _ := svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "", "", admin.ID)
	owner := principal(t, svc, admin.ID, "Desktop")
	s, err := svc.PublishSession(ctx, owner, g.ID, hub.VisibilityHubUsers)
	if err != nil {
		t.Fatal(err)
	}
	// The owner is offline (no WSS). Timer 1 fires too late to be stopped; timer 2 is the current grace period.
	stale := svc.ArmOwnerGraceForTest(s.SessionID, owner.Device.ID)
	svc.ArmOwnerGraceForTest(s.SessionID, owner.Device.ID)
	svc.OwnerGraceExpiredForTest(s.SessionID, owner.Device.ID, stale)
	if _, err := svc.GetSession(ctx, owner, s.SessionID); err != nil {
		t.Fatalf("stale callback ended the Session: %v", err)
	}
	if !svc.OwnerGraceArmedForTest(s.SessionID) {
		t.Fatal("stale callback removed the newer timer")
	}
}

func TestStaleViewerGraceCallbackKeepsNewerTimer(t *testing.T) {
	svc, _ := hubtest.New(t, func(o *hub.Options) { o.OwnerGrace = time.Hour })
	stale := svc.ArmViewerGraceForTest("v1", "dev1")
	svc.ArmViewerGraceForTest("v1", "dev1")
	svc.ViewerGraceExpiredForTest("v1", "dev1", stale)
	if !svc.ViewerGraceArmedForTest("v1") {
		t.Fatal("stale callback removed the newer timer")
	}
}
