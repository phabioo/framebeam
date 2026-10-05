package web

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/httpapi"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

var bg = context.Background()

type env struct {
	t   *testing.T
	svc *hub.Service
	clk *hubtest.Clock
	mux *http.ServeMux
	cfg Config
}

func newEnv(t *testing.T, withAdmin bool, mod func(*Config)) *env {
	t.Helper()
	svc, clk := hubtest.New(t, nil)
	cfg := Config{Listen: ":8443", UseTLS: false, MaxUploadBytes: 1 << 20}
	if mod != nil {
		mod(&cfg)
	}
	w, err := New(svc, cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	mux := http.NewServeMux()
	httpapi.Register(mux, svc, nil)
	w.Register(mux)
	if withAdmin {
		if _, err := svc.CreateAdmin(bg, "admin", "secret-1234"); err != nil {
			t.Fatal(err)
		}
	}
	return &env{t: t, svc: svc, clk: clk, mux: mux, cfg: cfg}
}

type client struct {
	e       *env
	cookies map[string]string
	remote  string
}

func (e *env) client() *client {
	return &client{e: e, cookies: map[string]string{}, remote: "127.0.0.1:5555"}
}

func (c *client) do(method, path string, body io.Reader, hdr map[string]string) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, path, body)
	r.RemoteAddr = c.remote
	for k, v := range c.cookies {
		r.AddCookie(&http.Cookie{Name: k, Value: v})
	}
	for k, v := range hdr {
		r.Header.Set(k, v)
	}
	rec := httptest.NewRecorder()
	c.e.mux.ServeHTTP(rec, r)
	for _, ck := range rec.Result().Cookies() {
		if ck.MaxAge < 0 {
			delete(c.cookies, ck.Name)
		} else {
			c.cookies[ck.Name] = ck.Value
		}
	}
	return rec
}

func (c *client) get(path string, hdr map[string]string) *httptest.ResponseRecorder {
	return c.do("GET", path, nil, hdr)
}

func (c *client) postForm(path string, v url.Values, hdr map[string]string) *httptest.ResponseRecorder {
	h := map[string]string{"Content-Type": "application/x-www-form-urlencoded"}
	for k, x := range hdr {
		h[k] = x
	}
	return c.do("POST", path, strings.NewReader(v.Encode()), h)
}

