package config

import (
	"errors"
	"strings"
	"testing"
)

func baseCfg() Config {
	return Config{Listen: "0.0.0.0:8443", TURNPort: 3478, TURNRelayPorts: DefaultTURNRelayPorts, SaveKeepRecent: 20, SaveKeepDaily: 30, SaveKeepWeekly: 26,
		ICEServers: []string{"stun:env.example.com:3478"}}
}

func TestApplyNetOverridesWin(t *testing.T) {
	c := baseCfg()
	bad := c.ApplyNet(map[string]string{
		NetListenPort: "9000", NetPublicHost: "hub.example.com", NetTURN: "true", NetTURNPort: "3500", NetRelayPorts: "50000-50100",
		NetRelayIP: "203.0.113.10", NetICEServers: `["stun:web.example.com:3478"]`, NetKeepRecent: "5", NetKeepDaily: "0", NetKeepWeekly: "1",
		"unknown": "x"})
	if bad != nil {
		t.Fatal(bad)
	}
	if c.Listen != "0.0.0.0:9000" { // host part kept
		t.Fatal(c.Listen)
	}
	if !c.TURN || c.PublicHost != "hub.example.com" || c.TURNPort != 3500 || c.TURNRelayPorts != "50000-50100" || c.TURNRelayIP != "203.0.113.10" ||
		len(c.ICEServers) != 1 || c.ICEServers[0] != "stun:web.example.com:3478" || c.SaveKeepRecent != 5 || c.SaveKeepDaily != 0 || c.SaveKeepWeekly != 1 {
		t.Fatalf("%+v", c)
	}
	for _, k := range NetKeys {
		var d Config = baseCfg()
		if err := d.SetNet(k, c.NetValue(k)); err != nil {
			t.Fatalf("%s round trip: %v", k, err)
		}
		if d.NetValue(k) != c.NetValue(k) {
			t.Fatalf("%s: %q != %q", k, d.NetValue(k), c.NetValue(k))
		}
	}
	// An empty saved list/host overrides a non-empty hub.env value.
	e := baseCfg()
	e.PublicHost = "env.example.com"
	e.ApplyNet(map[string]string{NetPublicHost: "", NetICEServers: "[]"})
	if e.PublicHost != "" || len(e.ICEServers) != 0 {
		t.Fatalf("%+v", e)
	}
}

func TestApplyNetSkipsUnreadableValues(t *testing.T) {
	c := baseCfg()
	bad := c.ApplyNet(map[string]string{NetListenPort: "abc", NetTURNPort: "99999", NetKeepRecent: "-4", NetRelayPorts: "9-1", NetICEServers: "not json", NetTURN: "maybe", NetKeepDaily: "7"})
	if len(bad) != 6 {
		t.Fatalf("%v", bad)
	}
	if c.Listen != "0.0.0.0:8443" || c.TURNPort != 3478 || c.SaveKeepRecent != 20 || c.TURNRelayPorts != DefaultTURNRelayPorts || c.TURN || c.SaveKeepDaily != 7 {
		t.Fatalf("%+v", c) // unreadable values leave the hub.env value alone, readable ones still apply
	}
}

func TestListenPortHelpers(t *testing.T) {
	for in, want := range map[string]int{":8443": 8443, "127.0.0.1:9000": 9000, "[::1]:7000": 7000, "": DefaultListenPort, "junk": DefaultListenPort} {
		if got := (&Config{Listen: in}).ListenPort(); got != want {
			t.Fatalf("%q: %d", in, got)
		}
	}
	for _, tc := range [][3]string{{":8443", "9000", ":9000"}, {"127.0.0.1:8443", "9000", "127.0.0.1:9000"}, {"[::1]:8443", "9000", "[::1]:9000"}} {
		p := 0
		for _, ch := range tc[1] {
			p = p*10 + int(ch-'0')
		}
		if got := ListenWithPort(tc[0], p); got != tc[2] {
			t.Fatalf("%v: %q", tc, got)
		}
	}
}

