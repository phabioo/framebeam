package web

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/json"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"net/url"
	"regexp"
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
		if _, err := svc.CreateAdmin(bg, "admin", "geheim-1234"); err != nil {
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

// login meldet als admin an und liefert das CSRF-Token der Session.
func (c *client) login() string {
	c.e.t.Helper()
	c.get("/login", nil)
	rec := c.postForm("/login", url.Values{"username": {"admin"}, "password": {"geheim-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
	if rec.Code != http.StatusSeeOther {
		c.e.t.Fatalf("Login: %d %s", rec.Code, rec.Body.String())
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
		t.Fatalf("Status %d, erwartet %d: %.300s", rec.Code, want, rec.Body.String())
	}
}

func contains(t *testing.T, rec *httptest.ResponseRecorder, subs ...string) {
	t.Helper()
	for _, s := range subs {
		if !strings.Contains(rec.Body.String(), s) {
			t.Fatalf("%q fehlt in: %.500s", s, rec.Body.String())
		}
	}
}

func notContains(t *testing.T, rec *httptest.ResponseRecorder, subs ...string) {
	t.Helper()
	for _, s := range subs {
		if strings.Contains(rec.Body.String(), s) {
			t.Fatalf("%q unerwartet in: %.500s", s, rec.Body.String())
		}
	}
}

func location(rec *httptest.ResponseRecorder) string { return rec.Header().Get("Location") }

func TestRedirectToSetupWithoutAdmin(t *testing.T) {
	e := newEnv(t, false, nil)
	c := e.client()
	for _, p := range []string{"/", "/library", "/clients", "/settings", "/login", "/irgendwas"} {
		rec := c.get(p, nil)
		if rec.Code != http.StatusSeeOther || location(rec) != "/setup" {
			t.Fatalf("%s: %d -> %q", p, rec.Code, location(rec))
		}
	}
	// API bleibt unberührt
	status(t, c.get("/.well-known/framebeam", nil), 200)
	status(t, c.get("/api/v1/games", nil), 401)
	status(t, c.get("/api/v1/gibt-es-nicht", nil), 404)
}

func TestSetupLoopbackOnly(t *testing.T) {
	e := newEnv(t, false, nil)
	remote := e.client()
	remote.remote = "192.0.2.50:1234"
	rec := remote.get("/setup", nil)
	status(t, rec, 200)
	contains(t, rec, "framebeam-hub setup-admin")
	notContains(t, rec, `name="password"`)
	status(t, remote.postForm("/setup", url.Values{"username": {"x"}, "password": {"geheim-1234"}, "password2": {"geheim-1234"}, "_csrf": {"a"}}, nil), 403)
	if has, _ := e.svc.HasAdmin(bg); has {
		t.Fatal("Admin von Remote angelegt")
	}

	c := e.client()
	rec = c.get("/setup", nil)
	status(t, rec, 200)
	contains(t, rec, `name="password"`)
	good := url.Values{"username": {"fabio"}, "password": {"geheim-1234"}, "password2": {"geheim-1234"}, "_csrf": {c.cookies[csrfCookie]}}
	bad := url.Values{"username": {"fabio"}, "password": {"geheim-1234"}, "password2": {"geheim-1234"}, "_csrf": {"falsch"}}
	status(t, c.postForm("/setup", bad, nil), 403)
	mismatch := url.Values{"username": {"fabio"}, "password": {"geheim-1234"}, "password2": {"anders-1234"}, "_csrf": {c.cookies[csrfCookie]}}
	status(t, c.postForm("/setup", mismatch, nil), 400)
	rec = c.postForm("/setup", good, nil)
	if rec.Code != 303 || location(rec) != "/library" || c.cookies[sessionCookie] == "" {
		t.Fatalf("Setup: %d %q", rec.Code, location(rec))
	}
	status(t, c.get("/library", nil), 200) // direkt eingeloggt
	rec = c.get("/setup", nil)
	if rec.Code != 303 || location(rec) != "/login" {
		t.Fatalf("zweites Setup: %d", rec.Code)
	}
	status(t, c.postForm("/setup", good, nil), 303)
	if us, _ := e.svc.ListUsers(bg); len(us) != 1 {
		t.Fatal("zweiter Admin angelegt")
	}
}

func TestLoginLogoutAndCookieFlags(t *testing.T) {
	for _, tls := range []bool{false, true} {
		e := newEnv(t, true, func(c *Config) { c.UseTLS = tls })
		c := e.client()
		rec := c.get("/library", nil)
		if rec.Code != 303 || location(rec) != "/login" {
			t.Fatalf("ohne Login: %d %q", rec.Code, location(rec))
		}
		// htmx ohne Login: HX-Redirect
		if h := c.get("/clients", map[string]string{"HX-Request": "true"}).Header().Get("HX-Redirect"); h != "/login" {
			t.Fatalf("HX-Redirect %q", h)
		}
		c.get("/login", nil)
		rec = c.postForm("/login", url.Values{"username": {"admin"}, "password": {"geheim-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
		if rec.Code != 303 || location(rec) != "/library" {
			t.Fatalf("Login: %d", rec.Code)
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
			t.Fatal("kein Session-Cookie")
		}
		status(t, c.get("/library", nil), 200)
		tok := c.csrf()
		status(t, c.postForm("/logout", url.Values{}, nil), 403)
		rec = c.postForm("/logout", url.Values{"_csrf": {tok}}, nil)
		if rec.Code != 303 || location(rec) != "/login" {
			t.Fatalf("Logout: %d", rec.Code)
		}
		if _, err := e.svc.LookupWebSession(bg, "x"); err == nil {
			t.Fatal("?")
		}
		if rec := c.get("/library", nil); rec.Code != 303 {
			t.Fatalf("nach Logout: %d", rec.Code)
		}
	}
}

func TestLoginFailureAndRateLimit(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.get("/login", nil)
	wrong := func(user string) *httptest.ResponseRecorder {
		return c.postForm("/login", url.Values{"username": {user}, "password": {"falsch"}, "_csrf": {c.cookies[csrfCookie]}}, nil)
	}
	r1 := wrong("admin")
	status(t, r1, 401)
	contains(t, r1, "Benutzername oder Passwort falsch.")
	r2 := wrong("niemand")
	status(t, r2, 401)
	if !strings.Contains(r2.Body.String(), "Benutzername oder Passwort falsch.") {
		t.Fatal("Meldung muss generisch sein")
	}
	for i := 0; i < 3; i++ {
		status(t, wrong("admin"), 401)
	}
	status(t, wrong("admin"), 429)
	// auch das richtige Passwort wird jetzt abgelehnt
	status(t, c.postForm("/login", url.Values{"username": {"admin"}, "password": {"geheim-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil), 429)
	// andere IP nicht betroffen
	o := e.client()
	o.remote = "192.0.2.9:1"
	o.login()
	e.clk.Advance(61 * time.Second)
	status(t, c.postForm("/login", url.Values{"username": {"admin"}, "password": {"geheim-1234"}, "_csrf": {c.cookies[csrfCookie]}}, nil), 303)
	// Login ohne/mit falschem CSRF
	n := e.client()
	status(t, n.postForm("/login", url.Values{"username": {"admin"}, "password": {"geheim-1234"}}, nil), 403)
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
		status(t, c.postForm(p, url.Values{"_csrf": {"falsch"}}, nil), 403)
		status(t, c.postForm(p, url.Values{}, map[string]string{"X-CSRF-Token": "falsch"}), 403)
	}
	if e.svc.Info().Name == "X" {
		t.Fatal("Name trotz fehlendem CSRF geändert")
	}
	status(t, c.postForm("/settings/name", url.Values{"name": {"Neu"}}, map[string]string{"X-CSRF-Token": tok}), 303)
	// Upload ohne/mit falschem CSRF
	body, ct := multipartBody(map[string]string{"_csrf": "falsch"}, "demo.nds", []byte("abc"))
	status(t, c.do("POST", "/library/upload", body, map[string]string{"Content-Type": ct}), 403)
	body, ct = multipartBody(nil, "demo.nds", []byte("abc"))
	status(t, c.do("POST", "/library/upload", body, map[string]string{"Content-Type": ct}), 403)
	if gs, _ := e.svc.ListGames(bg); len(gs) != 0 {
		t.Fatal("Upload trotz fehlendem CSRF")
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
	rec := c.upload(tok, map[string]string{"title": "Mein Spiel"}, "spiel.nds", rom)
	if rec.Code != 303 || location(rec) != "/library?ok=uploaded" {
		t.Fatalf("Upload: %d %.300s", rec.Code, rec.Body.String())
	}
	rec = c.get("/library?ok=uploaded", nil)
	status(t, rec, 200)
	contains(t, rec, "Mein Spiel", "nds", "ROM wurde zur Library hinzugefügt.", "1 ROM")
	gs, _ := e.svc.ListGames(bg)
	if len(gs) != 1 || gs[0].System != "nds" || gs[0].ROMSize != 3000 {
		t.Fatalf("%+v", gs)
	}
	contains(t, rec, `title="`+gs[0].ROMSHA256+`"`, shortHash(gs[0].ROMSHA256))

	// per API sichtbar und ladbar
	api := e.client()
	_ = api
	dev := uuid.NewString()
	pr, _ := e.svc.CreatePairingRequest(bg, hub.PairingInput{DeviceID: dev, DeviceName: "PC", Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1", ProtocolVersion: 1, RemoteAddr: "192.0.2.1"})
	admin, _ := e.svc.VerifyPassword(bg, "admin", "geheim-1234")
	e.svc.ApprovePairing(bg, pr.RequestID, admin.ID)
	res, _ := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken)
	at, _ := e.svc.IssueAccessToken(bg, dev, res.DeviceCredential)
	rec = c.get("/api/v1/games", map[string]string{"Authorization": "Bearer " + at.Token})
	status(t, rec, 200)
	var list struct{ Games []struct{ Title string } }
	json.Unmarshal(rec.Body.Bytes(), &list)
	if len(list.Games) != 1 || list.Games[0].Title != "Mein Spiel" {
		t.Fatalf("%s", rec.Body.String())
	}
	rec = c.get("/api/v1/roms/"+gs[0].ROMSHA256, map[string]string{"Authorization": "Bearer " + at.Token})
	if rec.Code != 200 || !bytes.Equal(rec.Body.Bytes(), rom) {
		t.Fatal("ROM-Download per API")
	}

	// Duplikat, zu groß, ohne Datei, falsche Endung
	rec = c.upload(tok, nil, "kopie.nds", rom)
	status(t, rec, 409)
	contains(t, rec, "bereits in der Library")
	status(t, c.upload(tok, nil, "gross.nds", randomBytes(50000)), 413)
	status(t, c.upload(tok, nil, "x.bin", randomBytes(100)), 400)
	if g2, _ := e.svc.ListGames(bg); len(g2) != 1 {
		t.Fatal("fehlerhafte Uploads gespeichert")
	}

	// Löschen (htmx)
	rec = c.postForm("/library/"+gs[0].ID+"/delete", url.Values{"q": {""}, "system": {""}},
		map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "library-results"})
	status(t, rec, 200)
	contains(t, rec, "Noch keine ROMs", "0 ROMs")
	notContains(t, rec, "<html")
	if g3, _ := e.svc.ListGames(bg); len(g3) != 0 {
		t.Fatal("nicht gelöscht")
	}
	rec = c.get("/api/v1/roms/"+gs[0].ROMSHA256, map[string]string{"Authorization": "Bearer " + at.Token})
	status(t, rec, 404)
}

func TestSearchAndFilterFragment(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "geheim-1234")
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
	contains(t, rec, "Keine ROMs gefunden.")
	// ohne htmx-Header: komplette Seite mit Filter
	rec = c.get("/library?q=beta", nil)
	contains(t, rec, "<html", "Beta Racer", `value="beta"`, "Alle Systeme", "Nintendo DS · 2")
	notContains(t, rec, "Alpha Quest")
}

func pending(t *testing.T, e *env, dev string) hub.PairingCreated {
	t.Helper()
	pr, err := e.svc.CreatePairingRequest(bg, hub.PairingInput{DeviceID: dev, DeviceName: "Lenas Gaming-PC", Platform: "windows",
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
	contains(t, rec, "Lenas Gaming-PC", "Pending Requests · 1", "windows x86_64", "Player 0.1.0", "gerade eben", "1 Anfrage")
	admin, _ := e.svc.VerifyPassword(bg, "admin", "geheim-1234")

	// Allow mit ungültigem User: Meldung, nichts passiert
	rec = c.postForm("/clients/requests/"+pr.RequestID+"/allow", url.Values{"user_id": {"u_gibtsnicht"}}, hdr)
	status(t, rec, 200)
	contains(t, rec, "gültigen Benutzer")
	if r, _ := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken); r.Status != hub.PairingPending {
		t.Fatal("trotz Fehler freigegeben")
	}
	rec = c.postForm("/clients/requests/"+pr.RequestID+"/allow", url.Values{"user_id": {admin.ID}}, hdr)
	status(t, rec, 200)
	contains(t, rec, "Gerät erlaubt")
	notContains(t, rec, "<html")
	res, err := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken)
	if err != nil || res.Status != hub.PairingApproved || res.DeviceCredential == "" {
		t.Fatalf("%+v %v", res, err)
	}
	// zweites Allow: nicht mehr offen
	contains(t, c.postForm("/clients/requests/"+pr.RequestID+"/allow", url.Values{"user_id": {admin.ID}}, hdr), "nicht mehr offen")

	rec = c.get("/clients", nil)
	contains(t, rec, "Trusted", "Revoke access", "Lenas Gaming-PC", "admin")
	notContains(t, rec, "1 Anfrage")

	at, err := e.svc.IssueAccessToken(bg, dev, res.DeviceCredential)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Authenticate(bg, at.Token); err != nil {
		t.Fatal(err)
	}
	rec = c.postForm("/clients/devices/"+dev+"/revoke", url.Values{}, hdr)
	status(t, rec, 200)
	contains(t, rec, "Zugriff widerrufen", "Revoked")
	notContains(t, rec, "Revoke access")
	if _, err := e.svc.Authenticate(bg, at.Token); err == nil {
		t.Fatal("Access Token nach Revoke noch gültig")
	}

	// Deny
	d2 := uuid.NewString()
	p2 := pending(t, e, d2)
	rec = c.postForm("/clients/requests/"+p2.RequestID+"/deny", url.Values{}, hdr)
	contains(t, rec, "Anfrage abgelehnt.")
	if r, _ := e.svc.PollPairing(bg, p2.RequestID, p2.PollToken); r.Status != hub.PairingDenied {
		t.Fatalf("%+v", r)
	}
	if _, err := e.svc.GetDevice(bg, d2); err == nil {
		t.Fatal("Gerät nach Deny registriert")
	}
}

func TestSettingsNameAndPassword(t *testing.T) {
	e := newEnv(t, true, func(c *Config) {
		c.UseTLS, c.CertFingerprint, c.CertSource = true, "AA:BB:CC", "Selbst erzeugt"
		c.CertNotAfter = time.Date(2036, 10, 5, 0, 0, 0, 0, time.UTC)
	})
	c := e.client()
	tok := c.login()
	rec := c.get("/settings", nil)
	status(t, rec, 200)
	contains(t, rec, "Test-Hub", "HTTPS", "AA:BB:CC", "Selbst erzeugt", ":8443", "Nur für Admins")

	rec = c.postForm("/settings/name", url.Values{"name": {"Wohnzimmer-Hub"}, "_csrf": {tok}}, nil)
	if rec.Code != 303 {
		t.Fatalf("%d", rec.Code)
	}
	if e.svc.Info().Name != "Wohnzimmer-Hub" {
		t.Fatal("Name nicht geändert")
	}
	contains(t, c.get("/settings?ok=name", nil), "Hub-Name gespeichert.", "Wohnzimmer-Hub")
	status(t, c.postForm("/settings/name", url.Values{"name": {"  "}, "_csrf": {tok}}, nil), 400)

	pw := func(cur, n, n2 string) *httptest.ResponseRecorder {
		return c.postForm("/settings/password", url.Values{"current": {cur}, "new": {n}, "new2": {n2}, "_csrf": {c.csrf()}}, nil)
	}
	contains(t, pw("falsch", "neues-passwort", "neues-passwort"), "aktuelle Passwort ist falsch")
	contains(t, pw("geheim-1234", "neues-passwort", "anders"), "stimmen nicht überein")
	contains(t, pw("geheim-1234", "kurz", "kurz"), "mindestens")
	if rec := pw("geheim-1234", "neues-passwort", "neues-passwort"); rec.Code != 303 {
		t.Fatalf("%d %s", rec.Code, rec.Body.String())
	}
	status(t, c.get("/settings", nil), 200) // neue Sitzung aktiv
	if _, err := e.svc.VerifyPassword(bg, "admin", "geheim-1234"); err == nil {
		t.Fatal("altes Passwort gilt noch")
	}
	if _, err := e.svc.VerifyPassword(bg, "admin", "neues-passwort"); err != nil {
		t.Fatal(err)
	}
}

func TestSettingsDevMode(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/settings", nil)
	contains(t, rec, "HTTP (Dev-Modus)")
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
	admin, _ := e.svc.VerifyPassword(bg, "admin", "geheim-1234")
	e.svc.AddROM(bg, bytes.NewReader(randomBytes(300)), "a.nds", "Ärger <b>Titel</b>", "", admin.ID)
	for _, p := range []string{"/library", "/clients", "/settings"} {
		rec := c.get(p, nil)
		status(t, rec, 200)
		contains(t, rec, "FrameBeam Hub", "Library", "Clients", "Settings", "admin · Admin", "Test-Hub", "/static/htmx.min.js")
		notContains(t, rec, "Saves", "Stream", "&lt;no value&gt;", "<no value>")
		if inlineRe.MatchString(rec.Body.String()) {
			t.Fatalf("%s: Inline-Skript/-Style: %s", p, inlineRe.FindString(rec.Body.String()))
		}
		h := rec.Header()
		if !strings.Contains(h.Get("Content-Security-Policy"), "script-src 'self'") || strings.Contains(h.Get("Content-Security-Policy"), "unsafe") ||
			h.Get("X-Frame-Options") != "DENY" || h.Get("Referrer-Policy") != "same-origin" || h.Get("Cache-Control") != "no-store" {
			t.Fatalf("%s: Header %v", p, h)
		}
	}
	rec = c.get("/library", nil)
	contains(t, rec, "Ärger &lt;b&gt;Titel&lt;/b&gt;") // Escaping
	notContains(t, rec, "<b>Titel</b>")
	contains(t, c.get("/clients", nil), "1 Anfrage")
	status(t, c.get("/", nil), 303)
	// Static
	rec = c.get("/static/htmx.min.js", nil)
	status(t, rec, 200)
	contains(t, rec, "htmx")
	rec = c.get("/static/app.css", nil)
	status(t, rec, 200)
	contains(t, rec, "--bg-app")
	if strings.Contains(rec.Body.String(), "http://") || strings.Contains(rec.Body.String(), "https://") || strings.Contains(rec.Body.String(), "@import") {
		t.Fatal("externe Ressource in CSS")
	}
}
