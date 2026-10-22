package hub_test

import (
	"bytes"
	"context"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

const cfToken = "tok-secret-0123456789"

type cfAPI struct {
	srv    *httptest.Server
	status atomic.Int32
	body   atomic.Value // string
	calls  atomic.Int32
	mu     sync.Mutex
	gotReq struct{ path, auth, body string }
}

func newCFAPI(t *testing.T, body string) *cfAPI {
	a := &cfAPI{}
	a.status.Store(http.StatusCreated)
	a.body.Store(body)
	a.srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		a.calls.Add(1)
		var b bytes.Buffer
		b.ReadFrom(r.Body)
		a.mu.Lock()
		a.gotReq.path, a.gotReq.auth, a.gotReq.body = r.Method+" "+r.URL.Path, r.Header.Get("Authorization"), b.String()
		a.mu.Unlock()
		w.WriteHeader(int(a.status.Load()))
		if a.status.Load() >= 400 {
			w.Write([]byte(`{"error":"echo ` + cfToken + `"}`)) // a body that must never reach logs or errors
			return
		}
		w.Write([]byte(a.body.Load().(string)))
	}))
	t.Cleanup(a.srv.Close)
	return a
}

const cfBody = `{"iceServers":[
 {"urls":["stun:stun.cloudflare.com:3478","stun:stun.cloudflare.com:53"]},
 {"urls":["turn:turn.cloudflare.com:3478?transport=udp","turn:turn.cloudflare.com:53?transport=udp","turn:turn.cloudflare.com:3478?transport=tcp","turns:turn.cloudflare.com:5349?transport=tcp","turns:turn.cloudflare.com:443?transport=tcp"],
  "username":"cf-user","credential":"cf-pass"}]}`

type syncBuf struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (s *syncBuf) Write(p []byte) (int, error) { s.mu.Lock(); defer s.mu.Unlock(); return s.b.Write(p) }
func (s *syncBuf) String() string              { s.mu.Lock(); defer s.mu.Unlock(); return s.b.String() }

func waitFor(t *testing.T, what string, ok func() bool) {
	t.Helper()
	for i := 0; i < 400; i++ {
		if ok() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatalf("timeout: %s", what)
}

func TestCloudflareTURNCredentials(t *testing.T) {
	api := newCFAPI(t, cfBody)
	svc, _ := hubtest.New(t, func(o *hub.Options) { o.ICEServers = []string{"stun:own.example.org:3478"} })
	if svc.RelayEnabled() {
		t.Fatal("relay on before start")
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartCloudflareTURN(ctx, hub.CloudflareTURNOptions{KeyID: "key123", Token: cfToken, BaseURL: api.srv.URL, TTL: time.Hour})
	if !svc.RelayEnabled() {
		t.Fatal("turn_v1 must be announced while the provider is on")
	}
	waitFor(t, "first fetch", func() bool { return api.calls.Load() > 0 && len(svc.CloudflareTURNURLs()) > 0 })
	api.mu.Lock()
	got := api.gotReq
	api.mu.Unlock()
	if got.path != "POST /v1/turn/keys/key123/credentials/generate-ice-servers" || got.auth != "Bearer "+cfToken || strings.TrimSpace(got.body) != `{"ttl":3600}` {
		t.Fatalf("%+v", got)
	}
	urls := svc.CloudflareTURNURLs()
	want := []string{"turn:turn.cloudflare.com:3478?transport=udp", "turn:turn.cloudflare.com:3478?transport=tcp",
		"turns:turn.cloudflare.com:5349?transport=tcp", "turns:turn.cloudflare.com:443?transport=tcp"}
	if strings.Join(urls, "|") != strings.Join(want, "|") {
		t.Fatalf("%v", urls)
	}
}

func TestCloudflareURLsStringOrArrayAndDedupe(t *testing.T) {
	api := newCFAPI(t, `{"iceServers":[{"urls":"stun:stun.cloudflare.com:3478"},{"urls":"stun:stun.cloudflare.com:3478"},
		{"urls":"turn:turn.cloudflare.com:3478?transport=udp","username":"u","credential":"c"}]}`)
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartCloudflareTURN(ctx, hub.CloudflareTURNOptions{KeyID: "k", Token: cfToken, BaseURL: api.srv.URL})
	waitFor(t, "fetch", func() bool { return len(svc.CloudflareTURNURLs()) > 0 })
	if u := svc.CloudflareTURNURLs(); len(u) != 1 || u[0] != "turn:turn.cloudflare.com:3478?transport=udp" {
		t.Fatalf("%v", u)
	}
	// The default STUN and the provider's STUN URL are the same: ice_servers has it once.
	if s := svc.ICEServersFor("hub.example.org", "dev"); len(s) != 1 {
		t.Fatalf("%v", s)
	}
}

func TestCloudflareFailureNoSecretsAndRecovery(t *testing.T) {
	api := newCFAPI(t, cfBody)
	api.status.Store(http.StatusUnauthorized)
	svc, _ := hubtest.New(t, nil)
	var logs syncBuf
	log := slog.New(slog.NewTextHandler(&logs, nil))
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartCloudflareTURN(ctx, hub.CloudflareTURNOptions{KeyID: "k", Token: cfToken, BaseURL: api.srv.URL, Log: log,
		RetryMin: 5 * time.Millisecond, RetryMax: 10 * time.Millisecond})
	waitFor(t, "retries", func() bool { return api.calls.Load() >= 4 })
	if n := strings.Count(logs.String(), "level=WARN"); n != 1 {
		t.Fatalf("want one warning per failure streak, got %d: %s", n, logs.String())
	}
	if strings.Contains(logs.String(), cfToken) || strings.Contains(logs.String(), "echo") || !strings.Contains(logs.String(), "401") {
		t.Fatalf("log: %s", logs.String())
	}
	if len(svc.CloudflareTURNURLs()) != 0 || !svc.RelayEnabled() {
		t.Fatal("no credentials expected, provider still configured")
	}
	api.status.Store(http.StatusCreated)
	waitFor(t, "recovery", func() bool { return len(svc.CloudflareTURNURLs()) > 0 })
	waitFor(t, "recovery log", func() bool { return strings.Contains(logs.String(), "refreshed again") })
}

func TestCloudflareRefreshAtHalfTTL(t *testing.T) {
	api := newCFAPI(t, cfBody)
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartCloudflareTURN(ctx, hub.CloudflareTURNOptions{KeyID: "k", Token: cfToken, BaseURL: api.srv.URL, TTL: 40 * time.Millisecond})
	waitFor(t, "refreshes", func() bool { return api.calls.Load() >= 3 })
	cancel()
	waitFor(t, "stopped", func() bool { return !svc.RelayEnabled() })
}
