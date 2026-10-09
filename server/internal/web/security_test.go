package web

import (
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

// A multipart POST must carry the CSRF token like any other POST; only the three streaming upload routes defer
// the check to their handler.
func TestMultipartPostsNeedCSRFToken(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	anna, err := e.svc.CreateUser(bg, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	nameBefore := e.svc.Info().Name
	for _, p := range []string{"/users/" + anna.ID + "/delete", "/users/" + anna.ID + "/disable", "/settings/name", "/library/rescan", "/cores/sync"} {
		fields := map[string]string{"name": "Hijacked"}
		status(t, c.multipartPost(p, fields, "", nil), 403)
		status(t, c.multipartPost(p, map[string]string{"name": "Hijacked", "_csrf": "wrong"}, "", nil), 403)
	}
	if us, _ := e.svc.ListUsers(bg); len(us) != 2 {
		t.Fatalf("users changed: %d", len(us))
	}
	if e.svc.Info().Name != nameBefore {
		t.Fatal("hub name changed without CSRF token")
	}
	// With the token (field or header) the same request goes through.
	status(t, c.multipartPost("/settings/name", map[string]string{"name": "Fine", "_csrf": tok}, "", nil), 303)
	if e.svc.Info().Name != "Fine" {
		t.Fatalf("name %q", e.svc.Info().Name)
	}
	body, ct := multipartBody(map[string]string{"name": "ViaHeader"}, "", nil)
	status(t, c.do("POST", "/settings/name", body, map[string]string{"Content-Type": ct, "X-CSRF-Token": tok}), 303)
	if e.svc.Info().Name != "ViaHeader" {
		t.Fatalf("name %q", e.svc.Info().Name)
	}
	// The upload routes keep working with _csrf in the form.
	status(t, c.upload(tok, nil, "game.nds", randomBytes(100)), 303)
	status(t, c.upload("wrong", nil, "game.nds", randomBytes(100)), 403)
}

func loginReq(c *client, user, pw, remote string) *http.Request {
	r := httptest.NewRequest("POST", "/login", strings.NewReader(url.Values{"username": {user}, "password": {pw}, "_csrf": {c.cookies[csrfCookie]}}.Encode()))
	r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	r.AddCookie(&http.Cookie{Name: csrfCookie, Value: c.cookies[csrfCookie]})
	r.RemoteAddr = remote
	return r
}

// Concurrent wrong logins from one address: at most 5 reach the password verification.
func TestConcurrentLoginsAreLimited(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.get("/login", nil)
	const n = 40
	codes := make(chan int, n)
	var wg sync.WaitGroup
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			rec := httptest.NewRecorder()
			e.mux.ServeHTTP(rec, loginReq(c, "admin", "wrong", "198.51.100.7:1"))
			codes <- rec.Code
		}()
	}
	wg.Wait()
	close(codes)
	verified, limited := 0, 0
	for code := range codes {
		switch code {
		case 401:
			verified++
		case 429:
			limited++
		default:
			t.Fatalf("unexpected status %d", code)
		}
	}
	if verified > loginMaxPerIP || verified+limited != n {
		t.Fatalf("verified %d (max %d), limited %d", verified, loginMaxPerIP, limited)
	}
}

func TestLoginBusyWhenNoVerifySlot(t *testing.T) {
	old := verifyWait
	verifyWait = 20 * time.Millisecond
	defer func() { verifyWait = old }()
	e := newEnv(t, true, nil)
	w, err := New(e.svc, e.cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	mux := http.NewServeMux()
	w.Register(mux)
	c := e.client()
	rec := httptest.NewRecorder()
	r := httptest.NewRequest("GET", "/login", nil)
	r.RemoteAddr = "127.0.0.1:1"
	mux.ServeHTTP(rec, r)
	for _, ck := range rec.Result().Cookies() {
		c.cookies[ck.Name] = ck.Value
	}
	for i := 0; i < maxConcurrentVerify; i++ {
		w.verifySem <- struct{}{}
	}
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, loginReq(c, "admin", "secret-12345", "198.51.100.7:1"))
	if rec.Code != http.StatusServiceUnavailable || rec.Header().Get("Retry-After") == "" {
		t.Fatalf("status %d, Retry-After %q", rec.Code, rec.Header().Get("Retry-After"))
	}
	for i := 0; i < maxConcurrentVerify; i++ {
		<-w.verifySem
	}
	// The busy answer did not use up an attempt: the login works now.
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, loginReq(c, "admin", "secret-12345", "198.51.100.7:1"))
	if rec.Code != http.StatusSeeOther {
		t.Fatalf("status %d", rec.Code)
	}
}

