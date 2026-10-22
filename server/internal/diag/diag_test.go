package diag

import (
	"archive/zip"
	"bytes"
	"context"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

// Planted secrets: none of them may appear in a bundle.
var secrets = []string{
	"s3cretTurnValue", "hunter2pass", "tok_ABCDEF1234567890", "INVITECODE123456", "cookieValue987654",
	"fbd_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "turnUserBob", "turnCredXYZ", "Zm9vOmJhcg==", "basicpw", "pairingcode99",
}

const fakeLog = `time=2026-10-11T10:00:00Z level=INFO msg="request" method=GET path=/api/v1/hub status=200 dur_ms=3
time=2026-10-11T10:00:01Z level=INFO msg="TURN" turn_secret=s3cretTurnValue username=turnUserBob credential=turnCredXYZ host=192.168.1.20
time=2026-10-11T10:00:02Z level=WARN msg="login" password=hunter2pass user=admin
{"access_token":"tok_ABCDEF1234567890","password": "hunter2pass","status_code":404}
Authorization: Bearer fbd_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
Authorization: Basic Zm9vOmJhcg==
Cookie: fb_session=cookieValue987654; fb_csrf=cookieValue987654
Set-Cookie: __Host-fb_session=cookieValue987654; Path=/; Secure
link https://hub.lan/invite#INVITECODE123456 and https://u:basicpw@host/x?code=pairingcode99&a=1
ice turn:turnUserBob:turnCredXYZ@relay.example.org:3478
plain line exit_code=2 host=nas.local ip=10.0.0.7 basic configuration loaded
`

func TestRedactRemovesSecretsKeepsHosts(t *testing.T) {
	out := string(Redact([]byte(fakeLog)))
	for _, s := range secrets {
		if strings.Contains(out, s) {
			t.Errorf("secret %q survived:\n%s", s, out)
		}
	}
	for _, keep := range []string{"192.168.1.20", "10.0.0.7", "nas.local", "relay.example.org", "hub.lan", "status=200", `"status_code":404`,
		"exit_code=2", "user=admin", "basic configuration", "path=/api/v1/hub"} {
		if !strings.Contains(out, keep) {
			t.Errorf("%q was removed:\n%s", keep, out)
		}
	}
}

func writeLogs(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(dir, 0o750); err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	for _, f := range []struct {
		name string
		age  time.Duration
	}{{"hub.log", 0}, {"hub.log.1", time.Hour}} {
		p := filepath.Join(dir, f.name)
		if err := os.WriteFile(p, []byte(fakeLog), 0o640); err != nil {
			t.Fatal(err)
		}
		if err := os.Chtimes(p, now.Add(-f.age), now.Add(-f.age)); err != nil {
			t.Fatal(err)
		}
	}
}

func readZip(t *testing.T, b []byte) map[string]string {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatal(err)
	}
	m := map[string]string{}
	for _, f := range zr.File {
		rc, err := f.Open()
		if err != nil {
			t.Fatal(err)
		}
		data, _ := io.ReadAll(rc)
		rc.Close()
		m[f.Name] = string(data)
	}
	return m
}

func TestBundleEntriesInfoAndRedaction(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	dir := filepath.Join(t.TempDir(), "logs")
	writeLogs(t, dir)
	cfg := config.Config{DataDir: t.TempDir(), Listen: ":8443", PublicHost: "hub.example.org", TURN: true, TURNPort: 3478,
		TURNRelayPorts: "49160-49199", ICEServers: []string{"stun:stun.example.org:3478", "turn:turnUserBob:turnCredXYZ@relay.example.org:3478"}}
	o := Options{Config: cfg, LogDir: dir, NoJournal: true}
	info, err := Collect(context.Background(), svc, o)
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	if err := Write(context.Background(), &buf, info, o); err != nil {
		t.Fatal(err)
	}
	m := readZip(t, buf.Bytes())
	for _, name := range []string{EntryReadme, EntryInfo, "logs/hub.log", "logs/hub.log.1"} {
		if _, ok := m[name]; !ok {
			t.Errorf("missing entry %s (have %v)", name, keys(m))
		}
	}
	if len(m) != 4 {
		t.Errorf("entries: %v", keys(m))
	}
	for name, content := range m {
		for _, s := range secrets {
			if strings.Contains(content, s) {
				t.Errorf("%s contains secret %q", name, s)
			}
		}
	}
	if !strings.Contains(m["logs/hub.log"], "192.168.1.20") {
		t.Error("IP address removed from the log")
	}
	var got map[string]any
	if err := json.Unmarshal([]byte(m[EntryInfo]), &got); err != nil {
		t.Fatal(err)
	}
	for _, k := range []string{"info_version", "generated_at", "mode", "hub", "system", "network", "cores", "counts", "features"} {
		if _, ok := got[k]; !ok {
			t.Errorf("hub-info.json lacks %q", k)
		}
	}
	if got["mode"] != "running" {
		t.Errorf("mode %v", got["mode"])
	}
	net := got["network"].(map[string]any)
	if net["public_host"] != "hub.example.org" || net["turn_configured"] != true || net["turn_running"] != false || net["listen_port"].(float64) != 8443 {
		t.Errorf("network: %v", net)
	}
	hubm := got["hub"].(map[string]any)
	if hubm["version"] == "" || hubm["go_version"] == "" || got["system"].(map[string]any)["os"] == "" {
		t.Errorf("hub/system: %v", got)
	}
	if got["counts"].(map[string]any)["users"] == nil {
		t.Errorf("counts: %v", got["counts"])
	}
	if strings.Contains(m[EntryInfo], "turnCredXYZ") || !strings.Contains(m[EntryInfo], "stun.example.org") {
		t.Errorf("ice servers: %s", m[EntryInfo])
	}
}

