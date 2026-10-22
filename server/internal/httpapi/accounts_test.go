package httpapi

import (
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
)

// confirm gives the admin device behind token a valid "Confirm it's you" (the test admin's password).
func (e *env) confirm(token string) {
	e.t.Helper()
	wantStatus(e.t, e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "secret-12345"}, opt{token: token}), 200, "")
}

func signInBody(user, pw, deviceID string) map[string]any {
	return map[string]any{"username": user, "password": pw, "device_id": deviceID, "device_name": "PC", "platform": "linux",
		"arch": "x86_64", "app_version": "0.18.0", "protocol_version": 1}
}

// signedIn signs a device in over HTTP and returns an access token.
func (e *env) signedIn(user, pw, deviceID string) string {
	e.t.Helper()
	rec := e.do("POST", "/api/v1/auth/sign-in", signInBody(user, pw, deviceID), opt{})
	wantStatus(e.t, rec, 200, "")
	r := decode[struct {
		Status           string `json:"status"`
		DeviceCredential string `json:"device_credential"`
	}](e.t, rec)
	if r.Status != "approved" {
		e.t.Fatalf("%+v", r)
	}
	tr := e.accessToken(deviceID, r.DeviceCredential)
	wantStatus(e.t, tr, 200, "")
	return decode[struct {
		AccessToken string `json:"access_token"`
	}](e.t, tr).AccessToken
}