func TestLimiterKeysBackoffAndEviction(t *testing.T) {
	now := time.Date(2026, 1, 1, 12, 0, 0, 0, time.UTC)
	l := newLimiter(func() time.Time { return now })

	// IPv6 is keyed by /64; IPv4 by the full address.
	for i := 0; i < loginMaxPerIP; i++ {
		if !l.take("2001:db8:1:2::"+string(rune('a'+i)), "u"+string(rune('a'+i))) {
			t.Fatalf("take %d", i)
		}
	}
	if l.take("2001:db8:1:2:ffff::1", "x") {
		t.Fatal("same /64 must share the limit")
	}
	if !l.take("2001:db8:1:3::1", "x") {
		t.Fatal("another /64 must not be affected")
	}
	if ipKey("192.0.2.1") == ipKey("192.0.2.2") {
		t.Fatal("IPv4 is keyed by the full address")
	}

	// Per-username backoff, independent of whether the user exists; grows, capped at 15 min.
	now = now.Add(time.Hour)
	l = newLimiter(func() time.Time { return now })
	attempt := func(i int) bool { return l.take("203.0.113."+string(rune('0'+i%10))+string(rune('0'+i/10)), "Ghost") }
	for i := 0; i < userMaxFailures; i++ {
		if !attempt(i) {
			t.Fatalf("attempt %d blocked early", i)
		}
	}
	if attempt(50) {
		t.Fatal("username must be blocked after 10 failures")
	}
	now = now.Add(userBackoffBase + time.Second)
	for i := 0; i < userMaxFailures; i++ {
		if !attempt(100 + i) {
			t.Fatalf("second round %d", i)
		}
	}
	if attempt(51) {
		t.Fatal("second block expected")
	}
	now = now.Add(userBackoffBase + time.Second) // the second block is longer (60 s)
	if attempt(52) {
		t.Fatal("block must have doubled")
	}
	now = now.Add(userBackoffMax + time.Second)
	if !attempt(53) {
		t.Fatal("block must end after the cap")
	}
	// A refund (successful login) releases the reservation.
	l = newLimiter(func() time.Time { return now })
	for i := 0; i < 20; i++ {
		if !l.take("192.0.2.1", "admin") {
			t.Fatalf("refund round %d", i)
		}
		l.refund("192.0.2.1", "admin")
	}

	// Stale entries are evicted; the maps stay bounded.
	l = newLimiter(func() time.Time { return now })
	for i := 0; i < 3*limiterHardCap; i++ {
		l.take("10.1."+itoa(i/250%250)+"."+itoa(i%250), "user"+itoa(i))
		if len(l.hits) > limiterHardCap || len(l.users) > limiterHardCap {
			t.Fatalf("map grew to %d/%d", len(l.hits), len(l.users))
		}
		if i%1000 == 0 {
			now = now.Add(20 * time.Minute)
		}
	}
	now = now.Add(time.Hour)
	for i := 0; i <= limiterSweepSize; i++ {
		l.mu.Lock()
		l.hits["k"+itoa(i)] = []time.Time{now.Add(-time.Hour)}
		l.mu.Unlock()
	}
	l.take("192.0.2.9", "z")
	if len(l.hits) > 2 || len(l.users) > 2 {
		t.Fatalf("stale entries not evicted: %d/%d", len(l.hits), len(l.users))
	}
}

func itoa(i int) string { return strconv.Itoa(i) }

// An internet attacker must not be able to lock the admin out: the username block spares LAN addresses and
// addresses with a recent successful login as that user.
func TestUsernameBlockSparesLANAndKnownAddresses(t *testing.T) {
	now := time.Date(2026, 1, 1, 12, 0, 0, 0, time.UTC)
	l := newLimiter(func() time.Time { return now })
	// A previous successful login from 203.0.113.50 and from the IPv6 network 2001:db8:5:5::/64.
	l.take("203.0.113.50", "admin")
	l.success("203.0.113.50", "admin")
	l.take("2001:db8:5:5::1", "admin")
	l.success("2001:db8:5:5::1", "admin")
	// Attackers from many unknown internet addresses block the username.
	for i := 0; i < userMaxFailures; i++ {
		if !l.take("198.51.100."+itoa(i+1), "admin") {
			t.Fatalf("attack attempt %d", i)
		}
	}
	if l.take("198.51.100.200", "admin") {
		t.Fatal("unknown internet address must be blocked")
	}
	for _, addr := range []string{"192.168.1.20", "10.0.0.5", "127.0.0.1", "fe80::1", "fd00::7", "203.0.113.50", "2001:db8:5:5::99"} {
		if !l.take(addr, "admin") {
			t.Fatalf("%s must not be blocked", addr)
		}
		l.refund(addr, "admin")
	}
	// The trust expires after 30 days.
	now = now.Add(trustedFor + userBackoffMax + time.Hour)
	for i := 0; i < userMaxFailures; i++ {
		l.take("198.51.100."+itoa(i+1), "admin")
	}
	if l.take("203.0.113.50", "admin") {
		t.Fatal("trust must expire")
	}
}
