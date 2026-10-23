package serverhost

import (
	"context"
	"errors"
	"net"
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/adminhost"
	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

var bg = context.Background()

func baseNet() config.Config {
	return config.Config{Listen: ":8443", TURNPort: config.DefaultTURNPort, TURNRelayPorts: config.DefaultTURNRelayPorts,
		SaveKeepRecent: 20, SaveKeepDaily: 30, SaveKeepWeekly: 26}
}

// newHost opens a service and a host with the base network configuration; restarts counts restart requests.
func newHost(t *testing.T, mod func(*Config)) (*Server, *hub.Service, *int) {
	t.Helper()
	svc, _, _ := hubtest.OpenIn(t, t.TempDir(), nil)
	restarts := new(int)
	cfg := Config{Listen: ":8443", Net: NetConfig{Base: baseNet(), Running: baseNet(), RequestRestart: func() { *restarts++ }}}
	if mod != nil {
		mod(&cfg)
	}
	return New(svc, cfg, nil), svc, restarts
}

func val(v string) url.Values { return url.Values{"value": {v}} }

func freePort(t *testing.T) int {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	return ln.Addr().(*net.TCPAddr).Port
}

func isValidation(err error) bool {
	var ve *adminhost.ValidationError
	return errors.As(err, &ve)
}

func TestNetSavePrecedenceResetAndPending(t *testing.T) {
	h, svc, _ := newHost(t, nil)
	st, err := h.NetState(bg)
	if err != nil || len(st.Stored) != 0 || len(st.Pending) != 0 || st.Desired.TURNPort != config.DefaultTURNPort || !st.CanRestart {
		t.Fatalf("%+v %v", st, err)
	}
	if err := h.SaveNet(bg, config.NetTURNPort, val("3500")); err != nil {
		t.Fatal(err)
	}
	st, _ = h.NetState(bg)
	if st.Stored[config.NetTURNPort] != "3500" || st.Desired.TURNPort != 3500 || st.Running.TURNPort != config.DefaultTURNPort {
		t.Fatalf("%+v", st)
	}
	if len(st.Pending) != 1 || st.Pending[0] != config.NetLabel(config.NetTURNPort) {
		t.Fatalf("pending: %v", st.Pending)
	}
	if err := h.ResetNet(bg, config.NetTURNPort); err != nil {
		t.Fatal(err)
	}
	if ov, _ := svc.NetOverrides(bg); len(ov) != 0 {
		t.Fatalf("%v", ov)
	}
	st, _ = h.NetState(bg)
	if len(st.Pending) != 0 {
		t.Fatalf("pending after reset: %v", st.Pending)
	}
	// A listen port that can be bound keeps the host part and is pending.
	p := freePort(t)
	if err := h.SaveNet(bg, config.NetListenPort, val(strconv.Itoa(p))); err != nil {
		t.Fatal(err)
	}
	st, _ = h.NetState(bg)
	if st.Desired.ListenPort() != p || len(st.Pending) != 1 {
		t.Fatalf("%d %v", st.Desired.ListenPort(), st.Pending)
	}
}

func TestNetSaveValidation(t *testing.T) {
	h, _, _ := newHost(t, nil)
	if err := h.SaveNet(bg, "nope", val("1")); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown key: %v", err)
	}
	if err := h.ResetNet(bg, "nope"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown key: %v", err)
	}
	for _, c := range []struct{ key, v string }{
		{config.NetTURNPort, "abc"}, {config.NetListenPort, "80"}, {config.NetKeepRecent, "-1"},
	} {
		if err := h.SaveNet(bg, c.key, val(c.v)); !isValidation(err) {
			t.Errorf("%s=%s: %v", c.key, c.v, err)
		}
	}
	// A listen port that is in use is refused.
	busy, err := net.Listen("tcp", ":0") // wildcard like the test bind
	if err != nil {
		t.Fatal(err)
	}
	defer busy.Close()
	bp := busy.Addr().(*net.TCPAddr).Port
	err = h.SaveNet(bg, config.NetListenPort, val(strconv.Itoa(bp)))
	if !isValidation(err) || !strings.Contains(err.Error(), "in use") {
		t.Fatalf("%v", err)
	}
}

