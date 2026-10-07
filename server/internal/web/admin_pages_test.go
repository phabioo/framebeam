package web

import (
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"net/url"
	"regexp"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
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
		"/systems/nds/firmware-mode", "/systems/nds/firmware/bios7/pin", "/systems/nds/firmware/bios7/remove", "/settings/appearance", "/settings/uploads"} {
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
	contains(t, c.get("/systems", nil), `href="/systems" class="active"`, "Nintendo DS", "melonds_ds", "1.4.0", "Included in the Player",
		"windows-x86_64", "ARM7 BIOS", "ARM9 BIOS", "DS Firmware", "Core package cache", "Check source now", "No core packages known yet", `<option value="">any version</option>`, "1.4.0 (not in source)", "Reported by clients")
	notContains(t, c.get("/systems", nil), "LATER")
}

func TestAdminPagesCSRF(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	u, _ := e.svc.CreateUser(bg, "max", "Max")
	paths := []string{"/users/invites", "/users/" + u.ID + "/disable", "/users/" + u.ID + "/enable", "/users/invites/" + uuid.NewString() + "/revoke",
		"/systems/nds/expected-version", "/systems/nds/firmware-mode", "/systems/nds/firmware/bios7/pin", "/systems/nds/firmware/bios7/remove",
		"/settings/appearance", "/settings/uploads", "/cores/sync"}
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
	contains(t, c.get("/users?err=admin", nil), "Admins cannot be disabled.")
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
	rec := c.get("/settings", nil)
	contains(t, rec, `data-theme="light"`, "Appearance", "Applies to this web interface", "Allow users to upload games", "Inactive",
		"Currently only admins can upload ROMs.", "uploaded_by")
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
	rec = c.get("/settings?ok=uploads", nil)
	contains(t, rec, "Active", "Users can upload ROMs from the Player.", "Upload setting saved.")
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
	notContains(t, c.get("/systems", nil), "firmware</span>") // no badge in builtin mode

	// Switch to native: all three files are required and missing -> badge on every page.
	status(t, postTok(c, tok, "/systems/nds/firmware-mode", url.Values{"mode": {"native"}}), 303)
	rec := c.get("/library", nil)
	contains(t, rec, `class="badge error">3 firmware</span>`)
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
	if rec.Code != 303 || location(rec) != "/systems?ok=fwfile" {
		t.Fatalf("%d %q %.200s", rec.Code, location(rec), rec.Body.String())
	}
	rec = c.get("/systems?ok=fwfile", nil)
	contains(t, rec, "✓ Valid", "Replace", "Remove", "Firmware file saved.", `>2 firmware</span>`)
	if b := rec.Body.String(); strings.Contains(b, string(dummy7[:64])) {
		t.Fatal("firmware bytes in the page")
	}
	// Pin a differing hash: mismatch pill, badge 3; clear it again.
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/pin", url.Values{"sha256": {strings.Repeat("A", 64)}}), 303)
	rec = c.get("/systems", nil)
	contains(t, rec, "✕ Hash mismatch", `>3 firmware</span>`, strings.Repeat("a", 4)+"…")
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/pin", url.Values{"sha256": {"nothex"}}), 400)
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/pin", url.Values{"sha256": {""}}), 303)
	contains(t, c.get("/systems", nil), "✓ Valid")
	// Remove.
	status(t, postTok(c, tok, "/systems/nds/firmware/bios7/remove", nil), 303)
	if rec := postTok(c, tok, "/systems/nds/firmware/bios7/remove", nil); location(rec) != "/systems?err=nofile" {
		t.Fatalf("%q", location(rec))
	}
	if rec := postTok(c, tok, "/systems/nds/firmware/nope/pin", url.Values{"sha256": {""}}); location(rec) != "/systems?err=nofile" {
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
	contains(t, rec, "Lena&#39;s gaming PC", "Player 0.1.0 · melonDS DS 1.3.0", "Core version mismatch · 1.4.0 expected")
	status(t, postTok(c, tok, "/systems/nds/expected-version", url.Values{"version": {"1.3.0"}}), 303)
	contains(t, c.get("/systems", nil), "● compatible")
	status(t, postTok(c, tok, "/systems/nds/expected-version", url.Values{"version": {""}}), 303)
	if entry, _ := e.svc.GetRegistryEntry(bg, "nds"); entry.ExpectedCoreVersion != "" {
		t.Fatalf("%+v", entry)
	}
	contains(t, c.get("/systems", nil), `<option value="" selected>any version</option>`)
	hs("9.9.9")
	contains(t, c.get("/systems", nil), "● compatible")
	if _, err := e.svc.Handshake(bg, dev, hub.HandshakeInput{Platform: "windows", Arch: "x86_64", PlayerVersion: "0.1.0",
		ProtocolVersion: 1, MinProtocolVersion: 1, Cores: &[]hub.CoreReport{}}); err != nil {
		t.Fatal(err)
	}
	contains(t, c.get("/systems", nil), "Core missing", "not installed")
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
			body := c.get("/settings", nil).Body.String()
			if got := strings.Contains(body, "Expires within 30 days"); got != tc.badge {
				t.Fatalf("badge=%v, want %v", got, tc.badge)
			}
			if got := strings.Contains(body, "must confirm the new fingerprint"); got != tc.hint {
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

	rec := c.get("/systems", nil)
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
	contains(t, c.get("/systems?ok=coresync", nil), "Checking the core source", "Last error", "HTTP 503", "melonds_ds")
}
