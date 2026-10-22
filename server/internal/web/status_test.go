package web

import (
	"log/slog"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/diag"
	"github.com/phabioo/framebeam/server/internal/health"
	"github.com/phabioo/framebeam/server/internal/updates"
)

func navLabels(gs []navGroup) (eyebrows, labels []string) {
	for _, g := range gs {
		eyebrows = append(eyebrows, g.Eyebrow)
		for _, it := range g.Items {
			labels = append(labels, it.Label)
		}
	}
	return
}

func TestNavGroupsAdmin(t *testing.T) {
	gs := navGroups(navInput{Admin: true, Active: "status", Conflicts: 1, Firmware: 2, Pending: 1, ActiveRuns: 3, ShowRuns: true,
		UpdateAvailable: true, StatusProblem: "Disk 92 %", NewReports: 2})
	eb, labels := navLabels(gs)
	if strings.Join(eb, "|") != "|SETUP|ADMIN" {
		t.Fatalf("eyebrows %q", eb)
	}
	if got := strings.Join(labels, ","); got != "Library,Saves,Runs,Plugins,Systems & Cores,Clients,Users,Status,Reports,Settings" {
		t.Fatalf("order %s", got)
	}
	badge := map[string]string{}
	for _, g := range gs {
		for _, it := range g.Items {
			badge[it.ID] = it.Badge
		}
	}
	want := map[string]string{"saves": "1 conflict", "runs": "3 active", "systems": "2 issues", "clients": "1 request", "status": "Disk 92 %", "reports": "2 new", "settings": "update"}
	for id, w := range want {
		if badge[id] != w {
			t.Fatalf("badge of %s = %q, want %q", id, badge[id], w)
		}
	}
	active := ""
	for _, g := range gs {
		for _, it := range g.Items {
			if it.Active {
				active += it.ID
			}
		}
	}
	if active != "status" {
		t.Fatalf("active %q", active)
	}
	// Without a problem the Status item has no badge; Runs is hidden without the feature.
	_, labels = navLabels(navGroups(navInput{Admin: true}))
	if strings.Contains(strings.Join(labels, ","), "Runs") {
		t.Fatal("Runs only with the feature")
	}
}

func TestNavGroupsMember(t *testing.T) {
	gs := navGroups(navInput{Admin: false, Active: "library", Conflicts: 2, Firmware: 5, Pending: 4, ShowRuns: true, StatusProblem: "Disk 99 %"})
	eb, labels := navLabels(gs)
	if got := strings.Join(labels, ","); got != "Library,Saves,Runs,Plugins,Settings" {
		t.Fatalf("a member sees only Play and Settings: %s", got)
	}
	for _, e := range eb {
		if e == "SETUP" || e == "ADMIN" {
			t.Fatalf("member sees the group %s", e)
		}
	}
	for _, g := range gs {
		for _, it := range g.Items {
			if it.ID == "status" || it.ID == "systems" || it.ID == "clients" || it.ID == "users" {
				t.Fatalf("admin item %s for a member", it.ID)
			}
			if it.Badge == "Disk 99 %" || strings.Contains(it.Badge, "issue") || strings.Contains(it.Badge, "request") {
				t.Fatalf("admin badge %q for a member", it.Badge)
			}
		}
	}
}

func TestNavRenderedInSidebar(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/library", nil)
	status(t, rec, 200)
	contains(t, rec, `class="nav-eyebrow" aria-hidden="true">SETUP<`, `>ADMIN<`, `href="/status"`, `href="/systems"`, `href="/settings"`, "Systems &amp; Cores")
	body := rec.Body.String()
	lib, sys, st, set := strings.Index(body, `href="/library"`), strings.Index(body, `href="/systems"`), strings.Index(body, `href="/status"`), strings.Index(body, `href="/settings"`)
	if !(lib < sys && sys < st && st < set) {
		t.Fatalf("order library %d systems %d status %d settings %d", lib, sys, st, set)
	}
	// The nav fragment (badge refresh) keeps the groups and marks Status active.
	rec = c.get("/nav-fragment", map[string]string{"HX-Current-URL": "http://hub/status"})
	status(t, rec, 200)
	contains(t, rec, "SETUP", "ADMIN", `class="active" aria-current="page" hx-get="/status"`)
}

