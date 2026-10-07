package config

import (
	"flag"
	"testing"
)

func parse(t *testing.T, env map[string]string, args ...string) *Config {
	t.Helper()
	fs := flag.NewFlagSet("t", flag.ContinueOnError)
	c := Register(fs, func(k string) string { return env[k] })
	if err := fs.Parse(args); err != nil {
		t.Fatal(err)
	}
	return c
}

func TestDefaultsEnvAndFlags(t *testing.T) {
	c := parse(t, nil)
	if c.DataDir != "/var/lib/framebeam" || c.Listen != ":8443" || c.Dev || !c.UseTLS() {
		t.Fatalf("defaults: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_LISTEN": ":9000", "FRAMEBEAM_DEV": "true"}, "-data-dir", "/x")
	if c.Listen != ":9000" || !c.Dev || c.DataDir != "/x" || c.UseTLS() {
		t.Fatalf("env/flag: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_LISTEN": ":9000"}, "-listen", "127.0.0.1:1")
	if c.Listen != "127.0.0.1:1" || !c.ListenIsLoopback() {
		t.Fatalf("flag override: %+v", c)
	}
}

func TestValidate(t *testing.T) {
	if err := parse(t, nil, "-tls-cert", "a").Validate(); err == nil {
		t.Fatal("cert without key must fail")
	}
	if err := parse(t, nil, "-dev", "-tls-cert", "a", "-tls-key", "b").Validate(); err == nil {
		t.Fatal("dev + tls must fail")
	}
	if err := parse(t, nil).Validate(); err != nil {
		t.Fatal(err)
	}
}

func TestICEServers(t *testing.T) {
	if c := parse(t, nil); len(c.ICEServers) != 0 || c.Validate() != nil {
		t.Fatalf("default must be empty: %+v", c.ICEServers)
	}
	c := parse(t, map[string]string{"FRAMEBEAM_ICE_SERVERS": "stun:a.example:3478"})
	if len(c.ICEServers) != 1 || c.ICEServers[0] != "stun:a.example:3478" || c.Validate() != nil {
		t.Fatalf("env: %v", c.ICEServers)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_ICE_SERVERS": "stun:env.example"}, "-ice-servers", "stun:a.example:1, stun:b.example:2")
	if len(c.ICEServers) != 2 || c.ICEServers[1] != "stun:b.example:2" || c.Validate() != nil {
		t.Fatalf("flag overrides env: %v", c.ICEServers)
	}
	if parse(t, nil, "-ice-servers", "turn:x.example").Validate() == nil {
		t.Fatal("non-stun URL must fail")
	}
}

func TestCoreSourceConfig(t *testing.T) {
	good := "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
	c := parse(t, nil)
	if c.CoreIndexURL == "" || len(c.CoreTrustKeys) != 0 || c.Validate() != nil {
		t.Fatalf("defaults: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_CORE_INDEX_URL": "https://mirror.example/i.json", "FRAMEBEAM_HUB_CORE_TRUST_KEYS": good + " , " + good})
	if c.CoreIndexURL != "https://mirror.example/i.json" || len(c.CoreTrustKeys) != 2 || c.Validate() != nil {
		t.Fatalf("env: %+v", c)
	}
	// Repeatable flag replaces the environment list, further uses append.
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_CORE_TRUST_KEYS": "env"}, "-core-trust-key", good, "-core-trust-key", good, "-core-index-url", "https://x.example/i")
	if len(c.CoreTrustKeys) != 2 || c.CoreIndexURL != "https://x.example/i" || c.Validate() != nil {
		t.Fatalf("flags: %+v", c)
	}
	for _, bad := range []*Config{parse(t, nil, "-core-index-url", "http://x.example/i"), parse(t, nil, "-core-trust-key", "nope")} {
		if bad.Validate() == nil {
			t.Fatalf("invalid core config accepted: %+v", bad)
		}
	}
}

func TestUpdateConfig(t *testing.T) {
	c := parse(t, nil)
	if c.UpdateIndexURL != "https://github.com/phabioo/framebeam/releases/download/updates-index/updates-index.json" ||
		c.UpdateRequestDir != "/run/framebeam" || c.Validate() != nil {
		t.Fatalf("defaults: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_UPDATE_INDEX_URL": "file:///tmp/updates-index.json", "FRAMEBEAM_HUB_UPDATE_REQUEST_DIR": "/tmp/req"})
	if c.UpdateIndexURL != "file:///tmp/updates-index.json" || c.UpdateRequestDir != "/tmp/req" || c.Validate() != nil {
		t.Fatalf("env: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_UPDATE_INDEX_URL": "https://env.example/i"}, "-update-index-url", "https://flag.example/i", "-update-request-dir", "/x")
	if c.UpdateIndexURL != "https://flag.example/i" || c.UpdateRequestDir != "/x" {
		t.Fatalf("flags: %+v", c)
	}
	if parse(t, nil, "-update-index-url", "http://x.example/i").Validate() == nil || parse(t, nil, "-update-request-dir", "").Validate() == nil {
		t.Fatal("invalid update config accepted")
	}
}