func TestNetLiveSettings(t *testing.T) {
	h, svc, restarts := newHost(t, nil)
	if r, d, w := svc.SaveRetention(); r != 0 || d != 0 || w != 0 {
		t.Fatalf("%d %d %d", r, d, w)
	}
	if err := h.SaveNet(bg, config.NetKeepRecent, val("3")); err != nil {
		t.Fatal(err)
	}
	if r, d, w := svc.SaveRetention(); r != 3 || d != 30 || w != 26 {
		t.Fatalf("retention applied live: %d %d %d", r, d, w)
	}
	if err := h.ResetNet(bg, config.NetKeepRecent); err != nil {
		t.Fatal(err)
	}
	if r, _, _ := svc.SaveRetention(); r != 20 {
		t.Fatal(r)
	}
	form := func(op, u string) url.Values { return url.Values{"op": {op}, "url": {u}} }
	if err := h.SaveNet(bg, config.NetICEServers, form("add", "stun:stun.example.com:3478")); err != nil {
		t.Fatal(err)
	}
	if got := svc.ICEServers(); len(got) != 1 || got[0] != "stun:stun.example.com:3478" {
		t.Fatalf("%v", got)
	}
	if err := h.SaveNet(bg, config.NetICEServers, form("add", "stun:stun.example.com:3478")); !isValidation(err) {
		t.Fatalf("duplicate: %v", err)
	}
	if err := h.SaveNet(bg, config.NetICEServers, form("remove", "stun:stun.example.com:3478")); err != nil {
		t.Fatal(err)
	}
	if got := svc.ICEServers(); len(got) != 1 || got[0] != config.DefaultSTUNServer {
		t.Fatalf("removing the last entry must give the default: %v", got)
	}
	if err := h.SaveNet(bg, config.NetICEServers, url.Values{"op": {"none"}}); err != nil {
		t.Fatal(err)
	}
	if got := svc.ICEServers(); got == nil || len(got) != 0 {
		t.Fatalf("none: %#v", got)
	}
	if *restarts != 0 {
		t.Fatal("restart requested by a live setting")
	}
}

func TestNetResetValidatesResult(t *testing.T) {
	h, svc, _ := newHost(t, nil)
	if err := h.SaveNet(bg, config.NetPublicHost, val("hub.example.com")); err != nil {
		t.Fatal(err)
	}
	if err := h.SaveNet(bg, config.NetTURN, val("1")); err != nil {
		t.Fatal(err)
	}
	// The base public host is empty: resetting it would leave TURN on without a host.
	err := h.ResetNet(bg, config.NetPublicHost)
	if !isValidation(err) || !strings.Contains(err.Error(), "needs a public host") {
		t.Fatalf("%v", err)
	}
	if ov, _ := svc.NetOverrides(bg); ov[config.NetPublicHost] != "hub.example.com" {
		t.Fatalf("override removed despite the error: %v", ov)
	}
	if err := h.ResetNet(bg, config.NetTURN); err != nil {
		t.Fatal(err)
	}
	if err := h.ResetNet(bg, config.NetPublicHost); err != nil {
		t.Fatal(err)
	}

	// A base listen port that is occupied now cannot be restored.
	busy, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Fatal(err)
	}
	defer busy.Close()
	bp := busy.Addr().(*net.TCPAddr).Port
	h2, svc2, _ := newHost(t, func(c *Config) { c.Net.Base.Listen = "127.0.0.1:" + strconv.Itoa(bp) })
	if err := h2.SaveNet(bg, config.NetListenPort, val(strconv.Itoa(freePort(t)))); err != nil {
		t.Fatal(err)
	}
	err = h2.ResetNet(bg, config.NetListenPort)
	if !isValidation(err) || !strings.Contains(err.Error(), "Port "+strconv.Itoa(bp)+" is in use") {
		t.Fatalf("%v", err)
	}
	if ov, _ := svc2.NetOverrides(bg); ov[config.NetListenPort] == "" {
		t.Fatalf("override removed: %v", ov)
	}
}

func TestNetStartupIssues(t *testing.T) {
	h, svc, _ := newHost(t, func(c *Config) {
		c.Net.Issues = []NetIssue{
			{Key: config.NetListenPort, Value: "9999", Message: "Saved port 9999 could not be used"},
			{Key: config.NetTURNPort, Value: "1111", Message: "stale issue"},
		}
	})
	st, _ := h.NetState(bg)
	if len(st.Issues) != 0 {
		t.Fatalf("issue shown without its saved value: %v", st.Issues)
	}
	svc.SetNetOverride(bg, config.NetListenPort, "9999")
	st, _ = h.NetState(bg)
	if len(st.Issues) != 1 || st.Issues[0] != "Saved port 9999 could not be used" {
		t.Fatalf("%v", st.Issues)
	}
	if len(st.Pending) != 0 { // already failed at startup: not pending
		t.Fatalf("%v", st.Pending)
	}
	// Changing the value clears the issue and makes the change pending again.
	if err := h.SaveNet(bg, config.NetListenPort, val(strconv.Itoa(freePort(t)))); err != nil {
		t.Fatal(err)
	}
	st, _ = h.NetState(bg)
	if len(st.Issues) != 0 || len(st.Pending) != 1 {
		t.Fatalf("%v %v", st.Issues, st.Pending)
	}
}

