package hub_test

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

type savesEnv struct {
	t    *testing.T
	svc  *hub.Service
	user hub.User
	game hub.Game
	devA string
	devB string
}

func hexSHA(b []byte) string { h := sha256.Sum256(b); return hex.EncodeToString(h[:]) }

func pairDevice(t *testing.T, svc *hub.Service, userID, name string) string {
	t.Helper()
	id := uuid.NewString()
	c, err := svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: id, DeviceName: name, Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.1"})
	if err != nil {
		t.Fatal(err)
	}
	if err := svc.ApprovePairing(ctx, c.RequestID, userID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.PollPairing(ctx, c.RequestID, c.PollToken); err != nil {
		t.Fatal(err)
	}
	return id
}

func newSavesEnv(t *testing.T) *savesEnv {
	t.Helper()
	svc, _ := hubtest.New(t, nil)
	u, err := svc.CreateAdmin(ctx, "fabio", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "", "", u.ID)
	if err != nil {
		t.Fatal(err)
	}
	return &savesEnv{t: t, svc: svc, user: u, game: g,
		devA: pairDevice(t, svc, u.ID, "Desktop"), devB: pairDevice(t, svc, u.ID, "Laptop")}
}

func (e *savesEnv) put(dev string, base int, data []byte, reason string) hub.PutSaveResult {
	e.t.Helper()
	r, err := e.svc.PutSave(ctx, hub.PutSaveInput{UserID: e.user.ID, DeviceID: dev, GameID: e.game.ID, Slot: "default",
		BaseRevision: base, SHA256: hexSHA(data), Reason: reason, Body: bytes.NewReader(data)})
	if err != nil {
		e.t.Fatalf("put: %v", err)
	}
	return r
}

func (e *savesEnv) history() []hub.SaveVersion {
	e.t.Helper()
	h, err := e.svc.ListSaveHistory(ctx, e.user.ID, e.game.ID, "default")
	if err != nil {
		e.t.Fatal(err)
	}
	return h
}

func (e *savesEnv) content() []byte {
	e.t.Helper()
	f, _, err := e.svc.OpenSaveContent(ctx, e.user.ID, e.game.ID, "default")
	if err != nil {
		e.t.Fatal(err)
	}
	defer f.Close()
	b, _ := io.ReadAll(f)
	return b
}

func TestSaveFirstUploadAndNormalUpload(t *testing.T) {
	e := newSavesEnv(t)
	a, b := []byte("save-one"), []byte("save-two")
	r := e.put(e.devA, 0, a, hub.SyncCheckpoint)
	if r.Conflict != nil || r.Slot.Current.Revision != 1 || r.Slot.Current.SHA256 != hexSHA(a) || r.Slot.Current.DeviceName != "Desktop" {
		t.Fatalf("%+v", r)
	}
	r = e.put(e.devA, 1, b, hub.SyncCheckpoint)
	if r.Slot.Current.Revision != 2 || !bytes.Equal(e.content(), b) {
		t.Fatalf("%+v", r.Slot.Current)
	}
	if h := e.history(); len(h) != 0 {
		t.Fatalf("plain checkpoints must not create history: %+v", h)
	}
	// Older checkpoint content is dropped (not captured in history).
	if _, err := os.Stat(filepath.Join(e.svc.DataDir(), "saves", e.user.ID, e.game.ID, "default", hexSHA(a))); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("old content kept: %v", err)
	}
}

func TestSaveIdempotentSameHash(t *testing.T) {
	e := newSavesEnv(t)
	a := []byte("save-one")
	e.put(e.devA, 0, a, hub.SyncCheckpoint)
	r := e.put(e.devA, 0, a, hub.SyncCheckpoint) // retry, even with a stale base
	if r.Conflict != nil || r.Slot.Current.Revision != 1 {
		t.Fatalf("%+v", r)
	}
	r = e.put(e.devB, 7, a, hub.SyncCheckpoint)
	if r.Conflict != nil || r.Slot.Current.Revision != 1 || len(e.history()) != 0 {
		t.Fatalf("%+v", r)
	}
}

