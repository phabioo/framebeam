package web

import (
	"context"
	"net"
	"net/url"
	"strconv"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// Settings sections, the Network form (saved values win over hub.env), live and restart-bound settings.

func baseNet() config.Config {
	return config.Config{Listen: ":8443", TURNPort: config.DefaultTURNPort, TURNRelayPorts: config.DefaultTURNRelayPorts,
		SaveKeepRecent: 20, SaveKeepDaily: 30, SaveKeepWeekly: 26}
}

// netEnv starts an env with a Network configuration and counts restart requests.
func netEnv(t *testing.T, mod func(*NetConfig)) (*env, *int) {
	t.Helper()
	restarts := new(int)
	e := newEnv(t, true, func(c *Config) {
		c.Net = NetConfig{Base: baseNet(), Running: baseNet(), RequestRestart: func() { *restarts++ }}
		if mod != nil {
			mod(&c.Net)
		}
	})
	return e, restarts
}

func freePort(t *testing.T) int {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	return ln.Addr().(*net.TCPAddr).Port
}

var hxPane = map[string]string{"HX-Request": "true", "HX-Target": "settings-area"}

func TestSettingsSectionsAndFragments(t *testing.T) {
	e, _ := netEnv(t, nil)
	c := e.client()
	c.login()
	for _, sec := range []string{"updates", "general", "network", "security"} {
		rec := c.get("/settings/"+sec, nil)
		status(t, rec, 200)
		contains(t, rec, `id="settings-area"`, `id="settings-nav"`, `/static/settings.js`, `hx-push-url="true"`, "✓ All changes saved",
			"Changes save automatically. Admins only.", "<html")
		notContains(t, rec, "<no value>")
		if inlineRe.MatchString(rec.Body.String()) {
			t.Fatalf("%s: inline script/style", sec)
		}
		// htmx section switch: only the content area plus the out-of-band section column.
		frag := c.get("/settings/"+sec, hxPane)
		status(t, frag, 200)
		contains(t, frag, `hx-swap-oob="true"`, "✓ All changes saved")
		notContains(t, frag, "<html", `class="sidebar"`)
	}
	status(t, c.get("/settings/nope", nil), 404)
	contains(t, c.get("/settings", nil), "Versions, channel and automatic installs") // default section
	// A sidebar navigation (main swap) still gets the whole settings page content.
	contains(t, c.get("/settings/network", map[string]string{"HX-Request": "true", "HX-Target": "main"}), `id="settings-area"`)
}

func TestNetworkSettingsPrecedenceResetAndPending(t *testing.T) {
	e, _ := netEnv(t, nil)
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/network", nil)
	contains(t, rec, "From hub.env / flags", "Only in local network", "a public host and the built-in TURN relay", `value="8443"`, `value="3478"`,
		`value="49160"`, `value="49199"`, `name="value" value="20"`)
	notContains(t, rec, "Restart required to apply", "Set on the web interface")

	// A value saved on the web wins and needs a restart for the TURN port.
	rec = postTok(c, tok, "/settings/network/turn_port", url.Values{"value": {"3500"}})
	status(t, rec, 303)
	if loc := location(rec); loc != "/settings/network?ok=netsaved" {
		t.Fatal(loc)
	}
	ov, _ := e.svc.NetOverrides(bg)
	if ov[config.NetTURNPort] != "3500" {
		t.Fatalf("%v", ov)
	}
	rec = c.get("/settings/network?ok=netsaved", nil)
	contains(t, rec, `value="3500"`, "Set on the web interface", "Reset to hub.env value (3478)", "Restart required to apply: TURN port", "Restart hub now",
		"Network setting saved.", "Restart required")

	// Reset deletes the override; hub.env applies again and nothing is pending.
	status(t, postTok(c, tok, "/settings/network/turn_port/reset", nil), 303)
	if ov, _ = e.svc.NetOverrides(bg); len(ov) != 0 {
		t.Fatalf("%v", ov)
	}
	rec = c.get("/settings/network", nil)
	contains(t, rec, `value="3478"`, "From hub.env / flags")
	notContains(t, rec, "Restart required to apply")

	// Listen port: free port is accepted (test-bind), keeps the host part of the address and is pending.
	p := freePort(t)
	status(t, postTok(c, tok, "/settings/network/listen_port", url.Values{"value": {strconv.Itoa(p)}}), 303)
	contains(t, c.get("/settings/network", nil), "Restart required to apply: Listen port", `value="`+strconv.Itoa(p)+`"`)
	// HTMX autosave answers with the re-rendered section, not a redirect.
	rec = c.postForm("/settings/network/turn_port", url.Values{"value": {"3501"}}, map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "settings-area"})
	status(t, rec, 200)
	contains(t, rec, "✓ All changes saved", "Restart required to apply: Listen port, TURN port", `hx-swap-oob="true"`)
	notContains(t, rec, "<html")
}

