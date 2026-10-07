package turnsrv

import (
	"context"
	"errors"
	"net"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/pion/turn/v4"
)

var secret = []byte("0123456789abcdef0123456789abcdef")

func TestCredentialsValidate(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	u, p, exp := Issue(secret, "dev-1", now)
	if exp.Sub(now) != 12*time.Hour || !strings.HasSuffix(u, ":dev-1") {
		t.Fatalf("%s %v", u, exp)
	}
	if id, ok := Validate(secret, u, p, now.Add(time.Hour)); !ok || id != "dev-1" {
		t.Fatal("valid credentials rejected")
	}
	if _, ok := Validate(secret, u, p, exp); ok {
		t.Fatal("expired accepted")
	}
	if _, ok := Validate(secret, u, p+"x", now); ok {
		t.Fatal("bad password accepted")
	}
	if _, ok := Validate([]byte("another-secret-another-secret-00"), u, p, now); ok {
		t.Fatal("other secret accepted")
	}
	if _, ok := Validate(secret, "garbage", p, now); ok {
		t.Fatal("garbage accepted")
	}
}

func TestPeerAllowed(t *testing.T) {
	pub := net.ParseIP("203.0.113.5")
	for _, c := range []struct {
		peer string
		ok   bool
	}{{"127.0.0.1", false}, {"::1", false}, {"0.0.0.0", false}, {"224.0.0.1", false}, {"169.254.1.1", false},
		{"fe80::1", false}, {"ff02::1", false}, {"192.0.2.10", true}, {"10.1.2.3", true}, {"192.168.0.7", true}} {
		if got := PeerAllowed(pub, net.ParseIP(c.peer)); got != c.ok {
			t.Errorf("%s: %v", c.peer, got)
		}
	}
	if !PeerAllowed(net.ParseIP("127.0.0.1"), net.ParseIP("127.0.0.1")) {
		t.Error("loopback relay must allow loopback peers")
	}
}

func TestHostFor(t *testing.T) {
	for _, c := range []struct{ req, pub, want string }{
		{"hub.example.org:8443", "x", "hub.example.org"},
		{"hub.example.org", "x", "hub.example.org"},
		{"192.0.2.1:8443", "x", "192.0.2.1"},
		{"[2001:db8::1]:8443", "x", "[2001:db8::1]"},
		{"[2001:db8::1]", "x", "[2001:db8::1]"},
		{"", "hub.example.org", "hub.example.org"},
		{"", "2001:db8::2", "[2001:db8::2]"},
	} {
		if got := HostFor(c.req, c.pub); got != c.want {
			t.Errorf("%q: %q want %q", c.req, got, c.want)
		}
	}
	u := URLs("[2001:db8::1]", 3478)
	if u[0] != "turn:[2001:db8::1]:3478?transport=udp" || u[1] != "turn:[2001:db8::1]:3478?transport=tcp" {
		t.Fatal(u)
	}
	if STUNURL("hub.example.org", 3478) != "stun:hub.example.org:3478" {
		t.Fatal()
	}
}

func TestResolveUpdatesAndWarnsOnce(t *testing.T) {
	ips := []net.IP{net.ParseIP("203.0.113.1")}
	var rerr error
	s, err := Start(context.Background(), Config{PublicHost: "hub.example.org", RelayMin: 49160, RelayMax: 49199, Secret: secret,
		ListenHost: "127.0.0.1", DeviceOK: func(context.Context, string) bool { return true },
		Resolver: func(context.Context, string) ([]net.IP, error) { return ips, rerr }, ResolveInterval: time.Hour})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if s.Status().RelayIP != "203.0.113.1" {
		t.Fatalf("%+v", s.Status())
	}
	ips, rerr = nil, errors.New("nxdomain")
	s.Resolve(context.Background())
	if s.Status().RelayIP != "" {
		t.Fatal("relay not dropped")
	}
	ips, rerr = []net.IP{net.ParseIP("2001:db8::1"), net.ParseIP("203.0.113.9")}, nil
	s.Resolve(context.Background())
	if s.Status().RelayIP != "203.0.113.9" {
		t.Fatalf("%+v", s.Status())
	}
}

func startLoopback(t *testing.T, deviceOK func(string) bool, relayIP net.IP, now func() time.Time) *Server {
	t.Helper()
	s, err := Start(context.Background(), Config{PublicHost: "127.0.0.1", RelayIP: relayIP, RelayMin: 49160, RelayMax: 49199,
		Secret: secret, ListenHost: "127.0.0.1", Now: now,
		DeviceOK: func(_ context.Context, id string) bool { return deviceOK(id) }})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close() })
	return s
}

func allocate(t *testing.T, s *Server, user, cred string) error {
	t.Helper()
	conn, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	c, err := turn.NewClient(&turn.ClientConfig{STUNServerAddr: net.JoinHostPort("127.0.0.1", itoa(s.Port())),
		TURNServerAddr: net.JoinHostPort("127.0.0.1", itoa(s.Port())), Username: user, Password: cred, Realm: Realm, Conn: conn})
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if err := c.Listen(); err != nil {
		t.Fatal(err)
	}
	rc, err := c.Allocate()
	if err != nil {
		return err
	}
	defer rc.Close()
	if a := rc.LocalAddr().(*net.UDPAddr); a.Port < 49160 || a.Port > 49199 || !a.IP.Equal(net.ParseIP("127.0.0.1")) {
		t.Errorf("relay addr %v", a)
	}
	return nil
}

func itoa(n int) string { return strconv.Itoa(n) }

func TestAllocationWithIssuedCredentials(t *testing.T) {
	revoked := map[string]bool{"revoked": true}
	s := startLoopback(t, func(id string) bool { return id == "dev-ok" || id == "revoked" && !revoked[id] }, net.ParseIP("127.0.0.1"), nil)
	c := s.Credentials("127.0.0.1:8443", "dev-ok")
	if err := allocate(t, s, c.Username, c.Credential); err != nil {
		t.Fatalf("valid: %v", err)
	}
	if err := allocate(t, s, c.Username, "wrong"); err == nil {
		t.Fatal("wrong password allocated")
	}
	r := s.Credentials("", "revoked")
	if err := allocate(t, s, r.Username, r.Credential); err == nil {
		t.Fatal("revoked device allocated")
	}
	u := s.Credentials("", "unknown")
	if err := allocate(t, s, u.Username, u.Credential); err == nil {
		t.Fatal("unknown device allocated")
	}
}

func TestAllocationExpiredAndNoRelayIP(t *testing.T) {
	past := time.Now().Add(-13 * time.Hour)
	s := startLoopback(t, func(string) bool { return true }, net.ParseIP("127.0.0.1"), nil)
	u, p, _ := Issue(secret, "dev", past)
	if err := allocate(t, s, u, p); err == nil {
		t.Fatal("expired allocated")
	}
	// No resolved IPv4: allocations fail.
	s2, err := Start(context.Background(), Config{PublicHost: "hub.example.org", RelayMin: 49160, RelayMax: 49199, Secret: secret,
		ListenHost: "127.0.0.1", DeviceOK: func(context.Context, string) bool { return true },
		Resolver: func(context.Context, string) ([]net.IP, error) { return nil, errors.New("down") }})
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close()
	c := s2.Credentials("", "dev")
	if err := allocate(t, s2, c.Username, c.Credential); err == nil {
		t.Fatal("allocated without relay IP")
	}
}
