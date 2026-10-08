package web

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// Admin web pages: Users, Systems & Cores, Settings and Clients.

var codeRe = regexp.MustCompile(`FB-[2-9A-HJKMNP-Z]{4}-[2-9A-HJKMNP-Z]{4}`)

func postTok(c *client, tok, path string, v url.Values) *httptest.ResponseRecorder {
	if v == nil {
		v = url.Values{}
	}
	v.Set("_csrf", tok)
	return c.postForm(path, v, nil)
}

func (c *client) multipartPost(path string, fields map[string]string, filename string, data []byte) *httptest.ResponseRecorder {
	body, ct := multipartBody(fields, filename, data)
	return c.do("POST", path, body, map[string]string{"Content-Type": ct})
}

func TestAdminPagesAreAdminOnlyAndRender(t *testing.T) {
	e := newEnv(t, true, nil)
	anon := e.client()
	for _, p := range []string{"/users", "/systems"} {
		rec := anon.get(p, nil)
		if rec.Code != http.StatusSeeOther || location(rec) != "/login" {
			t.Fatalf("%s: %d -> %q", p, rec.Code, location(rec))
		}
	}
	for _, p := range []string{"/users/invites", "/users/u_x/disable", "/users/u_x/enable", "/users/invites/x/revoke", "/systems/nds/expected-version",
		"/systems/nds/firmware-mode", "/systems/nds/firmware/bios7/pin", "/systems/nds/firmware/bios7/remove", "/settings/appearance", "/settings/uploads",
		"/settings/updates", "/settings/updates/check", "/settings/updates/install", "/settings/name", "/settings/password", "/settings/security/renew-cert", "/library/rescan",
		"/settings/network/listen_port", "/settings/network/listen_port/reset", "/settings/network/restart"} {
		if rec := anon.postForm(p, url.Values{}, nil); rec.Code != http.StatusSeeOther || location(rec) != "/login" {
			t.Fatalf("%s: %d -> %q", p, rec.Code, location(rec))
		}
	}
	// A regular user has no password and cannot reach the pages.
	e.svc.CreateUser(bg, "anna", "Anna")
	c := e.client()
	c.login()
	for _, p := range []string{"/users", "/systems", "/settings"} {
		rec := c.get(p, nil)
		status(t, rec, 200)
		contains(t, rec, "Systems &amp; Cores", `href="/users"`, "Library", "Saves", "Clients", "Settings", "/static/htmx.min.js")
		notContains(t, rec, "Stream", "<no value>")
		if inlineRe.MatchString(rec.Body.String()) {
			t.Fatalf("%s: inline script/style", p)
		}
	}
	contains(t, c.get("/users", nil), `href="/users" class="active"`, "Onboarding invites", "Accounts apply only on this Hub")
	// Systems & Cores: list | detail with tabs; the firmware tab is the default.
	rec := c.get("/systems", nil)
	contains(t, rec, `href="/systems" class="active"`, "Nintendo DS", "melonDS DS 1.4.0", "Included in the Player", "ARM7 BIOS", "ARM9 BIOS", "DS Firmware",
		"Search systems…", "● Ready", `aria-selected="true"`, `id="systems-list"`, `id="systems-detail"`, "Provided by the admin")
	notContains(t, rec, "LATER", "Core package cache", "Reported by Players")
	rec = c.get("/systems?sys=nds&tab=core", nil)
	contains(t, rec, "melonds_ds", "windows-x86_64", "Core package cache", "Check source now", "No core packages known yet", `<option value="">any version</option>`,
		"1.4.0 (not in source)", "File extensions", "3 files · see the Firmware tab")
	contains(t, c.get("/systems?sys=nds&tab=clients", nil), "Reported by Players", "No client has reported yet")
}