func TestMemberNavTemplate(t *testing.T) {
	e := newEnv(t, true, nil)
	srv, err := New(e.svc, Config{}, nil)
	if err != nil {
		t.Fatal(err)
	}
	d := pageData{Title: "x", Nav: "library", Theme: "light", HubName: "Home", HubVersion: "v1", User: "Lena", Role: "User",
		NavGroups: navGroups(navInput{Admin: false, Active: "library", ShowRuns: true})}
	rec := httptest.NewRecorder()
	srv.render(rec, 200, "notfound", "nav-fragment", d)
	for _, bad := range []string{"SETUP", "ADMIN", "/status", "/systems", "/clients", "/users"} {
		if strings.Contains(rec.Body.String(), bad) {
			t.Fatalf("%q rendered for a member: %s", bad, rec.Body.String())
		}
	}
	for _, good := range []string{"/library", "/saves", "/runs", "/plugins", "/settings"} {
		if !strings.Contains(rec.Body.String(), good) {
			t.Fatalf("%q missing for a member", good)
		}
	}
}

func TestSettingsNavGroups(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/settings/general", nil)
	status(t, rec, 200)
	body := rec.Body.String()
	order := []string{">HUB<", `href="/settings/general"`, `href="/settings/network"`, `href="/settings/security"`, `href="/settings/updates"`,
		`href="/settings/diagnostics"`, ">DATA<", `href="/settings/saves"`, ">INTEGRATIONS<", `href="/settings/metrics"`}
	last := -1
	for _, o := range order {
		i := strings.Index(body, o)
		if i < 0 || i < last {
			t.Fatalf("%q missing or out of order (%d after %d)", o, i, last)
		}
		last = i
	}
	contains(t, rec, "Network &amp; TURN", "Saves retention")
	// Existing routes keep working.
	for _, p := range []string{"/settings", "/settings/updates", "/settings/network", "/settings/saves", "/settings/diagnostics", "/settings/metrics"} {
		status(t, c.get(p, nil), 200)
	}
	status(t, c.get("/settings/nope", nil), 404)
}