func TestOfflineBundleWithoutService(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "logs")
	writeLogs(t, dir)
	o := Options{Config: config.Config{DataDir: t.TempDir(), Listen: ":8443"}, LogDir: dir, NoJournal: true}
	info, err := Collect(context.Background(), nil, o)
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	if err := Write(context.Background(), &buf, info, o); err != nil {
		t.Fatal(err)
	}
	m := readZip(t, buf.Bytes())
	if !strings.Contains(m[EntryInfo], `"mode": "offline"`) || strings.Contains(m[EntryInfo], `"counts"`) {
		t.Errorf("offline info: %s", m[EntryInfo])
	}
}

func TestLogSizeCapNewestFirst(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "logs")
	if err := os.MkdirAll(dir, 0o750); err != nil {
		t.Fatal(err)
	}
	line := "time=x level=INFO msg=\"line %04d\" pad=0123456789012345678901234567890123456789\n"
	mk := func(name string, n int, age time.Duration) {
		var b strings.Builder
		for i := 0; i < n; i++ {
			b.WriteString(strings.Replace(line, "%04d", name+string(rune('a'+i%26)), 1))
		}
		p := filepath.Join(dir, name)
		os.WriteFile(p, []byte(b.String()), 0o640)
		os.Chtimes(p, time.Now().Add(-age), time.Now().Add(-age))
	}
	mk("hub.log", 100, 0) // newest, ~7.5 KB
	mk("hub.log.1", 100, 1*time.Hour)
	mk("hub.log.2", 100, 2*time.Hour)
	const max = 10 << 10
	o := Options{Config: config.Config{DataDir: t.TempDir()}, LogDir: dir, MaxLogBytes: max, NoJournal: true}
	var buf bytes.Buffer
	if err := Write(context.Background(), &buf, Info{}, o); err != nil {
		t.Fatal(err)
	}
	m := readZip(t, buf.Bytes())
	total := 0
	for n, c := range m {
		if strings.HasPrefix(n, LogsPrefix) {
			total += len(c)
		}
	}
	if total > max+200 { // + the cut marker
		t.Errorf("log content %d bytes exceeds cap %d", total, max)
	}
	if len(m["logs/hub.log"]) < 7000 || strings.Contains(m["logs/hub.log"], "older content cut") {
		t.Errorf("newest file must be complete: %d bytes", len(m["logs/hub.log"]))
	}
	if !strings.Contains(m["logs/hub.log.1"], "older content cut") || !strings.HasPrefix(m["logs/hub.log.1"], "[older content cut") {
		t.Errorf("second file must be cut to its newest lines")
	}
	if _, ok := m["logs/hub.log.2"]; ok {
		t.Error("oldest file must be left out when the cap is reached")
	}
	// The cut starts at a line boundary.
	for _, l := range strings.Split(strings.TrimSpace(m["logs/hub.log.1"]), "\n")[1:] {
		if !strings.HasPrefix(l, "time=x level=INFO") {
			t.Fatalf("partial line %q", l)
		}
	}
}

func TestFileName(t *testing.T) {
	got := FileName(time.Date(2026, 10, 11, 9, 8, 7, 0, time.FixedZone("x", 3600)))
	if got != "uzume-diagnostics-20261011T080807Z.zip" {
		t.Fatal(got)
	}
}

func keys(m map[string]string) []string {
	var k []string
	for n := range m {
		k = append(k, n)
	}
	return k
}

func TestRecentRingInBundle(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	ring := NewRing(3, 1<<20)
	for _, l := range []string{"l1 a=1\n", "l2 password=hunter2pass\n", "l3 host=10.1.1.1\n", "l4 ok\n"} {
		ring.Write([]byte(l))
	}
	o := Options{Config: config.Config{DataDir: t.TempDir(), Listen: ":8443"}, Recent: ring, NoJournal: true}
	info, err := Collect(context.Background(), svc, o)
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	if err := Write(context.Background(), &buf, info, o); err != nil {
		t.Fatal(err)
	}
	got := readZip(t, buf.Bytes())[EntryRecent]
	if strings.Contains(got, "l1 ") || strings.Contains(got, "hunter2pass") || !strings.Contains(got, "10.1.1.1") || !strings.Contains(got, "l4 ok") {
		t.Fatalf("recent.log: %q", got)
	}
}
