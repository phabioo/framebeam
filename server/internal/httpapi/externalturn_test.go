package httpapi

import (
	"context"
	"encoding/json"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

type turnEntry struct {
	Urls       []string `json:"urls"`
	Username   string   `json:"username"`
	Credential string   `json:"credential"`
	ExpiresAt  *string  `json:"expires_at"`
}

func TestExternalTURNInHelloAckJoinAndICEUpdate(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.ICEServers = []string{"stun:stun.example.org:3478"} })
	a1 := s.device(s.admin.ID, "Desktop")
	anna := s.device(s.anna.ID, "Anna-Laptop")
	c1 := s.dial(a1)

	s.svc.SetTURN(fakeTURN{}) // embedded + two external servers at once
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	const shared, staticPW = "shared-secret-0123456789", "static-pw-0123456789"
	s.svc.StartExternalTURN(ctx, []hub.ExternalTURN{
		{Name: "vps", Mode: hub.ExternalTURNSecret, Secret: shared, URLs: []string{"turn:turn.example.org:3478?transport=udp"}},
		{Name: "provider", Mode: hub.ExternalTURNStatic, Username: "u1", Password: staticPW, URLs: []string{"turns:turn2.example.org:5349?transport=tcp"}},
	}, hub.ExternalTURNOptions{Refresh: 20 * time.Millisecond})

	check := func(what string, ts []turnEntry, dev string) {
		t.Helper()
		if len(ts) != 3 {
			t.Fatalf("%s: want embedded + 2 external, got %+v", what, ts)
		}
		if ts[0].Username != "1:"+dev || ts[0].ExpiresAt == nil {
			t.Errorf("%s embedded: %+v", what, ts[0])
		}
		if !strings.HasSuffix(ts[1].Username, ":"+dev) || ts[1].ExpiresAt == nil || ts[1].Urls[0] != "turn:turn.example.org:3478?transport=udp" {
			t.Errorf("%s shared secret: %+v", what, ts[1])
		}
		if ts[1].Credential == shared || strings.Contains(ts[1].Username+ts[1].Credential, shared) {
			t.Errorf("%s: the shared secret must not be sent", what)
		}
		if ts[2].Username != "u1" || ts[2].Credential != staticPW || ts[2].ExpiresAt != nil || ts[2].Urls[0] != "turns:turn2.example.org:5349?transport=tcp" {
			t.Errorf("%s static: %+v", what, ts[2])
		}
	}

	// ice_update to the connected app (also periodic: shared-secret credentials are renewed).
	for i := 0; i < 2; i++ {
		var p struct {
			TurnServers []turnEntry `json:"turn_servers"`
		}
		c1.awaitWhere("ice_update", func(m wsMsg) bool {
			p.TurnServers = nil
			return json.Unmarshal(m.Payload, &p) == nil && len(p.TurnServers) == 3
		})
		check("ice_update", p.TurnServers, a1.id)
	}

	// hello_ack of a new connection.
	c2 := s.dialRaw(a1)
	c2.send("hello", map[string]any{"protocol_version": 1, "device_id": a1.id})
	var ack struct {
		TurnServers []turnEntry `json:"turn_servers"`
	}
	json.Unmarshal(c2.await("hello_ack").Payload, &ack)
	check("hello_ack", ack.TurnServers, a1.id)

	// Join response of another device.
	sess := s.publish(a1, "hub_users")
	var join struct {
		TurnServers []turnEntry `json:"turn_servers"`
	}
	rec := s.join(anna, sess.SessionID)
	if err := json.Unmarshal(rec.Body.Bytes(), &join); err != nil {
		t.Fatal(err)
	}
	check("join", join.TurnServers, anna.id)
	if strings.Contains(rec.Body.String(), shared) {
		t.Fatal("join response holds the shared secret")
	}
}

// A list saved in the app replaces the flag list for connected apps (ice_update) and new connections
// (hello_ack) without a restart.
func TestSavedExternalTURNListAppliesWithoutRestart(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.ICEServers = []string{"stun:stun.example.org:3478"} })
	a1 := s.device(s.admin.ID, "Desktop")
	c1 := s.dial(a1)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s.svc.StartExternalTURN(ctx, []hub.ExternalTURN{{Name: "flag", Mode: hub.ExternalTURNStatic, Username: "flag-user",
		Password: "flag-pw-0123456789", URLs: []string{"turn:flag.example.org:3478"}}}, hub.ExternalTURNOptions{Refresh: time.Hour})

	if err := s.svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Name: "web", Mode: hub.ExternalTURNStatic, Username: "web-user",
		Secret: "web-pw-0123456789", URLs: []string{"turn:web.example.org:3478"}}); err != nil {
		t.Fatal(err)
	}
	// The flag server is now a saved entry without stored secret (still resolved from the flags); delete it.
	if err := s.svc.DeleteExternalTURN(ctx, "flag"); err != nil {
		t.Fatal(err)
	}
	only := func(ts []turnEntry) bool {
		return len(ts) == 1 && ts[0].Username == "web-user" && ts[0].Credential == "web-pw-0123456789" && ts[0].Urls[0] == "turn:web.example.org:3478"
	}
	c1.awaitWhere("ice_update", func(m wsMsg) bool {
		var p struct {
			TurnServers []turnEntry `json:"turn_servers"`
		}
		return json.Unmarshal(m.Payload, &p) == nil && only(p.TurnServers)
	})
	c2 := s.dialRaw(a1)
	c2.send("hello", map[string]any{"protocol_version": 1, "device_id": a1.id})
	var ack struct {
		TurnServers []turnEntry `json:"turn_servers"`
	}
	json.Unmarshal(c2.await("hello_ack").Payload, &ack)
	if !only(ack.TurnServers) {
		t.Fatalf("hello_ack: %+v", ack.TurnServers)
	}
}