// change applies one setting to a candidate and validates it like the web interface does.
func change(base Config, key, raw string) error {
	if err := base.SetNet(key, raw); err != nil {
		return err
	}
	return base.ValidateNet(key)
}

func TestValidateNet(t *testing.T) {
	withHost := baseCfg()
	withHost.PublicHost = "hub.example.com"
	for _, tc := range []struct {
		name string
		base Config
		key  string
		raw  string
		want string // "" = valid
	}{
		{"port ok", baseCfg(), NetListenPort, "9000", ""},
		{"port below 1024", baseCfg(), NetListenPort, "443", "install-hub.sh --port"},
		{"port 1024", baseCfg(), NetListenPort, "1024", ""},
		{"port 65536", baseCfg(), NetListenPort, "65536", "not a port number"},
		{"port equals relay range", baseCfg(), NetListenPort, "49170", "relay range"},
		{"port equals TURN port while TURN on", func() Config { c := withHost; c.TURN = true; return c }(), NetListenPort, "3478", "must differ"},
		{"turn port below 1024", baseCfg(), NetTURNPort, "100", "below 1024"},
		{"turn port equals listen", baseCfg(), NetTURNPort, "8443", "must differ"},
		{"turn port inside range", baseCfg(), NetTURNPort, "49170", "relay range"},
		{"range ok", baseCfg(), NetRelayPorts, "50000-50999", ""},
		{"range 1001 ports", baseCfg(), NetRelayPorts, "50000-51000", "at most 1000"},
		{"range reversed", baseCfg(), NetRelayPorts, "50000-49000", "must look like"},
		{"range below 1024", baseCfg(), NetRelayPorts, "1000-1100", "between 1024"},
		{"range above 65535", baseCfg(), NetRelayPorts, "65000-65536", "must look like"},
		{"range with TURN port", baseCfg(), NetRelayPorts, "3000-3999", "TURN port"},
		{"range with listen port", baseCfg(), NetRelayPorts, "8000-8900", "listen port"},
		{"turn without host", baseCfg(), NetTURN, "true", "public host"},
		{"turn with host", withHost, NetTURN, "true", ""},
		{"turn off needs nothing", baseCfg(), NetTURN, "false", ""},
		{"empty host while TURN on", func() Config { c := withHost; c.TURN = true; return c }(), NetPublicHost, "", "public host"},
		{"host with port", baseCfg(), NetPublicHost, "hub.example.com:8443", "without port"},
		{"host with scheme", baseCfg(), NetPublicHost, "https://hub.example.com", "without port or scheme"},
		{"host ok", baseCfg(), NetPublicHost, "hub.example.com", ""},
		{"relay ip ok", baseCfg(), NetRelayIP, "203.0.113.10", ""},
		{"relay ip empty", baseCfg(), NetRelayIP, "", ""},
		{"relay ip v6", baseCfg(), NetRelayIP, "2001:db8::1", "IPv4"},
		{"relay ip junk", baseCfg(), NetRelayIP, "hub", "IPv4"},
		{"stun ok", baseCfg(), NetICEServers, `["stun:stun.example.com:3478"]`, ""},
		{"stun wrong scheme", baseCfg(), NetICEServers, `["turn:x.example.com"]`, "stun: URL"},
		{"stun empty url", baseCfg(), NetICEServers, `["stun:"]`, "stun: URL"},
		{"retention ok", baseCfg(), NetKeepWeekly, "0", ""},
		{"retention negative", baseCfg(), NetKeepRecent, "-1", "0 or more"},
		{"retention huge", baseCfg(), NetKeepDaily, "100001", "between 0"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := change(tc.base, tc.key, tc.raw)
			if tc.want == "" {
				if err != nil {
					t.Fatal(err)
				}
				return
			}
			var ne *NetError
			if err == nil || !errors.As(err, &ne) || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("want %q, got %v", tc.want, err)
			}
		})
	}
	// Unusual hub.env values never block an unrelated change.
	odd := baseCfg()
	odd.Listen = ":443"
	if err := change(odd, NetKeepRecent, "10"); err != nil {
		t.Fatal(err)
	}
}