func TestAccountSignInAndOwnAccountAPI(t *testing.T) {
	e := newEnv(t, nil)
	ctx := t.Context()
	anna, err := e.svc.CreateUser(ctx, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.CreateUser(ctx, "bob", "Bob"); err != nil {
		t.Fatal(err)
	}
	// Anna has no password yet: sign-in fails like for any wrong combination; she sets one from an approved device.
	for _, c := range [][2]string{{"anna", "whatever-password"}, {"nobody", "whatever-password"}} {
		rec := e.do("POST", "/api/v1/auth/sign-in", signInBody(c[0], c[1], uuid.NewString()), opt{})
		wantStatus(t, rec, 401, "invalid_credentials")
		if rec.Header().Get("WWW-Authenticate") == "" {
			t.Fatal("401 without WWW-Authenticate")
		}
		e.clk.Advance(time.Second)
	}
	dev1 := uuid.NewString()
	e.remote = "192.0.2.20:1"
	p := e.request(dev1)
	if err := e.svc.ApprovePairing(ctx, p.RequestID, anna.ID); err != nil {
		t.Fatal(err)
	}
	cred := decode[pollResp](t, e.poll(p)).DeviceCredential
	tok1 := decode[struct {
		AccessToken string `json:"access_token"`
	}](t, e.accessToken(dev1, cred)).AccessToken

	acc := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: tok1}))
	if acc["user_id"] != anna.ID || acc["username"] != "anna" || acc["role"] != "user" || acc["has_password"] != false ||
		acc["password_required"] != false || acc["must_change_password"] != false || acc["confirmed_until"] != nil || acc["confirm_locked_until"] != nil {
		t.Fatalf("%v", acc)
	}
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{}), 401, "")

	// Rename: trimmed; a taken name is a conflict.
	rec := e.do("PATCH", "/api/v1/account", map[string]any{"display_name": "  Annika "}, opt{token: tok1})
	wantStatus(t, rec, 200, "")
	if decode[map[string]any](t, rec)["display_name"] != "Annika" {
		t.Fatalf("%s", rec.Body)
	}
	wantStatus(t, e.do("PATCH", "/api/v1/account", map[string]any{"display_name": "BOB"}, opt{token: tok1}), 409, "display_name_taken")
	wantStatus(t, e.do("PATCH", "/api/v1/account", map[string]any{"display_name": ""}, opt{token: tok1, raw: true}), 400, "bad_request")

	// Set a password: too short, then fine (no current password needed yet).
	wantStatus(t, e.do("PUT", "/api/v1/account/password", map[string]any{"new_password": "short"}, opt{token: tok1, raw: true}), 400, "bad_request")
	wantStatus(t, e.do("PUT", "/api/v1/account/password", map[string]any{"new_password": "anna-password-1"}, opt{token: tok1}), 204, "")
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: tok1})); a["has_password"] != true {
		t.Fatalf("%v", a)
	}
	// Change needs the current one.
	wantStatus(t, e.do("PUT", "/api/v1/account/password", map[string]any{"new_password": "anna-password-2"}, opt{token: tok1}), 401, "invalid_credentials")
	wantStatus(t, e.do("PUT", "/api/v1/account/password", map[string]any{"current_password": "anna-password-1", "new_password": "anna-password-2"}, opt{token: tok1}), 204, "")

	// Sign in on a new device with the password: no approval. Old tokens of that device id die on a repeated sign-in.
	dev2 := uuid.NewString()
	e.remote = "192.0.2.21:1"
	tok2 := e.signedIn("ANNA", "anna-password-2", dev2)
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok2}), 200, "")
	tok2b := e.signedIn("anna", "anna-password-2", dev2)
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok2}), 401, "")
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok2b}), 200, "")
	// A device id of another user is refused.
	wantStatus(t, e.do("POST", "/api/v1/auth/sign-in", signInBody("admin", "secret-12345", dev1), opt{}), 409, "conflict")

	// Own devices: both, with the kinds; the current one is marked.
	devs := decode[struct {
		Devices []struct {
			DeviceID   string  `json:"device_id"`
			SignInKind string  `json:"sign_in_kind"`
			Status     string  `json:"status"`
			Current    bool    `json:"current"`
			InstallID  *string `json:"install_id"`
		} `json:"devices"`
	}](t, e.do("GET", "/api/v1/account/devices", nil, opt{token: tok1}))
	if len(devs.Devices) != 2 {
		t.Fatalf("%+v", devs)
	}
	for _, d := range devs.Devices {
		want := map[string]string{dev1: "approval", dev2: "password"}[d.DeviceID]
		if d.SignInKind != want || d.Status != "trusted" || d.Current != (d.DeviceID == dev1) {
			t.Fatalf("%+v", d)
		}
	}
	// The admin's device is invisible and cannot be signed out.
	adminDev := uuid.NewString()
	adminTok := e.signedIn("admin", "secret-12345", adminDev)
	wantStatus(t, e.do("DELETE", "/api/v1/account/devices/"+adminDev, nil, opt{token: tok1}), 404, "not_found")
	wantStatus(t, e.do("DELETE", "/api/v1/account/devices/not-a-uuid", nil, opt{token: tok1, raw: true}), 404, "not_found")
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: adminTok}), 200, "")

	// Sign out one device: its token dies at once.
	wantStatus(t, e.do("DELETE", "/api/v1/account/devices/"+dev2, nil, opt{token: tok1}), 204, "")
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok2b}), 401, "")
	// Sign out all except the current one, then all including it.
	e.remote = "192.0.2.22:1"
	tok3 := e.signedIn("anna", "anna-password-2", uuid.NewString())
	rec = e.do("POST", "/api/v1/account/devices/sign-out-all", nil, opt{token: tok1})
	wantStatus(t, rec, 200, "")
	if decode[map[string]int](t, rec)["signed_out"] != 1 {
		t.Fatalf("%s", rec.Body)
	}
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok3}), 401, "")
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok1}), 200, "")
	rec = e.do("POST", "/api/v1/account/devices/sign-out-all", map[string]any{"include_current": true}, opt{token: tok1})
	wantStatus(t, rec, 200, "")
	if decode[map[string]int](t, rec)["signed_out"] != 1 {
		t.Fatalf("%s", rec.Body)
	}
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: tok1}), 401, "")
	wantStatus(t, e.do("GET", "/api/v1/account", nil, opt{token: adminTok}), 200, "") // the admin was not touched
}

