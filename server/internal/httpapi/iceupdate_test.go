package httpapi

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/hub"
)

type iceUpdatePayload struct {
	IceServers  []string `json:"ice_servers"`
	TurnServers []struct {
		Urls     []string `json:"urls"`
		Username string   `json:"username"`
	} `json:"turn_servers"`
}

func TestICEUpdatePushedToConnectedPlayers(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.ICEServers = []string{"stun:a.example.org:3478"} })
	d1 := s.device(s.admin.ID, "Desktop")
	d2 := s.device(s.anna.ID, "Anna-Laptop")
	c1, c2 := s.dial(d1), s.dial(d2)

	s.svc.SetICEServers([]string{"stun:b.example.org:3478"})
	for _, c := range []*wsClient{c1, c2} {
		var p iceUpdatePayload
		json.Unmarshal(c.await("ice_update").Payload, &p)
		if len(p.IceServers) != 1 || p.IceServers[0] != "stun:b.example.org:3478" || len(p.TurnServers) != 0 {
			t.Fatalf("%+v", p)
		}
	}

	// Unchanged effective list: no message. Back to the default: message.
	s.svc.SetICEServers([]string{"stun:b.example.org:3478"})
	s.svc.SetICEServers(nil)
	var p iceUpdatePayload
	json.Unmarshal(c1.await("ice_update").Payload, &p)
	if len(p.IceServers) != 1 || p.IceServers[0] != config.DefaultSTUNServer {
		t.Fatalf("%+v", p)
	}

	// Embedded TURN on: credentials per device, host = the WSS request host.
	s.svc.SetTURN(fakeTURN{})
	for i, c := range []*wsClient{c1, c2} {
		p = iceUpdatePayload{}
		m := c.awaitWhere("ice_update", func(m wsMsg) bool {
			return len(m.Payload) > 0 && json.Unmarshal(m.Payload, &p) == nil && len(p.TurnServers) > 0
		})
		_ = m
		want := "1:" + []string{d1.id, d2.id}[i]
		if len(p.IceServers) != 2 || p.IceServers[1] != "stun:127.0.0.1:3478" || p.TurnServers[0].Username != want {
			t.Fatalf("device %d: %+v", i, p)
		}
	}
	s.svc.SetTURN(nil)
	p = iceUpdatePayload{}
	c1.awaitWhere("ice_update", func(m wsMsg) bool {
		p = iceUpdatePayload{}
		json.Unmarshal(m.Payload, &p)
		return len(p.TurnServers) == 0
	})
	if len(p.IceServers) != 1 {
		t.Fatalf("%+v", p)
	}
}

func TestICEUpdateAfterCloudflareRefresh(t *testing.T) {
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusCreated)
		w.Write([]byte(`{"iceServers":[{"urls":["turn:turn.cloudflare.com:3478?transport=udp"],"username":"cf-user","credential":"cf-pass"}]}`))
	}))
	defer api.Close()
	s := newSessEnv(t, nil)
	c := s.dial(s.device(s.admin.ID, "Desktop"))
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s.svc.StartCloudflareTURN(ctx, hub.CloudflareTURNOptions{KeyID: "k", Token: "t", BaseURL: api.URL})
	var p iceUpdatePayload
	c.awaitWhere("ice_update", func(m wsMsg) bool {
		p = iceUpdatePayload{}
		json.Unmarshal(m.Payload, &p)
		return len(p.TurnServers) == 1
	})
	if p.TurnServers[0].Username != "cf-user" || p.TurnServers[0].Urls[0] != "turn:turn.cloudflare.com:3478?transport=udp" {
		t.Fatalf("%+v", p)
	}
}
