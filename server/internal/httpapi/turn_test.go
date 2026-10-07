package httpapi

import (
	"encoding/json"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

type fakeTURN struct{}

func (fakeTURN) STUNURL(h string) string {
	return turnsrv.STUNURL(turnsrv.HostFor(h, "hub.example.org"), 3478)
}
func (fakeTURN) Credentials(h, dev string) turnsrv.Credentials {
	return turnsrv.Credentials{URLs: turnsrv.URLs(turnsrv.HostFor(h, "hub.example.org"), 3478), Username: "1:" + dev,
		Credential: "secret-pw", ExpiresAt: time.Unix(1_800_000_000, 0).UTC()}
}

func TestTURNInHelloAckAndJoin(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.ICEServers = []string{"stun:stun.example.org:3478"} })
	a1 := s.device(s.admin.ID, "Desktop")
	anna := s.device(s.anna.ID, "Anna-Laptop")

	// Off: no turn_servers, ice_servers unchanged.
	c := s.dialRaw(a1)
	c.send("hello", map[string]any{"protocol_version": 1, "device_id": a1.id})
	if strings.Contains(string(c.await("hello_ack").Payload), "turn_servers") {
		t.Fatal("turn_servers while off")
	}
	sess := s.publish(a1, "hub_users")
	if strings.Contains(s.join(anna, sess.SessionID).Body.String(), "turn_servers") {
		t.Fatal("turn_servers in join while off")
	}

	s.svc.SetTURN(fakeTURN{})
	c2 := s.dialRaw(a1)
	c2.send("hello", map[string]any{"protocol_version": 1, "device_id": a1.id})
	var ack struct {
		IceServers  []string `json:"ice_servers"`
		TurnServers []struct {
			Urls                         []string `json:"urls"`
			Username, Credential, Expiry string
			ExpiresAt                    string `json:"expires_at"`
		} `json:"turn_servers"`
	}
	json.Unmarshal(c2.await("hello_ack").Payload, &ack)
	// The WSS upgrade went to the test server's 127.0.0.1 host.
	if len(ack.IceServers) != 2 || ack.IceServers[1] != "stun:127.0.0.1:3478" || len(ack.TurnServers) != 1 ||
		ack.TurnServers[0].Urls[0] != "turn:127.0.0.1:3478?transport=udp" || ack.TurnServers[0].Urls[1] != "turn:127.0.0.1:3478?transport=tcp" ||
		ack.TurnServers[0].Username != "1:"+a1.id || ack.TurnServers[0].ExpiresAt != "2027-01-15T08:00:00Z" {
		t.Fatalf("%+v", ack)
	}
	j := decode[joinResp](t, s.join(anna, sess.SessionID))
	if len(j.IceServers) != 2 || !strings.HasPrefix(j.IceServers[1], "stun:") {
		t.Fatalf("%+v", j)
	}
	if body := s.join(anna, sess.SessionID).Body.String(); !strings.Contains(body, `"turn_servers"`) || !strings.Contains(body, "?transport=tcp") ||
		!strings.Contains(body, `"username":"1:`+anna.id+`"`) {
		t.Fatal(body)
	}
}

func TestTURNSecretStable(t *testing.T) {
	e := newEnv(t, nil)
	a, err := e.svc.TURNSecret(t.Context())
	b, err2 := e.svc.TURNSecret(t.Context())
	if err != nil || err2 != nil || len(a) != 32 || string(a) != string(b) {
		t.Fatal(err, err2)
	}
}