func TestAccountConfirmAndRemovePasswordAPI(t *testing.T) {
	e := newEnv(t, nil)
	adminDev := uuid.NewString()
	adminTok := e.signedIn("admin", "secret-12345", adminDev)

	// Admin: the password can never be removed.
	rec := e.do("DELETE", "/api/v1/account/password", map[string]any{"current_password": "secret-12345"}, opt{token: adminTok})
	wantStatus(t, rec, 403, "password_required")

	// Confirm: not confirmed at first, bad duration, then confirmed with the default of 5 minutes.
	acc := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: adminTok}))
	if acc["confirmed_until"] != nil || acc["password_required"] != true || acc["role"] != "admin" {
		t.Fatalf("%v", acc)
	}
	wantStatus(t, e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "secret-12345", "duration_seconds": 60}, opt{token: adminTok, raw: true}), 400, "bad_request")
	rec = e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "secret-12345"}, opt{token: adminTok})
	wantStatus(t, rec, 200, "")
	until, err := time.Parse(time.RFC3339, decode[map[string]string](t, rec)["confirmed_until"])
	if err != nil || !until.Equal(e.clk.Now().Add(5*time.Minute)) {
		t.Fatalf("%v %v", until, err)
	}
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: adminTok})); a["confirmed_until"] == nil {
		t.Fatalf("%v", a)
	}
	wantStatus(t, e.do("DELETE", "/api/v1/account/confirm", nil, opt{token: adminTok}), 204, "")
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: adminTok})); a["confirmed_until"] != nil {
		t.Fatalf("%v", a)
	}

	// 3 wrong passwords: the third answers 429 with retry_after and the header.
	for i := 0; i < 2; i++ {
		wantStatus(t, e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "wrong-password-1"}, opt{token: adminTok}), 401, "invalid_credentials")
	}
	rec = e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "wrong-password-1"}, opt{token: adminTok})
	wantStatus(t, rec, 429, "rate_limited")
	ra := decode[struct {
		Error struct {
			RetryAfter int `json:"retry_after"`
		}
	}](t, rec).Error.RetryAfter
	if ra != 300 || rec.Header().Get("Retry-After") != strconv.Itoa(ra) {
		t.Fatalf("retry_after %d, header %q", ra, rec.Header().Get("Retry-After"))
	}
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: adminTok})); a["confirm_locked_until"] == nil {
		t.Fatalf("%v", a)
	}
	wantStatus(t, e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "secret-12345"}, opt{token: adminTok}), 429, "rate_limited")
	e.clk.Advance(5*time.Minute + time.Second)
	e.confirm(adminTok)

	// A member without a password cannot confirm; with a password she can remove it unless the policy forbids it.
	anna, _ := e.svc.CreateUser(t.Context(), "anna", "Anna")
	e.remote = "192.0.2.30:1"
	annaDev := uuid.NewString()
	p := e.request(annaDev)
	if err := e.svc.ApprovePairing(t.Context(), p.RequestID, anna.ID); err != nil {
		t.Fatal(err)
	}
	tok := decode[struct {
		AccessToken string `json:"access_token"`
	}](t, e.accessToken(annaDev, decode[pollResp](t, e.poll(p)).DeviceCredential)).AccessToken
	wantStatus(t, e.do("POST", "/api/v1/account/confirm", map[string]any{"password": "anything-12345"}, opt{token: tok}), 409, "password_not_set")
	wantStatus(t, e.do("PUT", "/api/v1/account/password", map[string]any{"new_password": "anna-password-1"}, opt{token: tok}), 204, "")

	// Policy (admin, needs confirmation; members are refused).
	wantStatus(t, e.do("GET", "/api/v1/admin/accounts/settings", nil, opt{token: tok}), 403, "forbidden")
	wantStatus(t, e.do("PUT", "/api/v1/admin/accounts/settings", map[string]any{"require_passwords": true}, opt{token: tok}), 403, "forbidden")
	if s := decode[map[string]any](t, e.do("GET", "/api/v1/admin/accounts/settings", nil, opt{token: adminTok})); s["require_passwords"] != false {
		t.Fatalf("%v", s)
	}
	e.clk.Advance(6 * time.Minute) // the admin's confirmation ran out
	wantStatus(t, e.do("PUT", "/api/v1/admin/accounts/settings", map[string]any{"require_passwords": true}, opt{token: adminTok}), 403, "confirmation_required")
	e.confirm(adminTok)
	wantStatus(t, e.do("PUT", "/api/v1/admin/accounts/settings", map[string]any{"require_passwords": true}, opt{token: adminTok}), 200, "")
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: tok})); a["password_required"] != true {
		t.Fatalf("%v", a)
	}
	wantStatus(t, e.do("DELETE", "/api/v1/account/password", map[string]any{"current_password": "anna-password-1"}, opt{token: tok}), 403, "password_required")
	wantStatus(t, e.do("PUT", "/api/v1/admin/accounts/settings", map[string]any{"require_passwords": false}, opt{token: adminTok}), 200, "")
	wantStatus(t, e.do("DELETE", "/api/v1/account/password", map[string]any{"current_password": "wrong-password-1"}, opt{token: tok}), 401, "invalid_credentials")
	wantStatus(t, e.do("DELETE", "/api/v1/account/password", map[string]any{"current_password": "anna-password-1"}, opt{token: tok}), 204, "")
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: tok})); a["has_password"] != false {
		t.Fatalf("%v", a)
	}
}

