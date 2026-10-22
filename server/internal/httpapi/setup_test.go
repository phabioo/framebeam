package httpapi

import (
	"sync"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
)

func setupBody(code, user, pw string) map[string]any {
	return map[string]any{"setup_code": code, "username": user, "password": pw, "device_name": "Wohnzimmer-PC", "device_id": uuid.NewString(),
		"platform": "windows", "arch": "x86_64", "player_version": "0.18.0", "protocol_version": 1}
}

func TestSetupInfoAndFeature(t *testing.T) {
	e := newEnvNoAdmin(t, nil)
	info := decode[map[string]any](t, e.do("GET", "/.well-known/uzume", nil, opt{}))
	if info["setup_required"] != true {
		t.Fatalf("setup_required: %v", info)
	}
	if _, err := e.svc.CreateAdmin(t.Context(), "admin", "secret-12345"); err != nil {
		t.Fatal(err)
	}
	info = decode[map[string]any](t, e.do("GET", "/.well-known/uzume", nil, opt{}))
	if info["setup_required"] != false {
		t.Fatalf("setup_required after setup: %v", info)
	}
	found := false
	for _, f := range hub.BaseFeatures() {
		found = found || f == hub.FeatureSetupCodeV1
	}
	if !found {
		t.Fatal("the handshake must advertise setup_code_v1")
	}
}

func TestSetupWithCodeEndpoint(t *testing.T) {
	e := newEnvNoAdmin(t, nil)
	e.remote = "192.0.2.50:4000" // any address: the code is the proof
	code, err := e.svc.NewSetupCode(t.Context())
	if err != nil {
		t.Fatal(err)
	}

	// A wrong code: 401 invalid_setup_code, the server stays open for setup.
	wantStatus(t, e.do("POST", "/api/v1/setup", setupBody("AAAA-AA", "max", "a-long-password-1"), opt{}), 401, "invalid_setup_code")
	// A field problem is 400 and does not use the code up.
	wantStatus(t, e.do("POST", "/api/v1/setup", setupBody(code.Code, "max", "short"), opt{}), 400, "bad_request")

	body := setupBody(code.Code, "max", "a-long-password-1")
	rec := e.do("POST", "/api/v1/setup", body, opt{})
	wantStatus(t, rec, 200, "")
	cred := decode[map[string]any](t, rec)
	if cred["status"] != "approved" || cred["device_credential"] == "" || cred["user_id"] == "" || cred["hub_id"] == "" {
		t.Fatalf("%v", cred)
	}
	wantStatus(t, e.accessToken(body["device_id"].(string), cred["device_credential"].(string)), 200, "")
	d, err := e.svc.GetDevice(t.Context(), body["device_id"].(string))
	if err != nil || d.SignInKind != hub.SignInPassword {
		t.Fatalf("%+v %v", d, err)
	}

	// Used up: the server is set up now.
	e.clk.Advance(2 * time.Minute)
	wantStatus(t, e.do("POST", "/api/v1/setup", setupBody(code.Code, "other", "a-long-password-1"), opt{}), 409, "already_set_up")
	if decode[map[string]any](t, e.do("GET", "/.well-known/uzume", nil, opt{}))["setup_required"] != false {
		t.Fatal("setup_required must be false")
	}
}

func TestSetupWithCodeExpiredIs410(t *testing.T) {
	e := newEnvNoAdmin(t, nil)
	e.remote = "192.0.2.51:4000"
	code, _ := e.svc.NewSetupCode(t.Context())
	e.clk.Advance(61 * time.Minute)
	wantStatus(t, e.do("POST", "/api/v1/setup", setupBody(code.Code, "max", "a-long-password-1"), opt{}), 410, "setup_code_expired")
	// The admin was not created.
	if req, _ := e.svc.SetupRequired(t.Context()); !req {
		t.Fatal("setup must still be required")
	}
}

func TestSetupWithCodeRateLimited(t *testing.T) {
	e := newEnvNoAdmin(t, nil)
	e.remote = "192.0.2.52:4000"
	if _, err := e.svc.NewSetupCode(t.Context()); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < hub.MaxSignInPerIPPerMinute; i++ {
		rec := e.do("POST", "/api/v1/setup", setupBody("AAAA-AA", "max", "a-long-password-1"), opt{})
		if rec.Code == 429 {
			t.Fatalf("try %d limited too early", i)
		}
	}
	wantStatus(t, e.do("POST", "/api/v1/setup", setupBody("AAAA-AA", "max", "a-long-password-1"), opt{}), 429, "rate_limited")
}

func TestSetupWithCodeParallel(t *testing.T) {
	e := newEnvNoAdmin(t, nil)
	e.remote = "192.0.2.53:4000"
	code, _ := e.svc.NewSetupCode(t.Context())
	const n = 4
	status := make([]int, n)
	var wg sync.WaitGroup
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			status[i] = e.do("POST", "/api/v1/setup", setupBody(code.Code, "admin", "a-long-password-1"), opt{}).Code
		}()
	}
	wg.Wait()
	ok, conflict := 0, 0
	for _, s := range status {
		switch s {
		case 200:
			ok++
		case 409:
			conflict++
		}
	}
	if ok != 1 || conflict != n-1 {
		t.Fatalf("statuses: %v", status)
	}
}