func TestSaveStaleBaseConflict(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("v1"), hub.SyncCheckpoint)
	e.put(e.devA, 1, []byte("v2"), hub.SyncCheckpoint)
	hubBytes := []byte("v3-hub")
	e.put(e.devA, 2, hubBytes, hub.SyncCheckpoint) // Rev 3
	r := e.put(e.devB, 1, []byte("laptop-1"), hub.SyncCheckpoint)
	if r.Conflict == nil || r.Conflict.Status != hub.ConflictOpen || r.Slot.Current.Revision != 3 || !bytes.Equal(e.content(), hubBytes) {
		t.Fatalf("%+v", r)
	}
	c := r.Conflict
	if c.Hub.Revision != 3 || c.Secured.BaseRevision != 1 || c.Secured.DeviceName != "Laptop" || c.Secured.SHA256 != hexSHA([]byte("laptop-1")) {
		t.Fatalf("%+v", c)
	}
	h := e.history()
	if len(h) != 1 || h[0].Reason != hub.HistoryConflictUpload || h[0].Version != c.Secured.Version {
		t.Fatalf("%+v", h)
	}
	// Same device again: the open conflict is updated, not duplicated; the older secured upload stays in history.
	r2 := e.put(e.devB, 2, []byte("laptop-2"), hub.SyncCheckpoint)
	if r2.Conflict == nil || r2.Conflict.ID != c.ID || len(r2.Slot.OpenConflicts) != 1 || r2.Conflict.Secured.SHA256 != hexSHA([]byte("laptop-2")) {
		t.Fatalf("%+v", r2)
	}
	if h := e.history(); len(h) != 2 {
		t.Fatalf("%+v", h)
	}
	// Retry of the identical secured upload adds nothing.
	e.put(e.devB, 2, []byte("laptop-2"), hub.SyncCheckpoint)
	if h := e.history(); len(h) != 2 {
		t.Fatalf("retry created history: %+v", h)
	}
	// A different device creates its own conflict.
	devC := pairDevice(t, e.svc, e.user.ID, "Deck")
	r3 := e.put(devC, 0, []byte("deck"), hub.SyncCheckpoint)
	if r3.Conflict == nil || r3.Conflict.ID == c.ID || len(r3.Slot.OpenConflicts) != 2 {
		t.Fatalf("%+v", r3)
	}
	if n, _ := e.svc.OpenConflictCount(ctx); n != 2 {
		t.Fatalf("count %d", n)
	}
}

func TestSaveBaseWithoutSlotRejected(t *testing.T) {
	e := newSavesEnv(t)
	_, err := e.svc.PutSave(ctx, hub.PutSaveInput{UserID: e.user.ID, DeviceID: e.devA, GameID: e.game.ID, Slot: "default",
		BaseRevision: 3, SHA256: hexSHA([]byte("x")), Reason: hub.SyncCheckpoint, Body: strings.NewReader("x")})
	if !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("%v", err)
	}
}

func TestSaveDeviceChangeCopiesPreviousToHistory(t *testing.T) {
	e := newSavesEnv(t)
	a := []byte("from-desktop")
	e.put(e.devA, 0, a, hub.SyncCheckpoint)
	r := e.put(e.devB, 1, []byte("from-laptop"), hub.SyncCheckpoint)
	if r.Conflict != nil || r.Slot.Current.Revision != 2 || r.Slot.Current.DeviceName != "Laptop" {
		t.Fatalf("%+v", r)
	}
	h := e.history()
	if len(h) != 1 || h[0].Reason != hub.HistoryDeviceChange || h[0].Revision != 1 || h[0].SHA256 != hexSHA(a) || h[0].DeviceName != "Desktop" {
		t.Fatalf("%+v", h)
	}
	f, _, err := e.svc.OpenSaveVersion(ctx, e.user.ID, e.game.ID, "default", 1)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if b, _ := io.ReadAll(f); !bytes.Equal(b, a) {
		t.Fatal("history content differs")
	}
	// Same device again: no further history.
	e.put(e.devB, 2, []byte("laptop-again"), hub.SyncCheckpoint)
	if len(e.history()) != 1 {
		t.Fatal("unexpected history")
	}
}