func TestAdminPasswordResetAPI(t *testing.T) {
	e := newEnv(t, nil)
	adminTok := e.signedIn("admin", "secret-12345", uuid.NewString())
	anna, _ := e.svc.CreateUser(t.Context(), "anna", "Anna")
	e.remote = "192.0.2.40:1"
	annaDev := uuid.NewString()
	p := e.request(annaDev)
	if err := e.svc.ApprovePairing(t.Context(), p.RequestID, anna.ID); err != nil {
		t.Fatal(err)
	}
	annaTok := decode[struct {
		AccessToken string `json:"access_token"`
	}](t, e.accessToken(annaDev, decode[pollResp](t, e.poll(p)).DeviceCredential)).AccessToken

	path := "/api/v1/admin/users/" + anna.ID + "/password-reset"
	wantStatus(t, e.do("POST", path, nil, opt{}), 401, "")
	wantStatus(t, e.do("POST", path, nil, opt{token: annaTok}), 403, "forbidden")
	wantStatus(t, e.do("POST", path, nil, opt{token: adminTok}), 403, "confirmation_required")
	e.confirm(adminTok)
	wantStatus(t, e.do("POST", "/api/v1/admin/users/"+e.admin.ID+"/password-reset", nil, opt{token: adminTok}), 403, "forbidden")
	wantStatus(t, e.do("POST", "/api/v1/admin/users/u_unknown/password-reset", nil, opt{token: adminTok}), 404, "not_found")
	rec := e.do("POST", path, nil, opt{token: adminTok})
	wantStatus(t, rec, 200, "")
	r := decode[struct {
		Temp string `json:"temporary_password"`
		Must bool   `json:"must_change_password"`
	}](t, rec)
	if !r.Must || len(strings.ReplaceAll(r.Temp, "-", "")) != 16 {
		t.Fatalf("%+v", r)
	}
	// Anna stays signed in, sees the flag, signs in elsewhere with the temporary password and changes it.
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: annaTok})); a["must_change_password"] != true || a["has_password"] != true {
		t.Fatalf("%v", a)
	}
	e.remote = "192.0.2.41:1"
	tok2 := e.signedIn("anna", r.Temp, uuid.NewString())
	wantStatus(t, e.do("PUT", "/api/v1/account/password", map[string]any{"current_password": r.Temp, "new_password": "my-own-password-1"}, opt{token: tok2}), 204, "")
	if a := decode[map[string]any](t, e.do("GET", "/api/v1/account", nil, opt{token: tok2})); a["must_change_password"] != false {
		t.Fatalf("%v", a)
	}
}

func TestSignInRateLimitsAndInviteRedeemWithPasswordAPI(t *testing.T) {
	e := newEnv(t, nil)
	// 10 failures lock the name: the tenth answers 429 with retry_after.
	var rec *httptest.ResponseRecorder
	for i := 0; i < hub.SignInFailureLimit; i++ {
		rec = e.do("POST", "/api/v1/auth/sign-in", signInBody("admin", "wrong-password-1", uuid.NewString()), opt{})
		if i < hub.SignInFailureLimit-1 {
			wantStatus(t, rec, 401, "invalid_credentials")
		}
		e.clk.Advance(7 * time.Second)
	}
	wantStatus(t, rec, 429, "rate_limited")
	if rec.Header().Get("Retry-After") != "900" {
		t.Fatalf("Retry-After %q", rec.Header().Get("Retry-After"))
	}
	wantStatus(t, e.do("POST", "/api/v1/auth/sign-in", signInBody("admin", "secret-12345", uuid.NewString()), opt{}), 429, "rate_limited")
	e.clk.Advance(16 * time.Minute)
	e.signedIn("admin", "secret-12345", uuid.NewString())

	// Invite redemption with a password.
	_, code, err := e.svc.CreateInvite(t.Context(), e.admin.ID, time.Hour, true)
	if err != nil {
		t.Fatal(err)
	}
	body := map[string]any{"code": code, "display_name": "Cleo", "device_id": uuid.NewString(), "device_name": "PC", "platform": "linux",
		"arch": "x86_64", "player_version": "0.18.0", "protocol_version": 1, "password": "short", "install_id": "inst-1"}
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", body, opt{raw: true}), 400, "bad_request")
	body["password"] = "cleo-password-1"
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", body, opt{}), 200, "")
	e.signedIn("cleo", "cleo-password-1", uuid.NewString())
}
