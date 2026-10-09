package web

import (
	"bytes"
	"errors"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// ---- Settings > Security: Renew now ----

type fakeCert struct {
	fp    string
	exp   time.Time
	calls int
	err   error
}

func (f *fakeCert) state() (string, time.Time) { return f.fp, f.exp }
func (f *fakeCert) renew() (string, time.Time, error) {
	f.calls++
	if f.err != nil {
		return "", time.Time{}, f.err
	}
	f.fp, f.exp = "AA:BB:NEW", time.Now().AddDate(10, 0, 0)
	return f.fp, f.exp, nil
}

func TestRenewCertFromSettings(t *testing.T) {
	fc := &fakeCert{fp: "AA:BB:OLD", exp: time.Now().AddDate(5, 0, 0)}
	e := newEnv(t, true, func(c *Config) {
		c.UseTLS, c.CertSource, c.CertState, c.RenewCert = true, "Self-generated", fc.state, fc.renew
	})
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/security", nil)
	contains(t, rec, "Renew now", "Every Player has to confirm the new fingerprint", "AA:BB:OLD")
	notContains(t, rec, "confirm(")

	rec = postTok(c, tok, "/settings/security/renew-cert", nil)
	status(t, rec, 303)
	if loc := location(rec); loc != "/settings/security?ok=certrenewed" {
		t.Fatalf("location %q", loc)
	}
	if fc.calls != 1 {
		t.Fatal("renew not called")
	}
	rec = c.get("/settings/security?ok=certrenewed", nil)
	contains(t, rec, "AA:BB:NEW", "Certificate renewed")
	notContains(t, rec, "AA:BB:OLD")

	// A failure keeps the page working and reports it.
	fc.err = errors.New("disk full")
	rec = postTok(c, tok, "/settings/security/renew-cert", nil)
	if loc := location(rec); loc != "/settings/security?err=renewfailed" {
		t.Fatalf("location %q", loc)
	}
	contains(t, c.get("/settings/security?err=renewfailed", nil), "could not be renewed")
}

func TestRenewCertNotOfferedForOwnCert(t *testing.T) {
	fc := &fakeCert{fp: "AA:BB:OWN", exp: time.Now().AddDate(1, 0, 0)}
	e := newEnv(t, true, func(c *Config) {
		c.UseTLS, c.CertSource, c.CertState = true, "Own cert/key", fc.state
		c.RenewCert = fc.renew // even if wired, an own certificate is never renewed
	})
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/security", nil)
	contains(t, rec, "never renewed by the hub")
	notContains(t, rec, "Renew now", "renew-cert")
	rec = postTok(c, tok, "/settings/security/renew-cert", nil)
	if loc := location(rec); loc != "/settings/security?err=renewunsupported" || fc.calls != 0 {
		t.Fatalf("location %q calls %d", loc, fc.calls)
	}
}

func TestRenewCertNotOfferedWithoutCallback(t *testing.T) {
	e := newEnv(t, true, func(c *Config) { c.UseTLS, c.CertSource, c.CertFingerprint = true, "Self-generated", "AA:BB" })
	c := e.client()
	c.login()
	rec := c.get("/settings/security", nil)
	notContains(t, rec, "Renew now")
	contains(t, rec, "AA:BB")
}

// ---- Invite link and /invite page ----

func TestInviteCopyLinkOnlyWithNewCode(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	rec := postTok(c, tok, "/users/invites", url.Values{"expiry": {"1h"}})
	code := codeRe.FindString(rec.Body.String())
	if code == "" {
		t.Fatal("no code")
	}
	contains(t, rec, "Copy link", `data-copy-link="http://example.com/invite#`+code+`"`, "readonly")
	rec = c.get("/users", nil)
	notContains(t, rec, "Copy link", code, "data-copy-link")
}

func TestInviteLinkUsesPublicHost(t *testing.T) {
	e := newEnv(t, true, func(c *Config) { c.UseTLS, c.PublicHost, c.PublicPort = true, "hub.example.org", 9443 })
	c := e.client()
	tok := c.login()
	rec := postTok(c, tok, "/users/invites", nil)
	code := codeRe.FindString(rec.Body.String())
	contains(t, rec, `data-copy-link="https://hub.example.org:9443/invite#`+code+`"`)

	e2 := newEnv(t, true, func(c *Config) { c.UseTLS, c.PublicHost = true, "hub.example.org" })
	c2 := e2.client()
	tok2 := c2.login()
	rec = postTok(c2, tok2, "/users/invites", nil)
	contains(t, rec, `data-copy-link="https://hub.example.org/invite#`)
}

func TestInviteLandingPublic(t *testing.T) {
	e := newEnv(t, true, func(c *Config) { c.UseTLS, c.CertFingerprint, c.CertSource = true, "AA:BB:CC:DD", "Self-generated" })
	anon := e.client()
	rec := anon.get("/invite", nil) // no login
	status(t, rec, 200)
	contains(t, rec, "I have an invite code", "Install or open the FrameBeam Player", "AA:BB:CC:DD", "example.com",
		`/static/invite.js`, "data-invite-code")
	notContains(t, rec, "FB-", "secret-12345", "csrf", "Sign out", `name="_csrf"`)
	if got := rec.Header().Get("Content-Security-Policy"); !strings.Contains(got, "script-src 'self'") {
		t.Fatalf("csp %q", got)
	}
	if strings.Contains(rec.Body.String(), "<script>") || strings.Contains(rec.Body.String(), "onclick") {
		t.Fatal("inline script")
	}
	// A fragment is never sent; a query string code is ignored and not echoed.
	rec = anon.get("/invite?code=FB-AAAA-BBBB", nil)
	status(t, rec, 200)
	notContains(t, rec, "FB-AAAA-BBBB")
}

// ---- Clients: Player too old ----

func TestClientsPlayerTooOld(t *testing.T) {
	e := newEnvOpts(t, true, nil, func(o *hub.Options) { o.ProtocolVersion, o.MinProtocolVersion = 3, 2 })
	c := e.client()
	c.login()
	users, _ := e.svc.ListUsers(bg)
	adminID := users[0].ID
	old := pairTestDevice(t, e, adminID, "Old Laptop")
	cur := pairTestDevice(t, e, adminID, "New Desktop")
	cores := []hub.CoreReport{}
	if _, err := e.svc.Handshake(bg, old, hub.HandshakeInput{Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0",
		ProtocolVersion: 1, MinProtocolVersion: 1, Cores: &cores}); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Handshake(bg, cur, hub.HandshakeInput{Platform: "linux", Arch: "x86_64", PlayerVersion: "0.2.0",
		ProtocolVersion: 3, MinProtocolVersion: 1, Cores: &cores}); err != nil {
		t.Fatal(err)
	}
	// A pending request from an old Player.
	if _, err := e.svc.CreatePairingRequest(bg, hub.PairingInput{DeviceID: uuid.NewString(), DeviceName: "Pending Old", Platform: "linux",
		Arch: "x86_64", PlayerVersion: "0.1", ProtocolVersion: 1, RemoteAddr: "192.0.2.9"}); err != nil {
		t.Fatal(err)
	}
	rec := c.get("/clients", nil)
	body := rec.Body.String()
	if n := strings.Count(body, "Player too old · protocol v2 required"); n != 2 {
		t.Fatalf("pill count %d (device + pending expected): %.800s", n, body)
	}
	// Only the old device's row carries the pill.
	row := func(name string) string {
		i := strings.Index(body, name)
		j := strings.Index(body[i:], "</tr>")
		return body[i : i+j]
	}
	if !strings.Contains(row("Old Laptop"), "pill error") || strings.Contains(row("New Desktop"), "too old") {
		t.Fatal("wrong rows marked")
	}
}

// ---- Library: Rescan folder ----

func TestLibraryRescan(t *testing.T) {
	dir := t.TempDir()
	write := func(name, data string) {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(data), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("alpha.nds", "dummy-alpha-rom")
	write("beta.nds", "dummy-beta-rom")
	write("copy-of-alpha.nds", "dummy-alpha-rom") // same content
	write("readme.txt", "not a rom")
	write("empty.nds", "")
	e := newEnv(t, true, func(c *Config) { c.ImportDir = dir })
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	if _, err := e.svc.AddROM(bg, bytes.NewReader([]byte("dummy-beta-rom")), "beta-existing.nds", "Beta Existing", "", users[0].ID); err != nil {
		t.Fatal(err)
	}
	contains(t, c.get("/library", nil), "Rescan folder", dir)

	rec := postTok(c, tok, "/library/rescan", nil)
	status(t, rec, 200)
	// alpha added; copy-of-alpha and beta already present; readme unsupported; empty failed.
	contains(t, rec, "1 added, 2 already in the library, 1 unsupported, 1 failed", "empty.nds", "alpha")
	games, _ := e.svc.ListGames(bg)
	if len(games) != 2 {
		t.Fatalf("%d games", len(games))
	}
	// Sources untouched.
	ents, _ := os.ReadDir(dir)
	if len(ents) != 5 {
		t.Fatalf("source folder changed: %d entries", len(ents))
	}
	if b, _ := os.ReadFile(filepath.Join(dir, "alpha.nds")); string(b) != "dummy-alpha-rom" {
		t.Fatal("source modified")
	}
	// Second scan adds nothing.
	rec = postTok(c, tok, "/library/rescan", nil)
	contains(t, rec, "0 added, 3 already in the library")
}

func TestLibraryRescanMissingOrUnconfigured(t *testing.T) {
	e := newEnv(t, true, func(c *Config) { c.ImportDir = filepath.Join(t.TempDir(), "nope") })
	c := e.client()
	tok := c.login()
	rec := postTok(c, tok, "/library/rescan", nil)
	status(t, rec, 400)
	contains(t, rec, "does not exist")

	e = newEnv(t, true, nil)
	c = e.client()
	tok = c.login()
	notContains(t, c.get("/library", nil), "Rescan folder")
	status(t, postTok(c, tok, "/library/rescan", nil), 400)
}