func TestAdminPagesCSRF(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	u, _ := e.svc.CreateUser(bg, "max", "Max")
	paths := []string{"/users/invites", "/users/" + u.ID + "/disable", "/users/" + u.ID + "/enable", "/users/" + u.ID + "/delete", "/clients/devices/" + uuid.NewString() + "/delete", "/users/invites/" + uuid.NewString() + "/revoke",
		"/systems/nds/expected-version", "/systems/nds/firmware-mode", "/systems/nds/firmware/bios7/pin", "/systems/nds/firmware/bios7/remove",
		"/settings/appearance", "/settings/uploads", "/settings/security/renew-cert", "/library/rescan", "/cores/sync", "/settings/updates", "/settings/updates/check", "/settings/updates/install",
		"/settings/network/listen_port", "/settings/network/listen_port/reset", "/settings/network/restart"}
	for _, p := range paths {
		status(t, c.postForm(p, url.Values{"mode": {"dark"}, "enabled": {"1"}}, nil), 403)
		status(t, c.postForm(p, url.Values{"_csrf": {"wrong"}}, nil), 403)
		status(t, c.postForm(p, url.Values{}, map[string]string{"X-CSRF-Token": "wrong"}), 403)
	}
	body, ct := multipartBody(map[string]string{"_csrf": "wrong"}, "x.bin", bytes.Repeat([]byte{1}, 16384))
	status(t, c.do("POST", "/systems/nds/firmware/bios7/upload", body, map[string]string{"Content-Type": ct}), 403)
	body, ct = multipartBody(nil, "x.bin", bytes.Repeat([]byte{1}, 16384))
	status(t, c.do("POST", "/systems/nds/firmware/bios7/upload", body, map[string]string{"Content-Type": ct}), 403)
	if got, _ := e.svc.GetUser(bg, u.ID); got.Disabled() {
		t.Fatal("disabled despite missing CSRF")
	}
	if _, err := e.svc.GetUser(bg, u.ID); err != nil {
		t.Fatal("deleted despite missing CSRF")
	}
	if a, _ := e.svc.Appearance(bg); a != hub.AppearanceLight {
		t.Fatal("appearance changed despite missing CSRF")
	}
	if invs, _ := e.svc.ListInvites(bg, 0); len(invs) != 0 {
		t.Fatal("invite created despite missing CSRF")
	}
	if e2, _ := e.svc.GetRegistryEntry(bg, "nds"); e2.Firmware[0].Present {
		t.Fatal("firmware stored despite missing CSRF")
	}
	// With the token everything works.
	status(t, postTok(c, tok, "/settings/uploads", url.Values{"enabled": {"1"}}), 303)
	status(t, c.postForm("/settings/appearance", url.Values{"mode": {"dark"}}, map[string]string{"X-CSRF-Token": tok}), 303)
}