func TestSaveFinalSessionEndCreatesHistory(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("a"), hub.SyncCheckpoint)
	e.put(e.devA, 1, []byte("b"), hub.SyncFinal)
	if len(e.history()) != 0 {
		t.Fatal("final alone must not create history")
	}
	e.put(e.devA, 2, []byte("c"), hub.SyncFinalSessionEnd)
	h := e.history()
	if len(h) != 1 || h[0].Reason != hub.HistorySessionEnd || h[0].Revision != 3 || h[0].SHA256 != hexSHA([]byte("c")) {
		t.Fatalf("%+v", h)
	}
	// Retry of the same final upload: no duplicate.
	e.put(e.devA, 2, []byte("c"), hub.SyncFinalSessionEnd)
	if len(e.history()) != 1 {
		t.Fatal("duplicate history version")
	}
}

func (e *savesEnv) makeConflict() (conflictID string) {
	e.put(e.devA, 0, []byte("hub-1"), hub.SyncCheckpoint)
	e.put(e.devA, 1, []byte("hub-2"), hub.SyncCheckpoint) // Rev 2 (device A)
	r := e.put(e.devB, 1, []byte("local-B"), hub.SyncCheckpoint)
	if r.Conflict == nil {
		e.t.Fatal("no conflict")
	}
	return r.Conflict.ID
}

func (e *savesEnv) resolve(id, resolution string, expected int, by string) (hub.SaveSlot, error) {
	return e.svc.ResolveSaveConflict(ctx, hub.ResolveInput{UserID: e.user.ID, GameID: e.game.ID, Slot: "default",
		ConflictID: id, Resolution: resolution, ExpectedRevision: expected, ResolvedBy: by})
}

func TestSaveResolveUseHub(t *testing.T) {
	e := newSavesEnv(t)
	id := e.makeConflict()
	s, err := e.resolve(id, hub.ResolveUseHub, 2, "device:"+e.devB)
	if err != nil {
		t.Fatal(err)
	}
	if s.Current.Revision != 2 || len(s.OpenConflicts) != 0 || !bytes.Equal(e.content(), []byte("hub-2")) {
		t.Fatalf("%+v", s)
	}
	h := e.history()
	var reasons []string
	for _, v := range h {
		reasons = append(reasons, v.Reason)
	}
	if len(h) != 2 || h[0].Reason != hub.HistoryBeforeResolution || h[0].SHA256 != hexSHA([]byte("hub-2")) || h[1].Reason != hub.HistoryConflictUpload {
		t.Fatalf("%v", reasons)
	}
	if n, _ := e.svc.OpenConflictCount(ctx); n != 0 {
		t.Fatalf("open conflicts %d", n)
	}
	if c, err := e.svc.GetSaveConflict(ctx, e.user.ID, e.game.ID, "default", id); err != nil || c.Status != hub.ConflictResolvedHub ||
		c.ResolvedBy != "device:"+e.devB || c.ResolvedAt == nil || c.Hub.Revision != 2 {
		t.Fatalf("%+v %v", c, err)
	}
	// Resolving again: stale, nothing changed.
	if _, err := e.resolve(id, hub.ResolveUseHub, 2, "x"); !errors.Is(err, hub.ErrSaveConflictStale) {
		t.Fatalf("%v", err)
	}
}

func TestSaveResolveUseLocal(t *testing.T) {
	e := newSavesEnv(t)
	id := e.makeConflict()
	s, err := e.resolve(id, hub.ResolveUseLocal, 2, "user:"+e.user.ID)
	if err != nil {
		t.Fatal(err)
	}
	if s.Current.Revision != 3 || s.Current.SHA256 != hexSHA([]byte("local-B")) || s.Current.DeviceName != "Laptop" ||
		!bytes.Equal(e.content(), []byte("local-B")) || len(s.OpenConflicts) != 0 {
		t.Fatalf("%+v", s)
	}
	if c, err := e.svc.GetSaveConflict(ctx, e.user.ID, e.game.ID, "default", id); err != nil || c.Status != hub.ConflictResolvedLocal ||
		c.ResolvedBy != "user:"+e.user.ID {
		t.Fatalf("%+v %v", c, err)
	}
	h := e.history()
	if len(h) != 2 || h[0].Reason != hub.HistoryBeforeResolution || h[0].Revision != 2 || h[0].SHA256 != hexSHA([]byte("hub-2")) {
		t.Fatalf("%+v", h)
	}
	// The hub content is still downloadable from history.
	f, _, err := e.svc.OpenSaveVersion(ctx, e.user.ID, e.game.ID, "default", h[0].Version)
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
}

