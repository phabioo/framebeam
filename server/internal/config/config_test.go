package config

import (
	"flag"
	"path/filepath"
	"runtime"
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
	if c.DataDir != DefaultDataDir(runtime.GOOS, func(string) string { return "" }) || c.Listen != ":8443" || c.Dev || !c.UseTLS() {
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
	if c.CoreBuildbotURL != "https://buildbot.libretro.com/nightly" || c.CoreInfoURL != "https://buildbot.libretro.com/assets/frontend/info.zip" ||
		c.CoreIndexURL != "" || len(c.CoreTrustKeys) != 0 || c.Validate() != nil {
		t.Fatalf("defaults: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_CORE_BUILDBOT_URL": "https://mirror.example/nightly", "FRAMEBEAM_HUB_CORE_INFO_URL": "https://mirror.example/info.zip",
		"FRAMEBEAM_HUB_CORE_TRUST_KEYS": good + " , " + good})
	if c.CoreBuildbotURL != "https://mirror.example/nightly" || c.CoreInfoURL != "https://mirror.example/info.zip" || len(c.CoreTrustKeys) != 2 || c.Validate() != nil {
		t.Fatalf("env: %+v", c)
	}
	// Repeatable flag replaces the environment list, further uses append; flags win over the environment.
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_CORE_TRUST_KEYS": "env", "FRAMEBEAM_HUB_CORE_BUILDBOT_URL": "https://env.example/n"},
		"-core-trust-key", good, "-core-trust-key", good, "-core-buildbot-url", "https://x.example/n", "-core-info-url", "https://x.example/info.zip")
	if len(c.CoreTrustKeys) != 2 || c.CoreBuildbotURL != "https://x.example/n" || c.CoreInfoURL != "https://x.example/info.zip" || c.Validate() != nil {
		t.Fatalf("flags: %+v", c)
	}
	// The deprecated -core-index-url is still accepted (and ignored), even with an old http value.
	c = parse(t, map[string]string{"FRAMEBEAM_HUB_CORE_INDEX_URL": "https://old.example/i.json"})
	if c.CoreIndexURL != "https://old.example/i.json" || c.Validate() != nil {
		t.Fatalf("deprecated env: %+v", c)
	}
	if c = parse(t, nil, "-core-index-url", "http://x.example/i"); c.Validate() != nil {
		t.Fatal("deprecated flag must not be validated")
	}
	for _, bad := range []*Config{parse(t, nil, "-core-buildbot-url", "http://x.example/n"), parse(t, nil, "-core-info-url", "http://x.example/i.zip"),
		parse(t, nil, "-core-trust-key", "nope")} {
		if bad.Validate() == nil {
			t.Fatalf("invalid core config accepted: %+v", bad)
		}
	}
}

func TestUpdateConfig(t *testing.T) {
	c := parse(t, nil)
	if c.Validate() != nil || c.UpdateIndexURL != "https://github.com/phabioo/framebeam/releases/download/updates-index/updates-index.json" ||
		c.UpdateRequestDir != DefaultUpdateRequestDir(runtime.GOOS, c.DataDir) {
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

func TestTURNConfig(t *testing.T) {
	c := parse(t, nil)
	if c.TURN || c.TURNPort != 3478 || c.TURNRelayPorts != "49160-49199" || c.Validate() != nil {
		t.Fatalf("defaults: %+v", c)
	}
	if c = parse(t, nil, "-turn"); c.Validate() == nil {
		t.Fatal("-turn without -public-host accepted")
	}
	c = parse(t, map[string]string{"FRAMEBEAM_TURN": "1", "FRAMEBEAM_PUBLIC_HOST": "hub.example.org", "FRAMEBEAM_TURN_PORT": "3479",
		"FRAMEBEAM_TURN_RELAY_PORTS": "50000-50009", "FRAMEBEAM_TURN_RELAY_IP": "203.0.113.7"})
	lo, hi, rerr := c.TURNRelayRange()
	if err := c.Validate(); err != nil || !c.TURN || c.TURNPort != 3479 || lo != 50000 || hi != 50009 || rerr != nil {
		t.Fatalf("env: %+v %v", c, err)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_TURN_PORT": "3479"}, "-turn", "-public-host", "hub.example.org", "-turn-port", "4000")
	if c.TURNPort != 4000 || c.Validate() != nil {
		t.Fatalf("flag: %+v", c)
	}
	for _, bad := range [][]string{{"-turn-relay-ports", "5-"}, {"-turn-relay-ports", "50010-50000"}, {"-turn-relay-ports", "0-10"},
		{"-turn-relay-ports", "x"}, {"-turn-relay-ports", "1-70000"}, {"-turn-port", "0"}, {"-turn-relay-ip", "2001:db8::1"}, {"-turn-relay-ip", "nope"}} {
		c = parse(t, nil, append([]string{"-turn", "-public-host", "hub.example.org"}, bad...)...)
		if c.Validate() == nil {
			t.Errorf("%v accepted", bad)
		}
	}
}

func TestSaveRetentionConfig(t *testing.T) {
	c := parse(t, nil)
	if c.SaveKeepRecent != 20 || c.SaveKeepDaily != 30 || c.SaveKeepWeekly != 26 {
		t.Fatalf("defaults: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_SAVE_KEEP_RECENT": "5", "FRAMEBEAM_SAVE_KEEP_DAILY": "0"}, "-save-keep-weekly", "3")
	if c.SaveKeepRecent != 5 || c.SaveKeepDaily != 0 || c.SaveKeepWeekly != 3 {
		t.Fatalf("env/flag: %+v", c)
	}
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
	if err := parse(t, nil, "-save-keep-daily", "-1").Validate(); err == nil {
		t.Fatal("negative value must fail")
	}
}

func TestWindowsDefaults(t *testing.T) {
	env := map[string]string{"ProgramData": filepath.Join("D:", "PD")}
	get := func(k string) string { return env[k] }
	data := filepath.Join("D:", "PD", "FrameBeam", "Hub")
	if got := DefaultDataDir("windows", get); got != data {
		t.Fatalf("data dir %q", got)
	}
	if got := DefaultDataDir("windows", func(string) string { return "" }); got != filepath.Join(`C:\ProgramData`, "FrameBeam", "Hub") {
		t.Fatalf("fallback data dir %q", got)
	}
	if DefaultDataDir("linux", get) != "/var/lib/framebeam" || DefaultUpdateRequestDir("linux", "/x") != "/run/framebeam" {
		t.Fatal("linux defaults changed")
	}
	reg := func(goos string, args ...string) *Config {
		fs := flag.NewFlagSet("t", flag.ContinueOnError)
		c := register(fs, get, goos)
		if err := fs.Parse(args); err != nil {
			t.Fatal(err)
		}
		return c
	}
	c := reg("windows")
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
	if c.DataDir != data || c.UpdateRequestDir != filepath.Join(data, "update-request") || c.ImportDir() != filepath.Join(data, "library-import") {
		t.Fatalf("windows defaults: %+v", c)
	}
	// The request directory follows -data-dir unless set itself.
	c = reg("windows", "-data-dir", filepath.Join("E:", "hub"))
	if err := c.Validate(); err != nil || c.UpdateRequestDir != filepath.Join("E:", "hub", "update-request") {
		t.Fatalf("custom data dir: %v %q", err, c.UpdateRequestDir)
	}
	c = reg("windows", "-update-request-dir", "")
	if c.Validate() == nil {
		t.Fatal("explicit empty request dir accepted")
	}
}
