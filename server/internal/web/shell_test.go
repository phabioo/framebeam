package web

import (
	"bufio"
	"context"
	"github.com/phabioo/framebeam/server/internal/httpapi"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

var mainSwap = map[string]string{"HX-Request": "true", "HX-Target": "main"}

func TestMainSwapFragment(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/clients", mainSwap)
	status(t, rec, 200)
	contains(t, rec, `id="clients-body"`, `<title>`, `id="nav"`, `hx-swap-oob="outerHTML"`, `aria-current="page"`)
	notContains(t, rec, "<html", "<aside", "<body", "<main")
	if rec.Header().Get("Vary") == "" {
		t.Fatal("missing Vary")
	}
	// History restore gets the full page.
	h := map[string]string{"HX-Request": "true", "HX-Target": "main", "HX-History-Restore-Request": "true"}
	contains(t, c.get("/clients", h), "<html", "<aside", `<main class="main" id="main">`)
}

func TestFullRenderUnchangedWithNavLinks(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/library", nil)
	contains(t, rec, "<html", "<aside", `<main class="main" id="main">`, `<nav class="nav" id="nav"`,
		`hx-get="/saves" hx-target="#main" hx-push-url="true"`, `/static/app.js`)
	notContains(t, rec, "hx-swap-oob")
	// Other fragment targets keep working (library filter).
	frag := c.get("/library", map[string]string{"HX-Request": "true", "HX-Target": "library-results"})
	notContains(t, frag, "<html", "<aside", `id="nav"`)
}

func TestNavFragment(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	if rec := c.get("/nav-fragment", nil); rec.Code != http.StatusSeeOther {
		t.Fatalf("unauthenticated nav-fragment: %d", rec.Code)
	}
	c.login()
	rec := c.get("/nav-fragment", map[string]string{"HX-Request": "true", "HX-Current-URL": "http://hub/saves/u/g/0"})
	status(t, rec, 200)
	contains(t, rec, `<nav class="nav" id="nav"`, `fb:badges from:body`)
	notContains(t, rec, "<html", "hx-swap-oob")
	if !strings.Contains(rec.Body.String(), `class="active" aria-current="page" hx-get="/saves"`) {
		t.Fatalf("saves not active: %s", rec.Body.String())
	}
}

func TestEventsRequireSession(t *testing.T) {
	e := newEnv(t, true, nil)
	rec := e.client().get("/events", nil)
	if rec.Code != http.StatusSeeOther {
		t.Fatalf("events without session: %d", rec.Code)
	}
}

func TestEventsStream(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	ts := httptest.NewServer(e.mux)
	defer ts.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", ts.URL+"/events", nil)
	req.AddCookie(&http.Cookie{Name: sessionCookie, Value: c.cookies[sessionCookie]})
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if ct := resp.Header.Get("Content-Type"); ct != "text/event-stream" {
		t.Fatalf("content type %q", ct)
	}
	if resp.Header.Get("Cache-Control") != "no-store" || resp.Header.Get("X-Accel-Buffering") != "no" {
		t.Fatalf("headers: %v", resp.Header)
	}
	lines := make(chan string, 32)
	go func() {
		sc := bufio.NewScanner(resp.Body)
		for sc.Scan() {
			lines <- sc.Text()
		}
		close(lines)
	}()
	// Wait for the preamble, then trigger an action (revoking an invite is not needed: any publish works
	// through a real service call).
	for l := range lines {
		if strings.HasPrefix(l, ": connected") {
			break
		}
	}
	users, err := e.svc.ListUsers(bg)
	if err != nil || len(users) == 0 {
		t.Fatal(err)
	}
	if _, _, err := e.svc.CreateInvite(bg, users[0].ID, time.Hour, false); err != nil {
		t.Fatal(err)
	}
	got := map[string]bool{}
	deadline := time.After(5 * time.Second)
	for !got["users"] {
		select {
		case l, ok := <-lines:
			if !ok {
				t.Fatal("stream ended")
			}
			if strings.HasPrefix(l, "event: ") {
				got[strings.TrimPrefix(l, "event: ")] = true
			}
		case <-deadline:
			t.Fatalf("no users event, got %v", got)
		}
	}
	cancel() // stream ends with the request context
}

func TestEventsBadgesAndCoalesce(t *testing.T) {
	e := newEnv(t, true, nil)
	w, err := New(e.svc, e.cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	rec := httptest.NewRecorder()
	r := httptest.NewRequest("GET", "/events", nil).WithContext(ctx)
	done := make(chan struct{})
	go func() { w.serveEvents(rec, r, time.Hour, 100*time.Millisecond); close(done) }()
	time.Sleep(50 * time.Millisecond)
	for i := 0; i < 20; i++ {
		e.svc.Publish("clients")
	}
	time.Sleep(400 * time.Millisecond)
	cancel()
	<-done
	body := rec.Body.String()
	if n := strings.Count(body, "event: clients\n"); n < 1 || n > 2 {
		t.Fatalf("clients events %d (burst should coalesce): %q", n, body)
	}
	if !strings.Contains(body, "event: badges\n") {
		t.Fatalf("no badges event: %q", body)
	}
}

func TestSettingsScriptLoadedByLayoutOnly(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	contains(t, c.get("/settings", nil), `/static/settings.js`)
	frag := c.get("/settings", mainSwap)
	status(t, frag, 200)
	notContains(t, frag, "<script")
}

func TestEventsEndOnShutdown(t *testing.T) {
	e := newEnv(t, true, nil)
	w, err := New(e.svc, e.cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	rec := httptest.NewRecorder()
	r := httptest.NewRequest("GET", "/events", nil)
	done := make(chan struct{})
	go func() { w.serveEvents(rec, r, time.Hour, time.Second); close(done) }()
	time.Sleep(50 * time.Millisecond)
	w.Shutdown()
	w.Shutdown() // idempotent
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("stream did not end on shutdown")
	}
}

func TestEventsThroughLogRequests(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	log := slog.New(slog.NewTextHandler(io.Discard, nil))
	ts := httptest.NewServer(httpapi.LogRequests(log, e.mux)) // same wrapping as cmd/framebeam-hub
	defer ts.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", ts.URL+"/events", nil)
	req.AddCookie(&http.Cookie{Name: sessionCookie, Value: c.cookies[sessionCookie]})
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 || resp.Header.Get("Content-Type") != "text/event-stream" {
		t.Fatalf("status %d, type %q", resp.StatusCode, resp.Header.Get("Content-Type"))
	}
	line, err := bufio.NewReader(resp.Body).ReadString('\n')
	if err != nil || !strings.HasPrefix(line, "retry:") {
		t.Fatalf("first line %q err %v", line, err)
	}
}