func TestNetworkSettingsValidation(t *testing.T) {
	e, _ := netEnv(t, nil)
	c := e.client()
	tok := c.login()
	busy, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Fatal(err)
	}
	defer busy.Close()
	busyPort := busy.Addr().(*net.TCPAddr).Port

	cases := []struct {
		name, key string
		form      url.Values
		want      string
	}{
		{"low listen port", config.NetListenPort, url.Values{"value": {"443"}}, "install-hub.sh --port"},
		{"listen not a number", config.NetListenPort, url.Values{"value": {"abc"}}, "whole number"},
		{"listen too high", config.NetListenPort, url.Values{"value": {"70000"}}, "not a port number"},
		{"listen busy", config.NetListenPort, url.Values{"value": {strconv.Itoa(busyPort)}}, "Port " + strconv.Itoa(busyPort) + " is in use"},
		{"listen equals TURN port with TURN off is fine?", config.NetTURNPort, url.Values{"value": {"8443"}}, "must differ from the listen port"},
		{"turn port low", config.NetTURNPort, url.Values{"value": {"80"}}, "below 1024"},
		{"range reversed", config.NetRelayPorts, url.Values{"min": {"50000"}, "max": {"49999"}}, "relay range must look like"},
		{"range too big", config.NetRelayPorts, url.Values{"min": {"20000"}, "max": {"21001"}}, "at most 1000"},
		{"range low", config.NetRelayPorts, url.Values{"min": {"500"}, "max": {"600"}}, "between 1024 and 65535"},
		{"range contains TURN port", config.NetRelayPorts, url.Values{"min": {"3000"}, "max": {"3999"}}, "TURN port"},
		{"range contains listen port", config.NetRelayPorts, url.Values{"min": {"8000"}, "max": {"8500"}}, "listen port"},
		{"range not numbers", config.NetRelayPorts, url.Values{"min": {"a"}, "max": {"b"}}, "number"},
		{"turn without public host", config.NetTURN, url.Values{"value": {"1"}}, "needs a public host"},
		{"host with port", config.NetPublicHost, url.Values{"value": {"hub.example.com:8443"}}, "without port"},
		{"relay ip v6", config.NetRelayIP, url.Values{"value": {"::1"}}, "IPv4"},
		{"relay ip junk", config.NetRelayIP, url.Values{"value": {"1.2.3"}}, "IPv4"},
		{"stun scheme", config.NetICEServers, url.Values{"op": {"add"}, "url": {"http://x.example.com"}}, "stun: URL"},
		{"retention negative", config.NetKeepDaily, url.Values{"value": {"-1"}}, "0 or more"},
		{"retention text", config.NetKeepWeekly, url.Values{"value": {"many"}}, "whole number"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			rec := postTok(c, tok, "/settings/network/"+tc.key, tc.form)
			status(t, rec, 400)
			contains(t, rec, tc.want, "✕ Not saved")
			if ov, _ := e.svc.NetOverrides(bg); len(ov) != 0 {
				t.Fatalf("stored despite the error: %v", ov)
			}
			// HTMX: the section comes back (status 200) with the message next to the field.
			h := map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "settings-area"}
			rec = c.postForm("/settings/network/"+tc.key, tc.form, h)
			status(t, rec, 200)
			contains(t, rec, tc.want, "✕ Not saved", `class="set-err"`)
		})
	}
	status(t, postTok(c, tok, "/settings/network/bogus", url.Values{"value": {"1"}}), 404)

	// A rejected value stays in its input; the other fields keep their values.
	rec := postTok(c, tok, "/settings/network/turn_port", url.Values{"value": {"80"}})
	contains(t, rec, `value="80"`, `value="8443"`, `value="49160"`)

	// With a public host the relay can be switched on; then the host cannot be emptied.
	status(t, postTok(c, tok, "/settings/network/public_host", url.Values{"value": {"hub.example.com"}}), 303)
	status(t, postTok(c, tok, "/settings/network/turn", url.Values{"value": {"1"}}), 303)
	rec = postTok(c, tok, "/settings/network/public_host", url.Values{"value": {""}})
	status(t, rec, 400)
	contains(t, rec, "needs a public host")
	contains(t, c.get("/settings/network", nil), "Restart required to apply: Public host, Built-in TURN relay")
}

