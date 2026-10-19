package web

import (
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/clientip"
)

// proxyEnv serves the web interface behind clientip.Middleware with the given trusted proxy list.
type proxyEnv struct {
	*env
	h http.Handler
}

func newProxyEnv(t *testing.T, withAdmin bool, trusted string) *proxyEnv {
	t.Helper()
	nets, err := clientip.ParseList(trusted)
	if err != nil {
		t.Fatal(err)
	}
	e := newEnv(t, withAdmin, func(c *Config) { c.TrustedProxy = strings.Split(trusted, ",") })
	return &proxyEnv{env: e, h: clientip.Middleware(nets)(e.mux)}
}

func (p *proxyEnv) do(method, path, peer, host string, hdr map[string]string, form url.Values, cookies ...*http.Cookie) *httptest.ResponseRecorder {
	var r *http.Request
	if form != nil {
		r = httptest.NewRequest(method, path, strings.NewReader(form.Encode()))
		r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	} else {
		r = httptest.NewRequest(method, path, nil)
	}
	r.RemoteAddr, r.Host = peer, host
	for k, v := range hdr {
		r.Header.Set(k, v)
	}
	for _, c := range cookies {
		r.AddCookie(c)
	}
	rec := httptest.NewRecorder()
	p.h.ServeHTTP(rec, r)
	return rec
}

func TestProxyHTTPSCookiesAndHSTS(t *testing.T) {
	p := newProxyEnv(t, true, "10.0.0.0/8")
	fwd := map[string]string{"X-Forwarded-For": "203.0.113.9", "X-Forwarded-Proto": "https"}

	rec := p.do("GET", "/login", "10.0.0.2:4000", "hub.example.org", fwd, nil)
	cs := rec.Result().Cookies()
	if len(cs) == 0 {
		t.Fatal("no cookie set")
	}
	for _, c := range cs {
		if !strings.HasPrefix(c.Name, hostPrefix) || !c.Secure || c.Path != "/" || c.Domain != "" || !c.HttpOnly {
			t.Fatalf("cookie %+v must be __Host-, Secure, Path=/, no Domain, HttpOnly", c)
		}
	}
	if got := rec.Header().Get("Strict-Transport-Security"); !strings.HasPrefix(got, "max-age=") {
		t.Fatalf("HSTS = %q", got)
	}

	// Forwarded proto http: neither Secure nor HSTS.
	rec = p.do("GET", "/login", "10.0.0.2:4000", "hub.example.org", map[string]string{"X-Forwarded-For": "203.0.113.9", "X-Forwarded-Proto": "http"}, nil)
	for _, c := range rec.Result().Cookies() {
		if c.Secure || strings.HasPrefix(c.Name, hostPrefix) {
			t.Fatalf("cookie %+v must not be Secure/__Host- for http", c)
		}
	}
	if rec.Header().Get("Strict-Transport-Security") != "" {
		t.Fatal("no HSTS for http")
	}

	// A peer outside the trusted list cannot claim https.
	rec = p.do("GET", "/login", "203.0.113.50:4000", "hub.example.org", fwd, nil)
	for _, c := range rec.Result().Cookies() {
		if c.Secure || strings.HasPrefix(c.Name, hostPrefix) {
			t.Fatalf("untrusted peer got cookie %+v", c)
		}
	}
	if rec.Header().Get("Strict-Transport-Security") != "" {
		t.Fatal("untrusted peer must not trigger HSTS")
	}
}

func TestProxyLoginWorksWithHostCookies(t *testing.T) {
	p := newProxyEnv(t, true, "10.0.0.0/8")
	fwd := map[string]string{"X-Forwarded-For": "203.0.113.9", "X-Forwarded-Proto": "https"}
	rec := p.do("GET", "/login", "10.0.0.2:1", "hub.example.org", fwd, nil)
	var csrf *http.Cookie
	for _, c := range rec.Result().Cookies() {
		if c.Name == hostPrefix+csrfCookie {
			csrf = c
		}
	}
	if csrf == nil {
		t.Fatal("no __Host- csrf cookie")
	}
	rec = p.do("POST", "/login", "10.0.0.2:1", "hub.example.org", fwd,
		url.Values{"username": {"admin"}, "password": {"secret-12345"}, "_csrf": {csrf.Value}}, csrf)
	if rec.Code != http.StatusSeeOther {
		t.Fatalf("login: %d", rec.Code)
	}
	found := false
	for _, c := range rec.Result().Cookies() {
		if c.Name == hostPrefix+sessionCookie && c.Secure {
			found = true
		}
	}
	if !found {
		t.Fatal("session cookie must be __Host- and Secure")
	}
}