func TestUsersPageInvitesAndDisable(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")

	// Create: the code is shown once, only in this response.
	rec := postTok(c, tok, "/users/invites", url.Values{"expiry": {"15m"}}) // authorize unchecked
	status(t, rec, 200)
	code := codeRe.FindString(rec.Body.String())
	if code == "" {
		t.Fatalf("no code in: %.600s", rec.Body.String())
	}
	contains(t, rec, "expires in 1", "Shown once")
	rec = c.get("/users", nil)
	notContains(t, rec, code)
	contains(t, rec, "First device needs admin approval", "Revoke")
	invs, _ := e.svc.ListInvites(bg, 0)
	if len(invs) != 1 || invs[0].AuthorizeDevice || invs[0].ExpiresAt.Sub(invs[0].CreatedAt) != 15*time.Minute {
		t.Fatalf("%+v", invs)
	}
	// Default: 1 h, authorize on.
	rec = postTok(c, tok, "/users/invites", url.Values{"authorize": {"1"}, "expiry": {"bogus"}})
	status(t, rec, 200)
	code2 := codeRe.FindString(rec.Body.String())
	invs, _ = e.svc.ListInvites(bg, 0)
	if len(invs) != 2 || !invs[0].AuthorizeDevice || invs[0].ExpiresAt.Sub(invs[0].CreatedAt) != time.Hour {
		t.Fatalf("%+v", invs)
	}

	// Revoke the first one; it shows up in the history.
	rec = postTok(c, tok, "/users/invites/"+invs[1].ID+"/revoke", nil)
	if rec.Code != 303 || location(rec) != "/users?ok=revoked" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	rec = postTok(c, tok, "/users/invites/"+invs[1].ID+"/revoke", nil)
	if location(rec) != "/users?err=noinvite" {
		t.Fatalf("%q", location(rec))
	}
	contains(t, c.get("/users?ok=revoked", nil), "Invite revoked.", "revoked")

	// Redeem the second one: the user appears, the invite moves to the history.
	res, err := e.svc.RedeemInvite(bg, hub.RedeemInput{Code: code2, DisplayName: "Lena", DeviceID: uuid.NewString(), DeviceName: "Lena PC",
		Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.3"})
	if err != nil {
		t.Fatal(err)
	}
	rec = c.get("/users", nil)
	contains(t, rec, "redeemed by Lena", "Lena", "Disable", "Active")
	notContains(t, rec, code2)

	// Disable / enable; admins and the own account are protected.
	status(t, postTok(c, tok, "/users/"+res.UserID+"/disable", nil), 303)
	if u, _ := e.svc.GetUser(bg, res.UserID); !u.Disabled() {
		t.Fatal("not disabled")
	}
	rec = c.get("/users", nil)
	contains(t, rec, "Disabled", "Enable", "Disabled users cannot sign in on any device. Saves and uploads are preserved.")
	if rec := postTok(c, tok, "/users/"+admin.ID+"/disable", nil); location(rec) != "/users?err=admin" {
		t.Fatalf("%q", location(rec))
	}
	contains(t, c.get("/users?err=admin", nil), "Admins cannot be disabled or deleted.")
	if a, _ := e.svc.GetUser(bg, admin.ID); a.Disabled() {
		t.Fatal("admin disabled")
	}
	if rec := postTok(c, tok, "/users/u_nobody/disable", nil); location(rec) != "/users?err=nouser" {
		t.Fatalf("%q", location(rec))
	}
	status(t, postTok(c, tok, "/users/"+res.UserID+"/enable", nil), 303)
	if u, _ := e.svc.GetUser(bg, res.UserID); u.Disabled() {
		t.Fatal("still disabled")
	}
	// Admin row has no action.
	if strings.Contains(c.get("/users", nil).Body.String(), "/users/"+admin.ID+"/disable") {
		t.Fatal("admin row offers Disable")
	}
	_ = code
}

func TestClientsAssignUserDefaults(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")
	_, code, _ := e.svc.CreateInvite(bg, admin.ID, time.Hour, false)
	res, err := e.svc.RedeemInvite(bg, hub.RedeemInput{Code: code, DisplayName: "Jonas", DeviceID: uuid.NewString(), DeviceName: "Jonas PC",
		Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.3"})
	if err != nil {
		t.Fatal(err)
	}
	plain := pending(t, e, uuid.NewString())
	rec := c.get("/clients", nil)
	contains(t, rec, "Assign user", "Jonas PC")
	// Pre-assigned request: its user is selected; a plain request defaults to the admin.
	sel := regexp.MustCompile(`<option value="([^"]+)" selected>`).FindAllStringSubmatch(rec.Body.String(), -1)
	if len(sel) != 2 {
		t.Fatalf("selected options: %v", sel)
	}
	got := map[string]bool{sel[0][1]: true, sel[1][1]: true}
	if !got[res.UserID] || !got[admin.ID] {
		t.Fatalf("selected %v, want %s and %s", got, res.UserID, admin.ID)
	}
	// Allow with another user assigns the device to that user.
	other, _ := e.svc.CreateUser(bg, "max", "Max")
	rec = c.postForm("/clients/requests/"+plain.RequestID+"/allow", url.Values{"user_id": {other.ID}, "_csrf": {tok}}, map[string]string{"HX-Request": "true", "HX-Target": "clients-body"})
	status(t, rec, 200)
	contains(t, rec, "Device allowed")
	if _, err := e.svc.PollPairing(bg, plain.RequestID, plain.PollToken); err != nil {
		t.Fatal(err)
	}
	if d, err := e.svc.ListDevices(bg); err != nil || len(d) != 1 || d[0].UserID != other.ID {
		t.Fatalf("%v %+v", err, d)
	}
	contains(t, c.get("/clients", nil), "<th>User</th>", ">Max</td>")
	// Disabled users are not offered.
	e.svc.DisableUser(bg, other.ID)
	rec = c.get("/clients", nil)
	notContains(t, rec, `>Max</option>`)
	contains(t, rec, "<th>User</th>")
}

func TestSettingsAppearanceAndUploadToggle(t *testing.T) {
	e := newEnv(t, true, nil)
	anon := e.client()
	contains(t, anon.get("/login", nil), `data-theme="light"`)
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/general", nil)
	contains(t, rec, `data-theme="light"`, "Appearance", "Applies to this web interface", "Allow users to upload games", "Inactive",
		"Currently only admins can upload ROMs.")
	// Dark: stored as Hub setting and applied to every page, including login/setup for visitors.
	status(t, postTok(c, tok, "/settings/appearance", url.Values{"mode": {"dark"}}), 303)
	for _, p := range []string{"/settings", "/library", "/saves", "/clients", "/users", "/systems"} {
		contains(t, c.get(p, nil), `data-theme="dark"`)
	}
	contains(t, anon.get("/login", nil), `data-theme="dark"`)
	status(t, postTok(c, tok, "/settings/appearance", url.Values{"mode": {"system"}}), 303)
	contains(t, anon.get("/login", nil), `data-theme="system"`)
	status(t, postTok(c, tok, "/settings/appearance", url.Values{"mode": {"neon"}}), 400)
	if a, _ := e.svc.Appearance(bg); a != "system" {
		t.Fatalf("appearance %q", a)
	}
	css := c.get("/static/app.css", nil).Body.String()
	for _, want := range []string{`:root[data-theme="dark"]`, `prefers-color-scheme: dark`, `:root[data-theme="system"]`} {
		if !strings.Contains(css, want) {
			t.Fatalf("CSS lacks %s", want)
		}
	}
	// Upload permission.
	status(t, postTok(c, tok, "/settings/uploads", url.Values{"enabled": {"1"}}), 303)
	rec = c.get("/settings/general?ok=uploads", nil)
	contains(t, rec, "Active", "Users can upload ROMs. Uploads are tagged with uploaded_by", "Upload setting saved.")
	if on, _ := e.svc.AllowUserUploads(bg); !on {
		t.Fatal("not on")
	}
	status(t, postTok(c, tok, "/settings/uploads", url.Values{"enabled": {"0"}}), 303)
	if on, _ := e.svc.AllowUserUploads(bg); on {
		t.Fatal("not off")
	}
}

func TestSystemsPageFirmwareFlow(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")
	notContains(t, c.get("/systems", nil), "issues</span>") // no badge in builtin mode

	// Switch to native: all three files are required and missing -> badge on every page.
	status(t, postTok(c, tok, "/systems/nds/firmware-mode", url.Values{"mode": {"native"}}), 303)
	rec := c.get("/library", nil)
	contains(t, rec, `class="badge error">3 issues</span>`)
	rec = c.get("/systems", nil)
	contains(t, rec, "○ Missing", "required", "Provide")
	rec = postTok(c, tok, "/systems/nds/firmware-mode", url.Values{"mode": {"bad"}})
	status(t, rec, 400)

	// Wrong size: error text, nothing stored.
	rec = c.multipartPost("/systems/nds/firmware/bios7/upload", map[string]string{"_csrf": tok}, "any.bin", bytes.Repeat([]byte{9}, 100))
	status(t, rec, 400)
	contains(t, rec, "ARM7 BIOS must be 16384 bytes")
	// Right size (dummy bytes): valid, badge drops to 2.
	dummy7 := bytes.Repeat([]byte{9}, 16384)
	rec = c.multipartPost("/systems/nds/firmware/bios7/upload", map[string]string{"_csrf": tok}, "any.bin", dummy7)
	if rec.Code != 303 || location(rec) != "/systems?sys=nds&tab=firmware&ok=fwfile" {
		t.Fatalf("%d %q %.200s", rec.Code, location(rec), rec.Body.String())
	}
	rec = c.get("/systems?ok=fwfile", nil)
	contains(t, rec, "✓ Valid", "Replace", "Remove", "Firmware file saved.", `>2 issues</span>`)
	if b := rec.Body.String(); strings.Contains(b, string(dummy7[:64])) {
		t.Fatal("firmware bytes in the page")
	}
	// Pin a differing hash: mismatch pill, badge 3; clear it again.
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/pin", url.Values{"sha256": {strings.Repeat("A", 64)}}), 303)
	rec = c.get("/systems", nil)
	contains(t, rec, "✕ Hash mismatch", `>3 issues</span>`, strings.Repeat("a", 4)+"…")
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/pin", url.Values{"sha256": {"nothex"}}), 400)
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/pin", url.Values{"sha256": {""}}), 303)
	contains(t, c.get("/systems", nil), "✓ Valid")
	// Remove.
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/remove", nil), 303)
	if rec := postTok(c, tok, "/systems/nds/firmware/bios7/remove", nil); location(rec) != "/systems?sys=nds&tab=firmware&err=nofile" {
		t.Fatalf("%q", location(rec))
	}
	if rec := postTok(c, tok, "/systems/nds/firmware/nope/pin", url.Values{"sha256": {""}}); location(rec) != "/systems?sys=nds&tab=firmware&err=nofile" {
		t.Fatalf("%q", location(rec))
	}
	if gone, _ := e.svc.GetRegistryEntry(bg, "nds"); gone.Firmware[0].Present {
		t.Fatal("still present")
	}
	// Missing file in the form.
	rec = c.multipartPost("/systems/nds/firmware/bios7/upload", map[string]string{"_csrf": tok}, "", nil)
	status(t, rec, 400)

	// Expected version and reported clients.
	dev := pairTestDevice(t, e, admin.ID, "Lena's gaming PC")
	hs := func(ver string) {
		t.Helper()
		cores := []hub.CoreReport{{ID: "melonds_ds", Version: ver}}
		if _, err := e.svc.Handshake(bg, dev, hub.HandshakeInput{Platform: "windows", Arch: "x86_64", PlayerVersion: "0.1.0",
			ProtocolVersion: 1, MinProtocolVersion: 1, Cores: &cores}); err != nil {
			t.Fatal(err)
		}
	}
	hs("1.3.0")
	rec = c.get("/systems", nil)
	contains(t, rec, "3 firmware", "✕ Not ready · 3 files missing or invalid", `<span class="count warn">1</span>`)
	rec = c.get("/systems?tab=clients", nil)
	contains(t, rec, "Lena&#39;s gaming PC", "Player 0.1.0 · melonDS DS 1.3.0", "▲ Core version mismatch · 1.4.0 expected", "launching stays allowed")
	notContains(t, rec, "✕ Core version mismatch") // a core mismatch only warns (ADR 0007 D3)
	status(t, postTok(c, tok, "/systems/nds/expected-version", url.Values{"version": {"1.3.0"}}), 303)
	contains(t, c.get("/systems?tab=clients", nil), "● compatible")
	status(t, postTok(c, tok, "/systems/nds/expected-version", url.Values{"version": {""}}), 303)
	if entry, _ := e.svc.GetRegistryEntry(bg, "nds"); entry.ExpectedCoreVersion != "" {
		t.Fatalf("%+v", entry)
	}
	contains(t, c.get("/systems?tab=core", nil), `<option value="" selected>any version</option>`)
	hs("9.9.9")
	contains(t, c.get("/systems?tab=clients", nil), "● compatible")
	if _, err := e.svc.Handshake(bg, dev, hub.HandshakeInput{Platform: "windows", Arch: "x86_64", PlayerVersion: "0.1.0",
		ProtocolVersion: 1, MinProtocolVersion: 1, Cores: &[]hub.CoreReport{}}); err != nil {
		t.Fatal(err)
	}
	contains(t, c.get("/systems?tab=clients", nil), "▲ Core missing", "not installed")
}

func TestSettingsCertificateExpiryWarning(t *testing.T) {
	for _, tc := range []struct {
		name   string
		in     time.Duration
		source string
		badge  bool
		hint   bool
	}{
		{"self-generated fine", 90 * 24 * time.Hour, "Self-generated", false, true},
		{"self-generated soon", 10 * 24 * time.Hour, "Self-generated", true, true},
		{"self-generated expired", -time.Hour, "Self-generated", true, true},
		{"own soon", 10 * 24 * time.Hour, "Own cert/key", true, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			e := newEnv(t, true, func(c *Config) {
				c.UseTLS, c.CertFingerprint, c.CertSource = true, "AA:BB", tc.source
				c.CertNotAfter = time.Now().Add(tc.in)
			})
			c := e.client()
			c.login()
			body := c.get("/settings/security", nil).Body.String()
			if got := strings.Contains(body, "Expires within 30 days"); got != tc.badge {
				t.Fatalf("badge=%v, want %v", got, tc.badge)
			}
			if got := strings.Contains(body, "must confirm the new value"); got != tc.hint {
				t.Fatalf("hint=%v, want %v", got, tc.hint)
			}
		})
	}
}

func TestSystemsPageCoreSource(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", bytes.Repeat([]byte{1}, 2048))
	src.AddPackage(t, "melonds_ds", "1.5.0", "linux-x64", bytes.Repeat([]byte{2}, 10))
	e := newEnvOpts(t, true, nil, func(o *hub.Options) { src.Apply(o) })
	c := e.client()
	tok := c.login()
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	var runs atomic.Int32
	go func() {
		e.svc.RunCoreSync(ctx, time.Hour, func(hub.CoreSyncReport, error) { runs.Add(1) })
		close(done)
	}()
	defer func() { cancel(); <-done }()
	wait := func(cond func() bool) {
		t.Helper()
		for i := 0; i < 500 && !cond(); i++ {
			time.Sleep(10 * time.Millisecond)
		}
		if !cond() {
			t.Fatal("timeout")
		}
	}
	wait(func() bool { return runs.Load() >= 1 })

	rec := c.get("/systems?tab=core", nil)
	contains(t, rec, src.IndexURL(), "Last error", "none", "melonds_ds", "1.5.0", "GPL-3.0", "yes", "no",
		`<option value="1.4.0" selected>1.4.0</option>`, `<option value="1.5.0">1.5.0</option>`, `<option value="">any version</option>`)
	notContains(t, rec, "(not in source)", "LATER", "No core packages known yet")

	// Changing the expected version downloads the newly selected version in the background.
	status(t, postTok(c, tok, "/systems/nds/expected-version", url.Values{"version": {"1.5.0"}}), 303)
	wait(func() bool {
		p, err := e.svc.GetCorePackage(bg, "melonds_ds", "1.5.0", "linux-x64")
		return err == nil && p.CachedFiles() == 2
	})

	// "Check source now": a CSRF-protected POST that triggers a sync; a failing source shows its error.
	status(t, e.client().postForm("/cores/sync", url.Values{}, nil), 303) // not signed in: redirect to login, no sync
	n := runs.Load()
	src.Down = true
	rec = postTok(c, tok, "/cores/sync", nil)
	if rec.Code != 303 || location(rec) != "/systems?ok=coresync" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	wait(func() bool { return runs.Load() > n })
	contains(t, c.get("/systems?tab=core&ok=coresync", nil), "Checking the core source", "Last error", "HTTP 503", "melonds_ds")
}

// ---- Settings: Updates ----

func updatesFeed(t *testing.T, version string) (url string, pub ed25519.PublicKey) {
	t.Helper()
	dir := t.TempDir()
	seed := bytes.Repeat([]byte{9}, 32)
	pub, _ = corepkg.PublicFromSeed(seed)
	data := []byte("dummy deb")
	sum := sha256.Sum256(data)
	name := "framebeam-hub_" + version + "_amd64.deb"
	if err := os.WriteFile(filepath.Join(dir, name), data, 0o644); err != nil {
		t.Fatal(err)
	}
	idx, err := updates.Marshal(updates.Index{Schema: 1, GeneratedAt: time.Now().UTC(), Releases: []updates.Release{{
		Product: "hub", Channel: "beta", Version: version, PublishedAt: time.Now().UTC(), ProtocolVersion: 1, MinProtocolVersion: 1,
		NotesURL: "https://example.org/notes", Artifacts: []updates.Artifact{{Platform: "linux-amd64", Kind: "deb", Name: name, Size: int64(len(data)),
			SHA256: hex.EncodeToString(sum[:]), URL: "file://" + filepath.Join(dir, name)}}}}})
	if err != nil {
		t.Fatal(err)
	}
	sig, _ := corepkg.Sign(idx, seed)
	p := filepath.Join(dir, "updates-index.json")
	os.WriteFile(p, idx, 0o644)
	os.WriteFile(p+".sig", sig, 0o644)
	return "file://" + p, pub
}

func TestSettingsUpdatesSection(t *testing.T) {
	feed, pub := updatesFeed(t, "0.3.0-beta.6")
	reqDir := t.TempDir()
	exe := updates.PackagedExecutable
	e := newEnvOpts(t, true, nil, func(o *hub.Options) {
		o.HubVersion, o.UpdateChannel, o.UpdateIndexURL, o.UpdateRequestDir, o.UpdatePlatform = "0.3.0-beta.5", "beta", feed, reqDir, "linux-amd64"
		o.CoreTrustKeys = []ed25519.PublicKey{pub}
		o.Executable = exe
	})
	c := e.client()
	tok := c.login()
	rec := c.get("/settings", nil)
	status(t, rec, 200)
	contains(t, rec, "Versions, channel and automatic installs for this hub", `name="channel" value="beta" class="on"`, `role="switch" aria-checked="true"`,
		"0.3.0-beta.5", "Check now", "checks every hour")
	notContains(t, rec, "Install update")

	// Check now runs in the background; run the check directly and reload.
	status(t, postTok(c, tok, "/settings/updates/check", nil), 303)
	e.svc.SetUpdateSettings(bg, "beta", false) // automatic install would stage it right away
	if _, err := e.svc.CheckUpdates(bg); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/settings", nil)
	contains(t, rec, "Update available", "0.3.0-beta.6", "Release notes", "https://example.org/notes", `action="/settings/updates/install"`, "Install update",
		`hx-confirm="Install 0.3.0-beta.6 now?`, ">update</span>")

	// Saving: channel and automatic install, invalid channel rejected.
	rec = postTok(c, tok, "/settings/updates", url.Values{"channel": {"stable"}})
	status(t, rec, 303)
	if loc := location(rec); loc != "/settings/updates?ok=updates" {
		t.Fatal(loc)
	}
	if s, _ := e.svc.UpdateSettings(bg); s.Channel != "stable" || s.Auto {
		t.Fatalf("%+v", s)
	}
	status(t, postTok(c, tok, "/settings/updates", url.Values{"channel": {"nightly"}}), 400)

	// Install: stages and creates the request file.
	rec = postTok(c, tok, "/settings/updates/install", nil)
	status(t, rec, 303)
	if loc := location(rec); loc != "/settings/updates?ok=updateinstall" && loc != "/settings/updates?err=updatenone" {
		t.Fatal(loc)
	}
	e.svc.SetUpdateSettings(bg, "beta", false)
	rec = postTok(c, tok, "/settings/updates/install", nil)
	if location(rec) != "/settings/updates?ok=updateinstall" {
		t.Fatalf("install: %d %s", rec.Code, location(rec))
	}
	if !updates.RequestPending(reqDir) {
		t.Fatal("request file missing")
	}
	contains(t, c.get("/settings/updates?ok=updateinstall", nil), "Update downloaded and verified", "Installing")
}

func TestSettingsUpdatesNotPackagedAndDev(t *testing.T) {
	feed, pub := updatesFeed(t, "0.3.0-beta.6")
	e := newEnvOpts(t, true, nil, func(o *hub.Options) {
		o.HubVersion, o.UpdateChannel, o.UpdateIndexURL, o.UpdateRequestDir, o.UpdatePlatform = "0.3.0-alpha.1", "dev", feed, t.TempDir(), "linux-amd64"
		o.CoreTrustKeys = []ed25519.PublicKey{pub}
		o.Executable = "/usr/local/bin/framebeam-hub"
	})
	c := e.client()
	tok := c.login()
	rec := c.get("/settings", nil)
	contains(t, rec, ">Off</button>", "development builds")
	status(t, postTok(c, tok, "/settings/updates", url.Values{"channel": {"beta"}}), 303)
	if _, err := e.svc.CheckUpdates(bg); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/settings", nil)
	contains(t, rec, "Update available", "0.3.0-beta.6", "sudo apt install ./framebeam-hub_0.3.0-beta.6_amd64.deb")
	notContains(t, rec, "Install update")
	rec = postTok(c, tok, "/settings/updates/install", nil)
	if location(rec) != "/settings/updates?err=updatepackage" {
		t.Fatal(location(rec))
	}
	contains(t, c.get("/settings/updates?err=updatepackage", nil), "not installed from the .deb package")
}

func TestDeleteUserAndDeviceWeb(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-1234")
	u, _ := e.svc.CreateUser(bg, "max", "Max")

	// Users page: Delete link for regular users only.
	rec := c.get("/users", nil)
	contains(t, rec, `href="/users/`+u.ID+`/delete"`)
	notContains(t, rec, `/users/`+admin.ID+`/delete`)

	// Confirm page names what is removed and carries a CSRF-protected POST form.
	rec = c.get("/users/"+u.ID+"/delete", nil)
	status(t, rec, 200)
	contains(t, rec, "Delete “Max”?", "deleted for good", "Games they uploaded stay in the library",
		`action="/users/`+u.ID+`/delete"`, `name="_csrf"`, "Cancel")
	if rec := c.get("/users/"+admin.ID+"/delete", nil); location(rec) != "/users?err=admin" {
		t.Fatalf("%q", location(rec))
	}
	if rec := c.get("/users/u_nobody/delete", nil); location(rec) != "/users?err=nouser" {
		t.Fatalf("%q", location(rec))
	}
	// Admin and unknown user cannot be deleted.
	if rec := postTok(c, tok, "/users/"+admin.ID+"/delete", nil); location(rec) != "/users?err=admin" {
		t.Fatalf("%q", location(rec))
	}
	if rec := postTok(c, tok, "/users/u_nobody/delete", nil); location(rec) != "/users?err=nouser" {
		t.Fatalf("%q", location(rec))
	}
	if _, err := e.svc.GetUser(bg, admin.ID); err != nil {
		t.Fatal("admin deleted")
	}

	// Device delete: confirm page, then POST.
	dev := uuid.NewString()
	pr := pending(t, e, dev)
	if err := e.svc.ApprovePairing(bg, pr.RequestID, u.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.PollPairing(bg, pr.RequestID, pr.PollToken); err != nil {
		t.Fatal(err)
	}
	contains(t, c.get("/clients", nil), `href="/clients/devices/`+dev+`/delete"`)
	rec = c.get("/clients/devices/"+dev+"/delete", nil)
	status(t, rec, 200)
	contains(t, rec, "Lena Gaming PC", "Deleted device", `action="/clients/devices/`+dev+`/delete"`, `name="_csrf"`)
	if rec := postTok(c, tok, "/clients/devices/"+dev+"/delete", nil); location(rec) != "/clients?ok=devdeleted" {
		t.Fatalf("%q", location(rec))
	}
	if _, err := e.svc.GetDevice(bg, dev); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("device not deleted: %v", err)
	}
	contains(t, c.get("/clients?ok=devdeleted", nil), "Device deleted.")
	if rec := postTok(c, tok, "/clients/devices/"+dev+"/delete", nil); location(rec) != "/clients?err=nodevice" {
		t.Fatalf("%q", location(rec))
	}

	// User delete.
	if rec := postTok(c, tok, "/users/"+u.ID+"/delete", nil); location(rec) != "/users?ok=userdeleted" {
		t.Fatalf("%q", location(rec))
	}
	if _, err := e.svc.GetUser(bg, u.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("user not deleted: %v", err)
	}
	rec = c.get("/users?ok=userdeleted", nil)
	contains(t, rec, "User deleted")
	notContains(t, rec, "Max")
}