func TestNetworkLiveSettings(t *testing.T) {
	e, restarts := netEnv(t, nil)
	c := e.client()
	tok := c.login()
	if r, d, w := e.svc.SaveRetention(); r != 0 || d != 0 || w != 0 { // the test service was opened without retention
		t.Fatalf("%d %d %d", r, d, w)
	}
	status(t, postTok(c, tok, "/settings/network/save_keep_recent", url.Values{"value": {"3"}}), 303)
	if r, d, w := e.svc.SaveRetention(); r != 3 || d != 30 || w != 26 {
		t.Fatalf("retention applied live: %d %d %d", r, d, w)
	}
	status(t, postTok(c, tok, "/settings/network/save_keep_daily", url.Values{"value": {"0"}}), 303)
	if _, d, _ := e.svc.SaveRetention(); d != 0 {
		t.Fatal(d)
	}
	rec := c.get("/settings/network", nil)
	notContains(t, rec, "Restart required to apply")
	// Reset: back to the hub.env value, live again.
	status(t, postTok(c, tok, "/settings/network/save_keep_recent/reset", nil), 303)
	if r, _, _ := e.svc.SaveRetention(); r != 20 {
		t.Fatal(r)
	}

	// External STUN servers apply live as well.
	status(t, postTok(c, tok, "/settings/network/ice_servers", url.Values{"op": {"add"}, "url": {"stun:stun.example.com:3478"}}), 303)
	if got := e.svc.ICEServers(); len(got) != 1 || got[0] != "stun:stun.example.com:3478" {
		t.Fatalf("%v", got)
	}
	contains(t, c.get("/settings/network", nil), "stun:stun.example.com:3478", "Remove")
	status(t, postTok(c, tok, "/settings/network/ice_servers", url.Values{"op": {"add"}, "url": {"stun:stun.example.com:3478"}}), 400) // duplicate
	status(t, postTok(c, tok, "/settings/network/ice_servers", url.Values{"op": {"add"}, "url": {"stun:other.example.com:3478"}}), 303)
	status(t, postTok(c, tok, "/settings/network/ice_servers", url.Values{"op": {"remove"}, "url": {"stun:stun.example.com:3478"}}), 303)
	if got := e.svc.ICEServers(); len(got) != 1 || got[0] != "stun:other.example.com:3478" {
		t.Fatalf("%v", got)
	}
	status(t, postTok(c, tok, "/settings/network/ice_servers/reset", nil), 303)
	if got := e.svc.ICEServers(); len(got) != 0 {
		t.Fatalf("%v", got)
	}
	if *restarts != 0 {
		t.Fatal("restart requested by a live setting")
	}
}

func TestNetworkRestartEndpoint(t *testing.T) {
	e, restarts := netEnv(t, nil)
	anon := e.client()
	status(t, anon.postForm("/settings/network/restart", url.Values{}, nil), 303) // not signed in -> login
	c := e.client()
	tok := c.login()
	status(t, c.postForm("/settings/network/restart", url.Values{}, nil), 403)
	status(t, c.postForm("/settings/network/restart", url.Values{"_csrf": {"wrong"}}, nil), 403)
	if *restarts != 0 {
		t.Fatal("restart without CSRF token")
	}
	// Unchanged port: the page polls and reloads itself.
	rec := postTok(c, tok, "/settings/network/restart", nil)
	status(t, rec, 200)
	contains(t, rec, "Restarting…", `data-restart-poll="/settings/network"`)
	if *restarts != 1 {
		t.Fatalf("restarts: %d", *restarts)
	}
	// Changed port: a link to the new address, no reload.
	p := freePort(t)
	status(t, postTok(c, tok, "/settings/network/listen_port", url.Values{"value": {strconv.Itoa(p)}}), 303)
	rec = postTok(c, tok, "/settings/network/restart", nil)
	contains(t, rec, "Restarting…", ":"+strconv.Itoa(p)+"/settings/network")
	notContains(t, rec, "data-restart-poll")
	if *restarts != 2 {
		t.Fatalf("restarts: %d", *restarts)
	}
	// The pending card carries the confirmation text.
	contains(t, c.get("/settings/network", nil), `hx-confirm="The hub restarts and running Sessions end."`)
}