func TestProxySetupAndLocalNeverReachable(t *testing.T) {
	// Trusting loopback (proxy on the same host) closes /setup even for requests that look local.
	p := newProxyEnv(t, false, "127.0.0.1/32")
	for _, hdr := range []map[string]string{nil, {"X-Forwarded-For": "203.0.113.9", "X-Forwarded-Proto": "https"}} {
		rec := p.do("GET", "/setup", "127.0.0.1:5000", "localhost", hdr, nil)
		if rec.Code != http.StatusForbidden && !strings.Contains(rec.Body.String(), "machine") {
			t.Fatalf("hdr %v: setup page code %d", hdr, rec.Code)
		}
		rec = p.do("POST", "/setup", "127.0.0.1:5000", "localhost", hdr, url.Values{"username": {"x"}, "password": {"secret-12345"}, "password2": {"secret-12345"}})
		if rec.Code != http.StatusForbidden || !strings.Contains(rec.Body.String(), "machine") {
			t.Fatalf("hdr %v: POST /setup = %d, want 403 (not local)", hdr, rec.Code)
		}
		rec = p.do("GET", "/api/v1/local/status", "127.0.0.1:5000", "localhost", hdr, nil)
		if rec.Code != http.StatusForbidden {
			t.Fatalf("hdr %v: local API = %d, want 403", hdr, rec.Code)
		}
	}
	// Control: without a trusted proxy a plain loopback request is local.
	q := newProxyEnv(t, false, "")
	rec := q.do("POST", "/setup", "127.0.0.1:5000", "127.0.0.1:8443", nil, url.Values{"username": {"x"}, "password": {"short"}, "password2": {"short"}})
	if !strings.Contains(rec.Body.String(), "CSRF") {
		t.Fatalf("plain loopback must reach the CSRF check, got %d %s", rec.Code, rec.Body.String())
	}
	// Loopback with a DNS Host (nginx without headers) is not local.
	rec = q.do("POST", "/setup", "127.0.0.1:5000", "hub.example.org", nil, url.Values{"username": {"x"}, "password": {"short"}, "password2": {"short"}})
	if rec.Code != http.StatusForbidden {
		t.Fatalf("loopback with a public Host: %d, want 403", rec.Code)
	}
}

// Backoff keys: behind a trusted proxy the real client IP counts, the proxy's private address never exempts, a LAN
// client address (from the header) does.
func TestProxyBackoffKeysOnRealClient(t *testing.T) {
	p := newProxyEnv(t, true, "10.0.0.0/8")
	login := func(client string) int {
		hdr := map[string]string{"X-Forwarded-For": client, "X-Forwarded-Proto": "https"}
		rec := p.do("GET", "/login", "10.0.0.2:1", "hub.example.org", hdr, nil)
		var csrf *http.Cookie
		for _, c := range rec.Result().Cookies() {
			if c.Name == hostPrefix+csrfCookie {
				csrf = c
			}
		}
		rec = p.do("POST", "/login", "10.0.0.2:1", "hub.example.org", hdr,
			url.Values{"username": {"ghost"}, "password": {"wrong-password-1"}, "_csrf": {csrf.Value}}, csrf)
		return rec.Code
	}
	// Per-address limit: one client exhausts its own bucket, another client of the same proxy is unaffected.
	for i := 0; i < loginMaxPerIP; i++ {
		if c := login("198.51.100.1"); c == http.StatusTooManyRequests {
			t.Fatalf("attempt %d limited early", i)
		}
	}
	if c := login("198.51.100.1"); c != http.StatusTooManyRequests {
		t.Fatalf("6th attempt of one client: %d, want 429", c)
	}
	if c := login("198.51.100.2"); c == http.StatusTooManyRequests {
		t.Fatal("another client behind the same proxy must not share the bucket")
	}
	// Username backoff runs although the TCP peer is private: spread over many clients it still triggers.
	p = newProxyEnv(t, true, "10.0.0.0/8")
	for i := 0; i < userMaxFailures; i++ {
		if c := login("203.0.113." + itoa(i+1)); c == http.StatusTooManyRequests {
			t.Fatalf("attempt %d blocked early", i)
		}
	}
	if c := login("203.0.113.99"); c != http.StatusTooManyRequests {
		t.Fatalf("username backoff must apply behind a private proxy, got %d", c)
	}
	// A LAN client address in the header keeps the exemption.
	if c := login("192.168.1.50"); c == http.StatusTooManyRequests {
		t.Fatal("a real LAN client address stays exempt from the username backoff")
	}
}

func TestProxyKeyNeverExempt(t *testing.T) {
	l := newLimiter(time.Now)
	for _, k := range []string{"proxy:10.0.0.2", "proxied"} {
		if l.userExempt(k, "admin", l.now()) {
			t.Fatalf("%q must never be exempt", k)
		}
	}
	if !l.userExempt("192.168.1.50", "admin", l.now()) {
		t.Fatal("real LAN address is exempt")
	}
}
