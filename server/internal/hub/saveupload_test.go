package hub_test

import (
	"bytes"
	"errors"
	"io"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

func (e *savesEnv) upload(dev, slot string, expected *int, data []byte) (hub.SaveSlot, error) {
	return e.svc.UploadSaveFile(ctx, hub.UploadSaveInput{UserID: e.user.ID, DeviceID: dev, GameID: e.game.ID, Slot: slot,
		ExpectedRevision: expected, SHA256: hexSHA(data), Body: bytes.NewReader(data)})
}

func intp(i int) *int { return &i }

func TestUploadSaveFileNewSlot(t *testing.T) {
	e := newSavesEnv(t)
	sl, err := e.upload(e.devA, "default", intp(0), []byte("from-other-emu"))
	if err != nil {
		t.Fatal(err)
	}
	c := sl.Current
	if c.Revision != 1 || c.Reason != hub.SyncUpload || c.DeviceID != e.devA || c.SHA256 != hexSHA([]byte("from-other-emu")) {
		t.Fatalf("%+v", c)
	}
	if string(e.content()) != "from-other-emu" || len(e.history()) != 0 {
		t.Fatalf("content %q history %v", e.content(), e.history())
	}
}

func TestUploadSaveFileReplacesAndKeepsHistory(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	sl, err := e.upload(e.devB, "default", intp(1), []byte("two"))
	if err != nil {
		t.Fatal(err)
	}
	if sl.Current.Revision != 2 || sl.Current.Reason != hub.SyncUpload || sl.Current.DeviceID != e.devB || string(e.content()) != "two" {
		t.Fatalf("%+v", sl.Current)
	}
	h := e.history()
	if len(h) != 1 || h[0].Reason != hub.HistoryBeforeUpload || h[0].Revision != 1 || h[0].SHA256 != hexSHA([]byte("one")) {
		t.Fatalf("%+v", h)
	}
	// nil expected revision (web): replaced again; a manual snapshot of Rev 2 is not captured twice.
	if _, err := e.svc.CreateSaveSnapshot(ctx, e.user.ID, e.game.ID, "default", nil); err != nil {
		t.Fatal(err)
	}
	n := len(e.history())
	sl, err = e.upload(e.devA, "default", nil, []byte("three"))
	if err != nil || sl.Current.Revision != 3 || len(e.history()) != n {
		t.Fatalf("%+v %v %v", sl.Current, err, e.reasons())
	}
}

func TestUploadSaveFileIdempotent(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("same"), hub.SyncCheckpoint)
	sl, err := e.upload(e.devB, "default", intp(1), []byte("same"))
	if err != nil || sl.Current.Revision != 1 || sl.Current.DeviceID != e.devA || sl.Current.Reason != hub.SyncCheckpoint || len(e.history()) != 0 {
		t.Fatalf("%+v %v", sl.Current, err)
	}
}

func TestUploadSaveFileStale(t *testing.T) {
	e := newSavesEnv(t)
	if _, err := e.upload(e.devA, "default", intp(2), []byte("x")); !errors.Is(err, hub.ErrSaveConflictStale) {
		t.Fatalf("missing slot, N>0: %v", err)
	}
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	for _, want := range []int{0, 2} {
		if _, err := e.upload(e.devA, "default", intp(want), []byte("x")); !errors.Is(err, hub.ErrSaveConflictStale) {
			t.Fatalf("expected %d: %v", want, err)
		}
	}
	if string(e.content()) != "one" || len(e.history()) != 0 {
		t.Fatal("stale upload changed the slot")
	}
}

func TestUploadSaveFileRejects(t *testing.T) {
	e := newSavesEnv(t)
	in := hub.UploadSaveInput{UserID: e.user.ID, DeviceID: e.devA, GameID: e.game.ID, Slot: "default", ExpectedRevision: intp(0)}
	in.SHA256, in.Body = hexSHA([]byte("other")), strings.NewReader("x")
	if _, err := e.svc.UploadSaveFile(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("hash mismatch: %v", err)
	}
	in.SHA256, in.Body = hexSHA(nil), strings.NewReader("")
	if _, err := e.svc.UploadSaveFile(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("empty: %v", err)
	}
	in.SHA256, in.Body, in.Slot = hexSHA([]byte("x")), strings.NewReader("x"), "Bad Slot"
	if _, err := e.svc.UploadSaveFile(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("slot name: %v", err)
	}
	in.Slot, in.SHA256, in.Body = "default", hexSHA([]byte("x")), io.LimitReader(zeroReader{}, hub.MaxSaveBytes+1)
	if _, err := e.svc.UploadSaveFile(ctx, in); !errors.Is(err, hub.ErrPayloadTooLarge) {
		t.Fatalf("too large: %v", err)
	}
	if _, err := e.svc.GetSaveSlot(ctx, e.user.ID, e.game.ID, "default"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("slot created by a rejected upload: %v", err)
	}
}