func TestSaveResolveStaleExpectedRevisionChangesNothing(t *testing.T) {
	e := newSavesEnv(t)
	id := e.makeConflict()
	_, err := e.resolve(id, hub.ResolveUseLocal, 1, "device:"+e.devB)
	if !errors.Is(err, hub.ErrSaveConflictStale) {
		t.Fatalf("%v", err)
	}
	s, _ := e.svc.GetSaveSlot(ctx, e.user.ID, e.game.ID, "default")
	if s.Current.Revision != 2 || len(s.OpenConflicts) != 1 || len(e.history()) != 1 {
		t.Fatalf("state changed: %+v hist %d", s, len(e.history()))
	}
	// Unknown conflict id.
	if _, err := e.resolve("c_nope", hub.ResolveUseHub, 2, "x"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
}

func TestSaveCrossUserIsolation(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("secret"), hub.SyncCheckpoint)
	other, err := e.svc.CreateUser(ctx, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, other.ID, e.game.ID, "default"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if _, _, err := e.svc.OpenSaveContent(ctx, other.ID, e.game.ID, "default"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if l, _ := e.svc.ListSaveSlots(ctx, other.ID); len(l) != 0 {
		t.Fatalf("%+v", l)
	}
	if l, _ := e.svc.ListSaveSlots(ctx, ""); len(l) != 1 {
		t.Fatalf("%+v", l)
	}
	// The other user uploads into an own slot (base 0), independent of the first.
	r, err := e.svc.PutSave(ctx, hub.PutSaveInput{UserID: other.ID, DeviceID: "dev-x", GameID: e.game.ID, Slot: "default",
		SHA256: hexSHA([]byte("mine")), Reason: hub.SyncCheckpoint, Body: strings.NewReader("mine")})
	if err != nil || r.Conflict != nil || r.Slot.Current.Revision != 1 {
		t.Fatalf("%v %+v", err, r)
	}
}

func TestSaveLimitsAndHashMismatch(t *testing.T) {
	e := newSavesEnv(t)
	big := io.LimitReader(zeroReader{}, hub.MaxSaveBytes+1)
	_, err := e.svc.PutSave(ctx, hub.PutSaveInput{UserID: e.user.ID, DeviceID: e.devA, GameID: e.game.ID, Slot: "default",
		SHA256: hexSHA([]byte("x")), Reason: hub.SyncCheckpoint, Body: big})
	if !errors.Is(err, hub.ErrPayloadTooLarge) {
		t.Fatalf("%v", err)
	}
	_, err = e.svc.PutSave(ctx, hub.PutSaveInput{UserID: e.user.ID, DeviceID: e.devA, GameID: e.game.ID, Slot: "default",
		SHA256: hexSHA([]byte("other")), Reason: hub.SyncCheckpoint, Body: strings.NewReader("x")})
	if !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("%v", err)
	}
	if tmps, _ := os.ReadDir(filepath.Join(e.svc.DataDir(), "tmp")); len(tmps) != 0 {
		t.Fatalf("temp files left over: %d", len(tmps))
	}
	if _, err := e.svc.GetSaveSlot(ctx, e.user.ID, e.game.ID, "default"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("slot created by a failed upload: %v", err)
	}
	// Exactly 64 MiB is accepted.
	ok := bytes.Repeat([]byte{7}, hub.MaxSaveBytes)
	if r := e.put(e.devA, 0, ok, hub.SyncCheckpoint); r.Slot.Current.Size != hub.MaxSaveBytes {
		t.Fatalf("%+v", r.Slot.Current)
	}
}

type zeroReader struct{}

func (zeroReader) Read(p []byte) (int, error) { clear(p); return len(p), nil }