func TestNetworkRestartUnavailable(t *testing.T) {
	e, _ := netEnv(t, func(n *NetConfig) { n.RequestRestart = nil })
	c := e.client()
	tok := c.login()
	rec := postTok(c, tok, "/settings/network/restart", nil)
	status(t, rec, 400)
	contains(t, rec, "cannot restart itself")
}

func TestNetworkStartupIssuesAndReachability(t *testing.T) {
	e, _ := netEnv(t, func(n *NetConfig) {
		n.Issues = []NetIssue{
			{Key: config.NetListenPort, Value: "9999", Message: "Saved port 9999 could not be used: address already in use. The hub uses :8443 until you change it."},
			{Key: config.NetTURNPort, Value: "1111", Message: "stale issue"},
		}
	})
	c := e.client()
	tok := c.login()
	// The issue is only shown while the saved value is still the one that failed.
	contains(t, c.get("/settings/network", nil), "Only in local network")
	notContains(t, c.get("/settings/network", nil), "Saved port 9999")
	e.svc.SetNetOverride(bg, config.NetListenPort, "9999")
	rec := c.get("/settings/network", nil)
	contains(t, rec, "Saved port 9999 could not be used", "Not applied at startup")
	notContains(t, rec, "stale issue", "Restart required to apply: Listen port") // not pending: it already failed
	// Changing the value clears the error card and makes the change pending again.
	p := freePort(t)
	status(t, postTok(c, tok, "/settings/network/listen_port", url.Values{"value": {strconv.Itoa(p)}}), 303)
	rec = c.get("/settings/network", nil)
	notContains(t, rec, "Saved port 9999")
	contains(t, rec, "Restart required to apply: Listen port")
}

func TestNetworkReachabilityWithTURN(t *testing.T) {
	ts, err := turnsrv.Start(context.Background(), turnsrv.Config{PublicHost: "hub.example.com", RelayMin: 49160, RelayMax: 49199,
		RelayIP: net.ParseIP("203.0.113.10"), Secret: make([]byte, 32), ListenHost: "127.0.0.1", Port: 0,
		DeviceOK: func(context.Context, string) bool { return true }})
	if err != nil {
		t.Skip("TURN server cannot start here:", err)
	}
	defer ts.Close()
	e, _ := netEnv(t, func(n *NetConfig) {
		n.Running.TURN, n.Running.PublicHost = true, "hub.example.com"
		n.Base.TURN, n.Base.PublicHost = true, "hub.example.com"
	})
	e.cfg.TURN = ts
	// New server with the running TURN instance.
	e2 := newEnv(t, true, func(c *Config) { c.Net = e.cfg.Net; c.TURN = ts })
	c := e2.client()
	c.login()
	rec := c.get("/settings/network", nil)
	contains(t, rec, "Prepared for the internet", "hub.example.com", "203.0.113.10", "active allocations", "UDP 49160-49199 (relay)", "Port forwards on the router")
	notContains(t, rec, "Only in local network", "Check again")
	contains(t, rec, "Prepared for the internet") // also in the section column
	if !strings.Contains(c.get("/settings/updates", nil).Body.String(), "Prepared for the internet") {
		t.Fatal("section column lacks the network status")
	}
}

func TestSettingsGeneralAutosaveFragments(t *testing.T) {
	e, _ := netEnv(t, nil)
	c := e.client()
	tok := c.login()
	h := map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "settings-area"}
	rec := c.postForm("/settings/uploads", url.Values{"enabled": {"1"}}, h)
	status(t, rec, 200)
	contains(t, rec, "Active", "✓ All changes saved", `aria-checked="true"`)
	notContains(t, rec, "<html")
	// Invalid name: message next to the field, nothing lost.
	rec = c.postForm("/settings/name", url.Values{"name": {"  "}}, h)
	status(t, rec, 200)
	contains(t, rec, "✕ Not saved", "Name must be 1 to 100 characters", `class="set-err"`)
	// Wrong password inside the inline form (HTMX) keeps the form open.
	rec = c.postForm("/settings/password", url.Values{"current": {"wrong"}, "new": {"new-password"}, "new2": {"new-password"}}, h)
	status(t, rec, 200)
	contains(t, rec, "current password is incorrect", "<details class=\"set-pw\" open>")
	// Appearance and name reload the page (theme and sidebar).
	rec = c.postForm("/settings/appearance", url.Values{"mode": {"dark"}}, h)
	if rec.Header().Get("HX-Redirect") != "/settings/general?ok=appearance" {
		t.Fatalf("%q", rec.Header().Get("HX-Redirect"))
	}
}
