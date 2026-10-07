package hub_test

import (
	"context"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// drainTopics collects the topics published within a short window.
func drainTopics(ch <-chan hub.Event) map[string]bool {
	got := map[string]bool{}
	for {
		select {
		case ev := <-ch:
			got[ev.Topic] = true
		case <-time.After(150 * time.Millisecond):
			return got
		}
	}
}

func wantTopics(t *testing.T, what string, got map[string]bool, want ...string) {
	t.Helper()
	for _, w := range want {
		if !got[w] {
			t.Errorf("%s: topic %q not published (got %v)", what, w, got)
		}
	}
}

func TestDerivedTopicsPublished(t *testing.T) {
	e := newSavesEnv(t)
	sctx, cancel := context.WithCancel(ctx)
	defer cancel()
	ch := e.svc.Subscribe(sctx)

	// Pairing: approve does not create the device, the poll does (clients and users change then).
	id := uuid.NewString()
	c, err := e.svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: id, DeviceName: "New", Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.9"})
	if err != nil {
		t.Fatal(err)
	}
	if err := e.svc.ApprovePairing(ctx, c.RequestID, e.user.ID); err != nil {
		t.Fatal(err)
	}
	drainTopics(ch)
	if _, err := e.svc.PollPairing(ctx, c.RequestID, c.PollToken); err != nil {
		t.Fatal(err)
	}
	wantTopics(t, "PollPairing", drainTopics(ch), hub.TopicClients, hub.TopicUsers)

	// A poll that creates nothing (consumed) publishes nothing.
	e.svc.PollPairing(ctx, c.RequestID, c.PollToken)
	if got := drainTopics(ch); len(got) != 0 {
		t.Errorf("consumed poll published %v", got)
	}

	if err := e.svc.RevokeDevice(ctx, id); err != nil {
		t.Fatal(err)
	}
	wantTopics(t, "RevokeDevice", drainTopics(ch), hub.TopicClients, hub.TopicUsers)

	// Saves: first upload changes the Library's save count.
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	wantTopics(t, "PutSave", drainTopics(ch), hub.TopicSaves, hub.TopicLibrary)
}