func TestPrepareRestart(t *testing.T) {
	h, _, restarts := newHost(t, func(c *Config) { c.PublicHost = "hub.example.org" })
	rb, restart, err := h.PrepareRestart(bg, "admin")
	if err != nil || rb.PortChanged || rb.URL != "http://hub.example.org:8443" {
		t.Fatalf("%+v %v", rb, err)
	}
	if *restarts != 0 {
		t.Fatal("restart before the caller asked for it")
	}
	restart()
	if *restarts != 1 {
		t.Fatalf("restarts: %d", *restarts)
	}
	p := freePort(t)
	if err := h.SaveNet(bg, config.NetListenPort, val(strconv.Itoa(p))); err != nil {
		t.Fatal(err)
	}
	rb, _, _ = h.PrepareRestart(bg, "admin")
	if !rb.PortChanged || rb.URL != "http://hub.example.org:"+strconv.Itoa(p) {
		t.Fatalf("%+v", rb)
	}
	// With TLS the scheme is https; without a public host a concrete listen host is used, a wildcard gives no URL.
	h2, _, _ := newHost(t, func(c *Config) { c.UseTLS = true; c.Listen = "192.0.2.7:8443" })
	if rb, _, _ := h2.PrepareRestart(bg, "admin"); rb.URL != "https://192.0.2.7:8443" {
		t.Fatalf("%+v", rb)
	}
	if rb, _, _ := h.PrepareRestart(bg, "admin"); strings.Contains(rb.URL, "evil") {
		t.Fatal(rb.URL)
	}
	h3, _, _ := newHost(t, nil)
	if rb, _, _ := h3.PrepareRestart(bg, "admin"); rb.URL != "" {
		t.Fatalf("wildcard listen must give no URL: %q", rb.URL)
	}
}

func TestPrepareRestartUnsupported(t *testing.T) {
	h, _, _ := newHost(t, func(c *Config) { c.Net.RequestRestart = nil })
	if _, _, err := h.PrepareRestart(bg, "admin"); !errors.Is(err, adminhost.ErrUnsupported) {
		t.Fatalf("%v", err)
	}
	if st, _ := h.NetState(bg); st.CanRestart {
		t.Fatal("CanRestart without a hook")
	}
}

func TestRenewCert(t *testing.T) {
	calls := 0
	fp, exp := "AA:BB:OLD", time.Now().AddDate(0, 1, 0)
	renew := func() (string, time.Time, error) {
		calls++
		fp, exp = "AA:BB:NEW", time.Now().AddDate(10, 0, 0)
		return fp, exp, nil
	}
	h, _, _ := newHost(t, func(c *Config) {
		c.UseTLS, c.CertSource, c.RenewCert = true, "Self-generated", renew
		c.CertState = func() (string, time.Time) { return fp, exp }
	})
	if sec := h.Security(); !sec.TLS || !sec.CanRenew || sec.Fingerprint != "AA:BB:OLD" {
		t.Fatalf("%+v", sec)
	}
	got, _, err := h.RenewCert("admin")
	if err != nil || got != "AA:BB:NEW" || calls != 1 {
		t.Fatalf("%q %v %d", got, err, calls)
	}
	if sec := h.Security(); sec.Fingerprint != "AA:BB:NEW" { // live state, not the value from startup
		t.Fatalf("%+v", sec)
	}
	// A failing renewal is reported; an own certificate or plain HTTP cannot renew.
	bad, _, _ := newHost(t, func(c *Config) {
		c.CertSource = "Self-generated"
		c.RenewCert = func() (string, time.Time, error) { return "", time.Time{}, errors.New("disk full") }
	})
	if _, _, err := bad.RenewCert("admin"); err == nil || errors.Is(err, adminhost.ErrUnsupported) {
		t.Fatalf("%v", err)
	}
	own, _, _ := newHost(t, func(c *Config) {
		c.CertSource, c.RenewCert, c.CertFingerprint = "Own cert/key", renew, "FF"
	})
	if _, _, err := own.RenewCert("admin"); !errors.Is(err, adminhost.ErrUnsupported) {
		t.Fatalf("%v", err)
	}
	if sec := own.Security(); sec.CanRenew || sec.Fingerprint != "FF" {
		t.Fatalf("%+v", sec)
	}
}

func TestStatusSourceAndImportFolder(t *testing.T) {
	h, _, _ := newHost(t, func(c *Config) {
		c.ImportDir = "/srv/import"
		c.Disk = func(string) (uint64, uint64, bool) { return 100 << 30, 50 << 30, true }
	})
	if dir, max := h.ImportFolder(); dir != "/srv/import" || max != DefaultMaxUploadBytes {
		t.Fatalf("%q %d", dir, max)
	}
	if h.StatusSource() == nil || h.RunningConfig().Listen != ":8443" {
		t.Fatal("status source or running config missing")
	}
	h.StatusSource().Problems(bg) // must not panic with the test disk reader
}