func TestProblemSelection(t *testing.T) {
	// Disk: below 90 % nothing, above a warning, above 97 % an error with the used share in the title.
	if len(diskProblems(1000, 150)) != 0 {
		t.Fatal("85 % used is fine")
	}
	p := diskProblems(1000, 80)
	if len(p) != 1 || p[0].Title != "Disk 92 %" || p[0].Severity != health.Warn {
		t.Fatalf("%+v", p)
	}
	if p = diskProblems(1000, 20); p[0].Severity != health.Error {
		t.Fatalf("98 %% used is an error: %+v", p)
	}

	// TURN failing and a failed update: the error wins, the update failure is still listed.
	e := newEnv(t, true, func(c *Config) {
		c.Disk = func(string) (uint64, uint64, bool) { return 1000, 500, true }
		c.Net.Issues = []NetIssue{{Key: config.NetTURN, Value: "x", Message: "The built-in TURN relay could not be started."}}
	})
	srv, err := New(e.svc, e.cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join(e.svc.DataDir(), updates.UpdatesDir)
	if err := os.MkdirAll(dir, 0o750); err != nil {
		t.Fatal(err)
	}
	failed := `{"version":"9.9.9","time":"2026-10-20T10:00:00Z","ok":false,"message":"checksum mismatch"}`
	if err := os.WriteFile(filepath.Join(dir, updates.LastResultName), []byte(failed), 0o640); err != nil {
		t.Fatal(err)
	}
	var ids []string
	ps := srv.problems(bg)
	for _, x := range ps {
		ids = append(ids, x.ID)
	}
	if strings.Join(ids, ",") != "turn,update" {
		t.Fatalf("ids %v", ids)
	}
	if mu := health.MostUrgent(ps); mu == nil || mu.ID != "turn" || mu.Severity != health.Error {
		t.Fatalf("most urgent %+v", mu)
	}
	if !strings.Contains(ps[1].Detail, "9.9.9") || !strings.Contains(ps[1].Detail, "checksum mismatch") {
		t.Fatalf("update detail %q", ps[1].Detail)
	}
	// Without the TURN issue the update failure is the banner; a successful install clears it.
	plain, _ := New(e.svc, Config{Disk: e.cfg.Disk}, nil)
	if mu := health.MostUrgent(plain.problems(bg)); mu == nil || mu.ID != "update" || mu.Title != "Update failed" {
		t.Fatalf("most urgent %+v", mu)
	}
	ok := `{"version":"9.9.9","time":"2026-10-20T10:00:00Z","ok":true,"message":""}`
	if err := os.WriteFile(filepath.Join(dir, updates.LastResultName), []byte(ok), 0o640); err != nil {
		t.Fatal(err)
	}
	for _, x := range plain.problems(bg) {
		if x.ID == "update" {
			t.Fatal("a successful install is not a problem")
		}
	}
}

func TestStatusPage(t *testing.T) {
	ring := diag.NewErrorRing(50)
	logRing := diag.NewRing(100, 1<<16)
	e := newEnv(t, true, func(c *Config) {
		c.Errors, c.Log = ring, logRing
		c.Disk = func(string) (uint64, uint64, bool) { return 1000, 500, true }
		c.PublicHost, c.PublicPort = "hub.example.com", 8443
		c.Net.Issues = []NetIssue{{Key: config.NetTURN, Value: "x", Message: "The built-in TURN relay could not be started."}}
	})
	// Anonymous: sent to the login page.
	anon := e.client()
	if rec := anon.get("/status", nil); rec.Code != 303 || location(rec) != "/login" {
		t.Fatalf("anonymous: %d %q", rec.Code, location(rec))
	}
	if rec := anon.get("/status/log", nil); rec.Code != 303 {
		t.Fatalf("anonymous log: %d", rec.Code)
	}

	ring.Add(diag.Entry{Time: time.Now().Add(-30 * time.Hour), Level: slog.LevelError, Message: "ancient"})
	log := slog.New(diag.NewCaptureHandler(slog.NewTextHandler(logRing, nil), ring))
	log.Error("database slow", "token", "supersecretvalue", "ms", 900)
	log.Warn("update check failed")

	c := e.client()
	c.login()
	rec := c.get("/status", nil)
	status(t, rec, 200)
	contains(t, rec, "<h1>Status</h1>", "hub.example.com:8443", "CPU", "Memory", "Disk · data",
		"TURN relay down", "The built-in TURN relay could not be started.", `href="/settings/network"`, "Open Network", // problem banner
		"ACTIVE SESSIONS · 0 · 0 VIEWERS", "No Session is running.", "CONNECTED PLAYERS · 0", "No Player is connected.",
		"LAST ERRORS · 24 H", "database slow", "update check failed", "Download log", `href="/status/log"`,
		"METRICS ENDPOINT", "Off", "http://hub.example.com:8443/metrics",
		`hx-trigger="every 10s"`, `id="status-body"`, "Updated ")
	notContains(t, rec, "ancient", "supersecretvalue", "Stream")
	contains(t, rec, "[redacted]")
	// The sidebar badge carries the same problem title.
	contains(t, rec, `<span class="badge">TURN relay down</span>`)

	// The poll gets only the body region.
	rec = c.get("/status", hxHdr("status-body"))
	status(t, rec, 200)
	contains(t, rec, `id="status-body"`)
	notContains(t, rec, "<html", "<aside")

	// The log download is plain text, redacted, not cached.
	logRing.Write([]byte("level=INFO msg=hello password=hunter2hunter2 Authorization: Bearer abcdefghijklmnop\n"))
	rec = c.get("/status/log", nil)
	status(t, rec, 200)
	if ct := rec.Header().Get("Content-Type"); !strings.HasPrefix(ct, "text/plain") || !strings.Contains(rec.Header().Get("Content-Disposition"), "attachment") {
		t.Fatalf("headers %q %q", ct, rec.Header().Get("Content-Disposition"))
	}
	contains(t, rec, "msg=hello", "[redacted]")
	notContains(t, rec, "hunter2hunter2", "abcdefghijklmnop")
}

func TestStatusPageWithoutProblem(t *testing.T) {
	e := newEnv(t, true, func(c *Config) {
		c.Errors, c.Log = diag.NewErrorRing(5), diag.NewRing(5, 1024)
		c.Disk = func(string) (uint64, uint64, bool) { return 1000, 500, true }
	})
	c := e.client()
	c.login()
	rec := c.get("/status", nil)
	status(t, rec, 200)
	notContains(t, rec, "status-banner")
	contains(t, rec, "No warnings or errors in the last 24 hours.")
}

// ---- metrics ----

func TestMetricsAuth(t *testing.T) {
	e := newEnv(t, true, nil)
	anon := e.client()
	hdr := func(tok string) map[string]string { return map[string]string{"Authorization": "Bearer " + tok} }

	// Off by default: 404 for everyone, with or without a token.
	status(t, anon.get("/metrics", nil), 404)
	status(t, anon.get("/metrics", hdr("fbm_anything")), 404)

	// Turn it on in Settings: the token appears once in the response.
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/metrics", nil)
	status(t, rec, 200)
	contains(t, rec, "Turn on", "Off: the address does not exist")
	rec = postTok(c, tok, "/settings/metrics", map[string][]string{"enabled": {"1"}})
	status(t, rec, 200)
	m := regexpToken.FindString(rec.Body.String())
	if m == "" {
		t.Fatalf("token not shown: %.600s", rec.Body.String())
	}
	contains(t, rec, "Your metrics token", "shown only once", "Turn off", "Create new token", "/metrics")
	notContains(t, rec, "Stream")
	// A second visit does not show it again.
	rec = c.get("/settings/metrics", nil)
	notContains(t, rec, m, "Your metrics token")
	contains(t, rec, "A token is set")

	// 401 without or with a wrong token (also the admin web session is no key), 200 with the right one.
	for _, h := range []map[string]string{nil, hdr("fbm_wrong"), hdr(m + "x"), {"Authorization": "Basic " + m}, {"Authorization": "Bearer"}} {
		rec := anon.get("/metrics", h)
		status(t, rec, 401)
		if rec.Header().Get("WWW-Authenticate") == "" {
			t.Fatal("401 needs WWW-Authenticate")
		}
	}
	status(t, c.get("/metrics", nil), 401)
	rec = anon.get("/metrics", hdr(m))
	status(t, rec, 200)
	if ct := rec.Header().Get("Content-Type"); ct != "text/plain; version=0.0.4; charset=utf-8" {
		t.Fatalf("content type %q", ct)
	}
	if rec.Header().Get("Cache-Control") != "no-store" {
		t.Fatal("no-store")
	}
	notContains(t, rec, m)

	// Regenerate: the old token stops, the new one works.
	rec = postTok(c, tok, "/settings/metrics/token", nil)
	status(t, rec, 200)
	n := regexpToken.FindString(rec.Body.String())
	if n == "" || n == m {
		t.Fatalf("new token %q", n)
	}
	status(t, anon.get("/metrics", hdr(m)), 401)
	status(t, anon.get("/metrics", hdr(n)), 200)

	// CSRF is required on both actions; anonymous cannot switch.
	status(t, c.postForm("/settings/metrics", map[string][]string{"enabled": {"0"}}, nil), 403)
	status(t, c.postForm("/settings/metrics/token", nil, nil), 403)
	if rec := anon.postForm("/settings/metrics", map[string][]string{"enabled": {"0"}}, nil); rec.Code != 303 {
		t.Fatalf("anonymous switch: %d", rec.Code)
	}
	status(t, anon.get("/metrics", hdr(n)), 200)

	// Off again: 404 even with the right token.
	status(t, postTok(c, tok, "/settings/metrics", map[string][]string{"enabled": {"0"}}), 200)
	status(t, anon.get("/metrics", hdr(n)), 404)
}

func TestMetricsFormat(t *testing.T) {
	ring := diag.NewErrorRing(5)
	e := newEnv(t, true, func(c *Config) { c.Errors = ring })
	ring.Add(diag.Entry{Time: time.Now(), Level: slog.LevelError, Message: "boom"})
	ring.Add(diag.Entry{Time: time.Now(), Level: slog.LevelWarn, Message: "careful"})
	tok, err := e.svc.SetMetricsEnabled(bg, true)
	if err != nil {
		t.Fatal(err)
	}
	rec := e.client().get("/metrics", map[string]string{"Authorization": "bearer " + tok})
	status(t, rec, 200)
	body := rec.Body.String()
	contains(t, rec,
		"# HELP framebeam_hub_uptime_seconds ", "# TYPE framebeam_hub_uptime_seconds gauge\n",
		"# TYPE framebeam_sessions_active gauge\nframebeam_sessions_active 0\n",
		"framebeam_session_viewers 0\n", "framebeam_players_connected 0\n", "framebeam_turn_enabled 0\n",
		"# TYPE framebeam_log_records_total counter\n", `framebeam_log_records_total{level="error"} 1`+"\n", `framebeam_log_records_total{level="warn"} 1`+"\n",
		"framebeam_process_memory_bytes ", `framebeam_hub_info{version="`)
	notContains(t, rec, "framebeam_turn_allocations") // no TURN relay running
	if !strings.HasSuffix(body, "\n") {
		t.Fatal("exposition ends with a newline")
	}
	// Every sample line is "name[{labels}] value" and every family has HELP and TYPE before its samples.
	typed := map[string]bool{}
	for _, line := range strings.Split(strings.TrimSuffix(body, "\n"), "\n") {
		switch {
		case strings.HasPrefix(line, "# HELP "):
		case strings.HasPrefix(line, "# TYPE "):
			typed[strings.Fields(line)[2]] = true
		default:
			f := strings.Fields(line)
			if len(f) != 2 {
				t.Fatalf("sample line %q", line)
			}
			name, _, _ := strings.Cut(f[0], "{")
			if !typed[name] {
				t.Fatalf("sample %q before its TYPE line", line)
			}
		}
	}
}

func TestPromHelpers(t *testing.T) {
	if promLabel("a\"b\\c\nd") != `a\"b\\c\nd` {
		t.Fatalf("%q", promLabel("a\"b\\c\nd"))
	}
	if promFloat(3) != "3" || promFloat(0.25) != "0.25" || promFloat(1e9) != "1000000000" {
		t.Fatalf("%s %s %s", promFloat(3), promFloat(0.25), promFloat(1e9))
	}
	var p promWriter
	p.metric("x_total", "counter", "help", `a="1"`, 2)
	if p.buf.String() != "# HELP x_total help\n# TYPE x_total counter\nx_total{a=\"1\"} 2\n" {
		t.Fatalf("%q", p.buf.String())
	}
	if formatDuration(30*time.Second) != "under 1 min" || formatDuration(12*24*time.Hour+4*time.Hour) != "12 days 4 h" ||
		formatDuration(3*time.Hour+12*time.Minute) != "3 h 12 min" || formatDuration(5*time.Minute) != "5 min" {
		t.Fatal("formatDuration")
	}
}

var regexpToken = regexp.MustCompile(`fbm_[A-Za-z0-9_-]{20,}`)