// login signs in as admin and returns the session's CSRF token.
func (c *client) login() string {
	c.e.t.Helper()
	c.get("/login", nil)
	rec := c.postForm("/login", url.Values{"username": {"admin"}, "password": {"secret-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
	if rec.Code != http.StatusSeeOther {
		c.e.t.Fatalf("login: %d %s", rec.Code, rec.Body.String())
	}
	return c.csrf()
}

func (c *client) csrf() string {
	ws, err := c.e.svc.LookupWebSession(bg, c.cookies[sessionCookie])
	if err != nil {
		c.e.t.Fatal(err)
	}
	return ws.CSRFToken
}

func status(t *testing.T, rec *httptest.ResponseRecorder, want int) {
	t.Helper()
	if rec.Code != want {
		t.Fatalf("status %d, want %d: %.300s", rec.Code, want, rec.Body.String())
	}
}

func contains(t *testing.T, rec *httptest.ResponseRecorder, subs ...string) {
	t.Helper()
	for _, s := range subs {
		if !strings.Contains(rec.Body.String(), s) {
			t.Fatalf("%q missing in: %.500s", s, rec.Body.String())
		}
	}
}

func notContains(t *testing.T, rec *httptest.ResponseRecorder, subs ...string) {
	t.Helper()
	for _, s := range subs {
		if strings.Contains(rec.Body.String(), s) {
			t.Fatalf("%q unexpected in: %.500s", s, rec.Body.String())
		}
	}
}

func location(rec *httptest.ResponseRecorder) string { return rec.Header().Get("Location") }

func TestRedirectToSetupWithoutAdmin(t *testing.T) {
	e := newEnv(t, false, nil)
	c := e.client()
	for _, p := range []string{"/", "/library", "/clients", "/settings", "/login", "/whatever"} {
		rec := c.get(p, nil)
		if rec.Code != http.StatusSeeOther || location(rec) != "/setup" {
			t.Fatalf("%s: %d -> %q", p, rec.Code, location(rec))
		}
	}
	// API stays untouched
	status(t, c.get("/.well-known/framebeam", nil), 200)
	status(t, c.get("/api/v1/games", nil), 401)
	status(t, c.get("/api/v1/does-not-exist", nil), 404)
}

func TestSetupLoopbackOnly(t *testing.T) {
	e := newEnv(t, false, nil)
	remote := e.client()
	remote.remote = "192.0.2.50:1234"
	rec := remote.get("/setup", nil)
	status(t, rec, 200)
	contains(t, rec, "framebeam-hub setup-admin")
	notContains(t, rec, `name="password"`)
	status(t, remote.postForm("/setup", url.Values{"username": {"x"}, "password": {"secret-1234"}, "password2": {"secret-1234"}, "_csrf": {"a"}}, nil), 403)
	if has, _ := e.svc.HasAdmin(bg); has {
		t.Fatal("admin created from remote")
	}

	c := e.client()
	rec = c.get("/setup", nil)
	status(t, rec, 200)
	contains(t, rec, `name="password"`)
	good := url.Values{"username": {"fabio"}, "password": {"secret-1234"}, "password2": {"secret-1234"}, "_csrf": {c.cookies[csrfCookie]}}
	bad := url.Values{"username": {"fabio"}, "password": {"secret-1234"}, "password2": {"secret-1234"}, "_csrf": {"wrong"}}
	status(t, c.postForm("/setup", bad, nil), 403)
	mismatch := url.Values{"username": {"fabio"}, "password": {"secret-1234"}, "password2": {"different-1234"}, "_csrf": {c.cookies[csrfCookie]}}
	status(t, c.postForm("/setup", mismatch, nil), 400)
	rec = c.postForm("/setup", good, nil)
	if rec.Code != 303 || location(rec) != "/library" || c.cookies[sessionCookie] == "" {
		t.Fatalf("setup: %d %q", rec.Code, location(rec))
	}
	status(t, c.get("/library", nil), 200) // signed in right away
	rec = c.get("/setup", nil)
	if rec.Code != 303 || location(rec) != "/login" {
		t.Fatalf("second setup: %d", rec.Code)
	}
	status(t, c.postForm("/setup", good, nil), 303)
	if us, _ := e.svc.ListUsers(bg); len(us) != 1 {
		t.Fatal("second admin created")
	}
}

func TestLoginLogoutAndCookieFlags(t *testing.T) {
	for _, tls := range []bool{false, true} {
		e := newEnv(t, true, func(c *Config) { c.UseTLS = tls })
		c := e.client()
		rec := c.get("/library", nil)
		if rec.Code != 303 || location(rec) != "/login" {
			t.Fatalf("without login: %d %q", rec.Code, location(rec))
		}
		// htmx without login: HX-Redirect
		if h := c.get("/clients", map[string]string{"HX-Request": "true"}).Header().Get("HX-Redirect"); h != "/login" {
			t.Fatalf("HX-Redirect %q", h)
		}
		c.get("/login", nil)
		rec = c.postForm("/login", url.Values{"username": {"admin"}, "password": {"secret-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
		if rec.Code != 303 || location(rec) != "/library" {
			t.Fatalf("login: %d", rec.Code)
		}
		var found bool
		for _, ck := range rec.Result().Cookies() {
			if ck.Name != sessionCookie {
				continue
			}
			found = true
			if !ck.HttpOnly || ck.SameSite != http.SameSiteStrictMode || ck.Path != "/" || ck.Secure != tls || ck.MaxAge <= 0 {
				t.Fatalf("Cookie-Flags (tls=%v): %+v", tls, ck)
			}
		}
		if !found {
			t.Fatal("no session cookie")
		}
		status(t, c.get("/library", nil), 200)
		tok := c.csrf()
		status(t, c.postForm("/logout", url.Values{}, nil), 403)
		rec = c.postForm("/logout", url.Values{"_csrf": {tok}}, nil)
		if rec.Code != 303 || location(rec) != "/login" {
			t.Fatalf("logout: %d", rec.Code)
		}
		if _, err := e.svc.LookupWebSession(bg, "x"); err == nil {
			t.Fatal("?")
		}
		if rec := c.get("/library", nil); rec.Code != 303 {
			t.Fatalf("after logout: %d", rec.Code)
		}
	}
}

func TestLoginFailureAndRateLimit(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.get("/login", nil)
	wrong := func(user string) *httptest.ResponseRecorder {
		return c.postForm("/login", url.Values{"username": {user}, "password": {"wrong"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
	}
	r1 := wrong("admin")
	status(t, r1, 401)
	contains(t, r1, "Username or password is incorrect.")
	r2 := wrong("nobody")
	status(t, r2, 401)
	if !strings.Contains(r2.Body.String(), "Username or password is incorrect.") {
		t.Fatal("message must be generic")
	}
	for i := 0; i < 3; i++ {
		status(t, wrong("admin"), 401)
	}
	status(t, wrong("admin"), 429)
	// even the correct password is rejected now
	status(t, c.postForm("/login", url.Values{"username": {"admin"}, "password": {"secret-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil), 429)
	// a different IP is not affected
	o := e.client()
	o.remote = "192.0.2.9:1"
	o.login()
	e.clk.Advance(61 * time.Second)
	status(t, c.postForm("/login", url.Values{"username": {"admin"}, "password": {"secret-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil), 303)
	// Login without/with wrong CSRF
	n := e.client()
	status(t, n.postForm("/login", url.Values{"username": {"admin"}, "password": {"secret-1234"}}, nil), 403)
}

func TestNonAdminCannotLogin(t *testing.T) {
	e := newEnv(t, true, nil)
	e.svc.CreateUser(bg, "anna", "Anna")
	c := e.client()
	c.get("/login", nil)
	status(t, c.postForm("/login", url.Values{"username": {"anna"}, "password": {""}, "_csrf": {c.cookies[csrfCookie]}}, nil), 401)
}

func TestCSRFRequiredOnPosts(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	paths := []string{"/settings/name", "/settings/password", "/library/" + uuid.NewString() + "/delete",
		"/clients/requests/" + uuid.NewString() + "/allow", "/clients/requests/" + uuid.NewString() + "/deny",
		"/clients/devices/" + uuid.NewString() + "/revoke", "/logout"}
	for _, p := range paths {
		status(t, c.postForm(p, url.Values{"name": {"X"}}, nil), 403)
		status(t, c.postForm(p, url.Values{"_csrf": {"wrong"}}, nil), 403)
		status(t, c.postForm(p, url.Values{}, map[string]string{"X-CSRF-Token": "wrong"}), 403)
	}
	if e.svc.Info().Name == "X" {
		t.Fatal("name changed despite missing CSRF")
	}
	status(t, c.postForm("/settings/name", url.Values{"name": {"New"}}, map[string]string{"X-CSRF-Token": tok}), 303)
	// Upload without/with wrong CSRF
	body, ct := multipartBody(map[string]string{"_csrf": "wrong"}, "demo.nds", []byte("abc"))
	status(t, c.do("POST", "/library/upload", body, map[string]string{"Content-Type": ct}), 403)
	body, ct = multipartBody(nil, "demo.nds", []byte("abc"))
	status(t, c.do("POST", "/library/upload", body, map[string]string{"Content-Type": ct}), 403)
	if gs, _ := e.svc.ListGames(bg); len(gs) != 0 {
		t.Fatal("upload despite missing CSRF")
	}
}

func multipartBody(fields map[string]string, filename string, data []byte) (io.Reader, string) {
	var b bytes.Buffer
	w := multipart.NewWriter(&b)
	if v, ok := fields["_csrf"]; ok {
		w.WriteField("_csrf", v)
	}
	for k, v := range fields {
		if k != "_csrf" {
			w.WriteField(k, v)
		}
	}
	fw, _ := w.CreateFormFile("file", filename)
	fw.Write(data)
	w.Close()
	return &b, w.FormDataContentType()
}

func randomBytes(n int) []byte { b := make([]byte, n); rand.Read(b); return b }

func (c *client) upload(tok string, fields map[string]string, filename string, data []byte) *httptest.ResponseRecorder {
	f := map[string]string{"_csrf": tok}
	for k, v := range fields {
		f[k] = v
	}
	body, ct := multipartBody(f, filename, data)
	return c.do("POST", "/library/upload", body, map[string]string{"Content-Type": ct})
}

func TestUploadListAPIDeleteAndErrors(t *testing.T) {
	e := newEnv(t, true, func(c *Config) { c.MaxUploadBytes = 10000 })
	c := e.client()
	tok := c.login()
	rom := randomBytes(3000)
	rec := c.upload(tok, map[string]string{"title": "My Game"}, "game.nds", rom)
	if rec.Code != 303 || location(rec) != "/library?ok=uploaded" {
		t.Fatalf("upload: %d %.300s", rec.Code, rec.Body.String())
	}
	rec = c.get("/library?ok=uploaded", nil)
	status(t, rec, 200)
	contains(t, rec, "My Game", "nds", "ROM added to the library.", "1 ROM")
	gs, _ := e.svc.ListGames(bg)
	if len(gs) != 1 || gs[0].System != "nds" || gs[0].ROMSize != 3000 {
		t.Fatalf("%+v", gs)
	}
	contains(t, rec, `title="`+gs[0].ROMSHA256+`"`, shortHash(gs[0].ROMSHA256))

	// visible and downloadable via API
	api := e.client()
	_ = api
	dev := uuid.NewString()
	pr, _ := e.svc.CreatePairingRequest(bg, hub.PairingInput{DeviceID: dev, DeviceName: "PC", Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1", ProtocolVersion: 1, RemoteAddr: "192.0.2.1"})
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")
	e.svc.ApprovePairing(bg, pr.RequestID, admin.ID)
	res, _ := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken)
	at, _ := e.svc.IssueAccessToken(bg, dev, res.DeviceCredential)
	rec = c.get("/api/v1/games", map[string]string{"Authorization": "Bearer " + at.Token})
	status(t, rec, 200)
	var list struct{ Games []struct{ Title string } }
	json.Unmarshal(rec.Body.Bytes(), &list)
	if len(list.Games) != 1 || list.Games[0].Title != "My Game" {
		t.Fatalf("%s", rec.Body.String())
	}
	rec = c.get("/api/v1/roms/"+gs[0].ROMSHA256, map[string]string{"Authorization": "Bearer " + at.Token})
	if rec.Code != 200 || !bytes.Equal(rec.Body.Bytes(), rom) {
		t.Fatal("ROM download via API")
	}

	// duplicate, too large, no file, wrong extension
	rec = c.upload(tok, nil, "copy.nds", rom)
	status(t, rec, 409)
	contains(t, rec, "already in the library")
	status(t, c.upload(tok, nil, "large.nds", randomBytes(50000)), 413)
	status(t, c.upload(tok, nil, "x.bin", randomBytes(100)), 400)
	if g2, _ := e.svc.ListGames(bg); len(g2) != 1 {
		t.Fatal("faulty uploads stored")
	}

	// Delete (htmx)
	rec = c.postForm("/library/"+gs[0].ID+"/delete", url.Values{"q": {""}, "system": {""}},
		map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "library-results"})
	status(t, rec, 200)
	contains(t, rec, "No ROMs in the library yet", "0 ROMs")
	notContains(t, rec, "<html")
	if g3, _ := e.svc.ListGames(bg); len(g3) != 0 {
		t.Fatal("not deleted")
	}
	rec = c.get("/api/v1/roms/"+gs[0].ROMSHA256, map[string]string{"Authorization": "Bearer " + at.Token})
	status(t, rec, 404)
}

func TestSearchAndFilterFragment(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")
	a, _ := e.svc.AddROM(bg, bytes.NewReader(randomBytes(200)), "alpha.nds", "Alpha Quest", "", admin.ID)
	e.svc.AddROM(bg, bytes.NewReader(randomBytes(200)), "beta.nds", "Beta Racer", "", admin.ID)
	hx := map[string]string{"HX-Request": "true", "HX-Target": "library-results"}

	rec := c.get("/library?q=alpha", hx)
	status(t, rec, 200)
	contains(t, rec, "Alpha Quest", `id="library-sub"`, `hx-swap-oob="true"`)
	notContains(t, rec, "Beta Racer", "<html", "<aside")
	rec = c.get("/library?q="+a.ROMSHA256[10:20], hx)
	contains(t, rec, "Alpha Quest")
	notContains(t, rec, "Beta Racer")
	rec = c.get("/library?system=nds", hx)
	contains(t, rec, "Alpha Quest", "Beta Racer")
	rec = c.get("/library?system=gba", hx)
	contains(t, rec, "No ROMs found.")
	// without htmx header: full page with filter
	rec = c.get("/library?q=beta", nil)
	contains(t, rec, "<html", "Beta Racer", `value="beta"`, "All systems", "Nintendo DS · 2")
	notContains(t, rec, "Alpha Quest")
}

func pending(t *testing.T, e *env, dev string) hub.PairingCreated {
	t.Helper()
	pr, err := e.svc.CreatePairingRequest(bg, hub.PairingInput{DeviceID: dev, DeviceName: "Lena Gaming PC", Platform: "windows",
		Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.77"})
	if err != nil {
		t.Fatal(err)
	}
	return pr
}

func TestClientsAllowDenyRevoke(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	hdr := map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "clients-body"}

	dev := uuid.NewString()
	pr := pending(t, e, dev)
	rec := c.get("/clients", nil)
	status(t, rec, 200)
	contains(t, rec, "Lena Gaming PC", "Pending Requests · 1", "windows x86_64", "Player 0.1.0", "just now", "1 request")
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")

	// Allow with an invalid user: message, nothing happens
	rec = c.postForm("/clients/requests/"+pr.RequestID+"/allow", url.Values{"user_id": {"u_doesnotexist"}}, hdr)
	status(t, rec, 200)
	contains(t, rec, "valid user")
	if r, _ := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken); r.Status != hub.PairingPending {
		t.Fatal("approved despite error")
	}
	rec = c.postForm("/clients/requests/"+pr.RequestID+"/allow", url.Values{"user_id": {admin.ID}}, hdr)
	status(t, rec, 200)
	contains(t, rec, "Device allowed")
	notContains(t, rec, "<html")
	res, err := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken)
	if err != nil || res.Status != hub.PairingApproved || res.DeviceCredential == "" {
		t.Fatalf("%+v %v", res, err)
	}
	// second Allow: no longer open
	contains(t, c.postForm("/clients/requests/"+pr.RequestID+"/allow", url.Values{"user_id": {admin.ID}}, hdr), "no longer open")

	rec = c.get("/clients", nil)
	contains(t, rec, "Trusted", "Revoke access", "Lena Gaming PC", "admin")
	notContains(t, rec, "1 request")

	at, err := e.svc.IssueAccessToken(bg, dev, res.DeviceCredential)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Authenticate(bg, at.Token); err != nil {
		t.Fatal(err)
	}
	rec = c.postForm("/clients/devices/"+dev+"/revoke", url.Values{}, hdr)
	status(t, rec, 200)
	contains(t, rec, "Access revoked", "Revoked")
	notContains(t, rec, "Revoke access")
	if _, err := e.svc.Authenticate(bg, at.Token); err == nil {
		t.Fatal("access token still valid after revoke")
	}

	// Deny
	d2 := uuid.NewString()
	p2 := pending(t, e, d2)
	rec = c.postForm("/clients/requests/"+p2.RequestID+"/deny", url.Values{}, hdr)
	contains(t, rec, "Request denied.")
	if r, _ := e.svc.PollPairing(bg, p2.RequestID, p2.PollToken); r.Status != hub.PairingDenied {
		t.Fatalf("%+v", r)
	}
	if _, err := e.svc.GetDevice(bg, d2); err == nil {
		t.Fatal("device registered after deny")
	}
}

func TestSettingsNameAndPassword(t *testing.T) {
	e := newEnv(t, true, func(c *Config) {
		c.UseTLS, c.CertFingerprint, c.CertSource = true, "AA:BB:CC", "Self-generated"
		c.CertNotAfter = time.Date(2036, 10, 5, 0, 0, 0, 0, time.UTC)
	})
	c := e.client()
	tok := c.login()
	rec := c.get("/settings", nil)
	status(t, rec, 200)
	contains(t, rec, "Test-Hub", "HTTPS", "AA:BB:CC", "Self-generated", ":8443", "Admins only")

	rec = c.postForm("/settings/name", url.Values{"name": {"Living-Room-Hub"}, "_csrf": {tok}}, nil)
	if rec.Code != 303 {
		t.Fatalf("%d", rec.Code)
	}
	if e.svc.Info().Name != "Living-Room-Hub" {
		t.Fatal("name not changed")
	}
	contains(t, c.get("/settings?ok=name", nil), "Hub name saved.", "Living-Room-Hub")
	status(t, c.postForm("/settings/name", url.Values{"name": {"  "}, "_csrf": {tok}}, nil), 400)

	pw := func(cur, n, n2 string) *httptest.ResponseRecorder {
		return c.postForm("/settings/password", url.Values{"current": {cur}, "new": {n}, "new2": {n2}, "_csrf": {c.csrf()}}, nil)
	}
	contains(t, pw("wrong", "new-password", "new-password"), "current password is incorrect")
	contains(t, pw("secret-1234", "new-password", "different"), "do not match")
	contains(t, pw("secret-1234", "short", "short"), "at least")
	if rec := pw("secret-1234", "new-password", "new-password"); rec.Code != 303 {
		t.Fatalf("%d %s", rec.Code, rec.Body.String())
	}
	status(t, c.get("/settings", nil), 200) // new session active
	if _, err := e.svc.VerifyPassword(bg, "admin", "secret-1234"); err == nil {
		t.Fatal("old password still valid")
	}
	if _, err := e.svc.VerifyPassword(bg, "admin", "new-password"); err != nil {
		t.Fatal(err)
	}
}

func TestSettingsDevMode(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/settings", nil)
	contains(t, rec, "HTTP (dev mode)")
	notContains(t, rec, "Fingerprint")
}

var inlineRe = regexp.MustCompile(`(?i)<script(\s[^>]*)?>[^<]|\sstyle=|<style|\son[a-z]+=`)

func TestAllPagesRenderWithHeaders(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	for _, p := range []string{"/login"} {
		_ = p
	}
	rec := c.get("/login", nil)
	status(t, rec, 200)
	c.login()
	pending(t, e, uuid.NewString())
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")
	e.svc.AddROM(bg, bytes.NewReader(randomBytes(300)), "a.nds", "Trouble <b>Title</b>", "", admin.ID)
	for _, p := range []string{"/library", "/saves", "/clients", "/settings"} {
		rec := c.get(p, nil)
		status(t, rec, 200)
		contains(t, rec, "FrameBeam Hub", "Library", "Saves", "Clients", "Settings", "admin · Admin", "Test-Hub", "/static/htmx.min.js")
		notContains(t, rec, "Stream", "&lt;no value&gt;", "<no value>")
		if inlineRe.MatchString(rec.Body.String()) {
			t.Fatalf("%s: inline script/style: %s", p, inlineRe.FindString(rec.Body.String()))
		}
		h := rec.Header()
		if !strings.Contains(h.Get("Content-Security-Policy"), "script-src 'self'") || strings.Contains(h.Get("Content-Security-Policy"), "unsafe") ||
			h.Get("X-Frame-Options") != "DENY" || h.Get("Referrer-Policy") != "same-origin" || h.Get("Cache-Control") != "no-store" {
			t.Fatalf("%s: Header %v", p, h)
		}
	}
	rec = c.get("/library", nil)
	contains(t, rec, "Trouble &lt;b&gt;Title&lt;/b&gt;") // Escaping
	notContains(t, rec, "<b>Titel</b>")
	contains(t, c.get("/clients", nil), "1 request")
	status(t, c.get("/", nil), 303)
	// Static
	rec = c.get("/static/htmx.min.js", nil)
	status(t, rec, 200)
	contains(t, rec, "htmx")
	rec = c.get("/static/app.css", nil)
	status(t, rec, 200)
	contains(t, rec, "--bg-app")
	if strings.Contains(rec.Body.String(), "http://") || strings.Contains(rec.Body.String(), "https://") || strings.Contains(rec.Body.String(), "@import") {
		t.Fatal("external resource in CSS")
	}
}

// ---- Saves page (3k) ----

func pairTestDevice(t *testing.T, e *env, userID, name string) string {
	t.Helper()
	id := uuid.NewString()
	c, err := e.svc.CreatePairingRequest(bg, hub.PairingInput{DeviceID: id, DeviceName: name, Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.1"})
	if err != nil {
		t.Fatal(err)
	}
	if err := e.svc.ApprovePairing(bg, c.RequestID, userID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.PollPairing(bg, c.RequestID, c.PollToken); err != nil {
		t.Fatal(err)
	}
	return id
}

func sha(b []byte) string { h := sha256.Sum256(b); return hex.EncodeToString(h[:]) }

func putSave(t *testing.T, e *env, userID, devID, gameID string, base int, data []byte, reason string) hub.PutSaveResult {
	t.Helper()
	r, err := e.svc.PutSave(bg, hub.PutSaveInput{UserID: userID, DeviceID: devID, GameID: gameID, Slot: "default", BaseRevision: base,
		SHA256: sha(data), Reason: reason, Body: bytes.NewReader(data)})
	if err != nil {
		t.Fatal(err)
	}
	return r
}

func TestSavesPage(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	// Not signed in: redirect to login.
	status(t, c.get("/saves", nil), http.StatusSeeOther)
	tok := c.login()

	// Empty state, no badge.
	rec := c.get("/saves", nil)
	status(t, rec, http.StatusOK)
	contains(t, rec, "No saves yet.", `href="/saves"`)
	notContains(t, rec, "conflict</span>", "Restore")

	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	other, err := e.svc.CreateUser(bg, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	g, err := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "harbor.nds", "Harbor Rally", "", adminID)
	if err != nil {
		t.Fatal(err)
	}
	devA, devB := pairTestDevice(t, e, adminID, "Laptop Office"), pairTestDevice(t, e, adminID, "Desktop Living Room")
	hubSave, localSave := []byte("hub-save-content"), []byte("local-save-content")
	putSave(t, e, adminID, devA, g.ID, 0, []byte("old"), hub.SyncCheckpoint)
	putSave(t, e, adminID, devA, g.ID, 1, hubSave, hub.SyncFinalSessionEnd) // Rev 2 + session end
	r := putSave(t, e, adminID, devB, g.ID, 1, localSave, hub.SyncCheckpoint)
	if r.Conflict == nil {
		t.Fatal("expected a conflict")
	}
	base := "/saves/" + adminID + "/" + g.ID + "/default"

	// List: conflict first, badge, detail with both sides and the actions; no Restore.
	rec = c.get("/saves", nil)
	status(t, rec, http.StatusOK)
	contains(t, rec, "Harbor Rally", "▲ Conflict · unresolved", "1 conflict</span>", "Current checkpoint", "Rev 2", "Laptop Office",
		"▲ Conflict: upload is based on Rev 1, current is Rev 2", "Current checkpoint on the Hub", "Secured upload · sync pending",
		"Desktop Living Room", shortHash(sha(hubSave)), shortHash(sha(localSave)),
		"Use Hub version", "Adopt local save as new current version", "Keep both, decide later", "Download",
		`name="expected_revision" value="2"`, `hx-confirm=`, base+"/conflicts/"+r.Conflict.ID+"/resolve", "CURRENT", "Session end", "Conflict upload")
	notContains(t, rec, "Restore")
	// Detail by path, unknown slot is 404.
	status(t, c.get(base, nil), http.StatusOK)
	status(t, c.get("/saves/"+adminID+"/"+g.ID+"/nope", nil), http.StatusNotFound)
	// User filter.
	rec = c.get("/saves?user="+other.ID, nil)
	status(t, rec, http.StatusOK)
	contains(t, rec, "No saves yet.")
	notContains(t, rec, "Harbor Rally")
	contains(t, c.get("/saves?user="+adminID, nil), "Harbor Rally")

	// Downloads (current + history).
	rec = c.get(base+"/download", nil)
	status(t, rec, http.StatusOK)
	if !bytes.Equal(rec.Body.Bytes(), hubSave) || !strings.Contains(rec.Header().Get("Content-Disposition"), "attachment") {
		t.Fatalf("download: %q %v", rec.Body.String(), rec.Header())
	}
	rec = c.get(base+"/history/"+strconv.Itoa(r.Conflict.Secured.Version)+"/download", nil)
	status(t, rec, http.StatusOK)
	if !bytes.Equal(rec.Body.Bytes(), localSave) {
		t.Fatal("history download differs")
	}
	status(t, c.get(base+"/history/99/download", nil), http.StatusNotFound)

	resolve := base + "/conflicts/" + r.Conflict.ID + "/resolve"
	// CSRF: without token or with a wrong one, nothing happens.
	status(t, c.postForm(resolve, url.Values{"resolution": {"use_local"}, "expected_revision": {"2"}}, nil), http.StatusForbidden)
	status(t, c.postForm(resolve, url.Values{"resolution": {"use_local"}, "expected_revision": {"2"}, "_csrf": {"wrong"}}, nil), http.StatusForbidden)
	if n, _ := e.svc.OpenConflictCount(bg); n != 1 {
		t.Fatalf("conflict resolved without CSRF: %d", n)
	}
	// Stale expected_revision: redirect with an error message, nothing changed.
	rec = c.postForm(resolve, url.Values{"resolution": {"use_local"}, "expected_revision": {"1"}, "_csrf": {tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if !strings.Contains(location(rec), "err=stale") {
		t.Fatalf("location %q", location(rec))
	}
	contains(t, c.get(location(rec), nil), "The slot changed in the meantime")
	if s, _ := e.svc.GetSaveSlot(bg, adminID, g.ID, "default"); s.Current.Revision != 2 || len(s.OpenConflicts) != 1 {
		t.Fatalf("%+v", s)
	}
	// Resolve via web (htmx: HX-Redirect).
	rec = c.postForm(resolve, url.Values{"resolution": {"use_local"}, "expected_revision": {"2"}, "_csrf": {tok}}, map[string]string{"HX-Request": "true"})
	status(t, rec, http.StatusOK)
	if rec.Header().Get("HX-Redirect") != base+"?ok=resolved" {
		t.Fatalf("HX-Redirect %q", rec.Header().Get("HX-Redirect"))
	}
	cf, err := e.svc.GetSaveConflict(bg, adminID, g.ID, "default", r.Conflict.ID)
	if err != nil || cf.Status != hub.ConflictResolvedLocal || cf.ResolvedBy != "user:"+adminID {
		t.Fatalf("%+v %v", cf, err)
	}
	rec = c.get(base+"?ok=resolved", nil)
	status(t, rec, http.StatusOK)
	contains(t, rec, "Conflict resolved.", "Rev 3", "Desktop Living Room", "Checkpoint · today", "Session end", "Conflict upload")
	notContains(t, rec, "Use Hub version", "conflict</span>", "Restore")
	// Resolving again: stale.
	rec = c.postForm(resolve, url.Values{"resolution": {"use_hub"}, "expected_revision": {"3"}, "_csrf": {tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if !strings.Contains(location(rec), "err=stale") {
		t.Fatalf("location %q", location(rec))
	}
}
