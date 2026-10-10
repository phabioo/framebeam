package httpapi

import (
	"os"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
)

func localBody(user, pw string) map[string]any {
	return map[string]any{"username": user, "password": pw, "device_name": "Test-PC", "device_id": uuid.NewString(),
		"platform": "windows", "arch": "x86_64", "player_version": "0.9.0", "protocol_version": 1}
}

func TestLocalEndpointsRefuseNonLoopback(t *testing.T) {
	e := newEnv(t, nil)
	tok := e.login(uuid.NewString())
	hdr := map[string]string{"X-Forwarded-For": "127.0.0.1", "X-Real-IP": "127.0.0.1", "Forwarded": "for=127.0.0.1"}
	for _, remote := range []string{"192.0.2.10:4000", "[2001:db8::1]:4000", "10.0.0.5:1"} {
		e.remote = remote
		calls := []struct {
			method, path string
			body         any
			token        string
		}{
			{"GET", "/api/v1/local/status", nil, ""},
			{"POST", "/api/v1/local/setup", localBody("x", "long-enough-pw"), ""},
			{"POST", "/api/v1/local/pair", localBody("admin", "secret-12345"), ""},
			{"PUT", "/api/v1/local/settings", map[string]any{"network_sharing": false}, tok},
			{"PUT", "/api/v1/local/settings", map[string]any{"network_sharing": false}, ""},
		}
		for _, c := range calls {
			rec := e.do(c.method, c.path, c.body, opt{token: c.token, header: hdr})
			wantStatus(t, rec, 403, "")
			if errCode(t, rec) != "forbidden" {
				t.Fatalf("%s %s from %s: %s", c.method, c.path, remote, rec.Body.String())
			}
		}
	}
	if st, err := e.svc.LocalStatus(t.Context()); err != nil || !st.AdminExists {
		t.Fatalf("%v %v", st, err)
	}
}

func TestLocalEndpointsRefuseProxiedLoopback(t *testing.T) {
	e := newEnv(t, nil)
	e.remote = "127.0.0.1:4000"
	for _, h := range []string{"X-Forwarded-For", "X-Real-IP", "Forwarded", "X-Forwarded-Host"} {
		rec := e.do("POST", "/api/v1/local/pair", localBody("admin", "secret-12345"), opt{header: map[string]string{h: "203.0.113.9"}})
		wantStatus(t, rec, 403, "")
	}
}

func TestLocalSetupAndPair(t *testing.T) {
	e := newEnvNoAdmin(t, nil)
	e.remote = "127.0.0.1:5000"
	st := decode[map[string]any](t, e.do("GET", "/api/v1/local/status", nil, opt{}))
	if st["admin_exists"] != false || st["hub_version"] == "" {
		t.Fatalf("%v", st)
	}
	// Password rules are those of the web setup.
	wantStatus(t, e.do("POST", "/api/v1/local/setup", localBody("admin", "short"), opt{}), 400, "")
	wantStatus(t, e.do("POST", "/api/v1/local/pair", localBody("admin", "whatever-12345"), opt{}), 401, "")

	devID := uuid.NewString()
	body := localBody("admin", "secret-12345")
	body["device_id"] = devID
	rec := e.do("POST", "/api/v1/local/setup", body, opt{})
	wantStatus(t, rec, 200, "")
	cred := decode[map[string]any](t, rec)
	if cred["status"] != "approved" || cred["device_credential"] == "" || cred["user_id"] == "" || cred["hub_id"] == "" {
		t.Fatalf("%v", cred)
	}
	// The credential works like one from a normal pairing.
	tr := e.accessToken(devID, cred["device_credential"].(string))
	wantStatus(t, tr, 200, "")
	if decode[map[string]any](t, e.do("GET", "/api/v1/local/status", nil, opt{}))["admin_exists"] != true {
		t.Fatal("admin_exists")
	}
	e.clk.Advance(2 * time.Minute) // the per-IP attempt limit is 5 a minute
	// Second setup: an admin exists.
	wantStatus(t, e.do("POST", "/api/v1/local/setup", localBody("other", "secret-12345"), opt{}), 409, "")
	// Pair as the admin: wrong password 401, right one 200.
	wantStatus(t, e.do("POST", "/api/v1/local/pair", localBody("admin", "wrong-password"), opt{}), 401, "")
	e.clk.Advance(2 * time.Minute)
	rec = e.do("POST", "/api/v1/local/pair", localBody("admin", "secret-12345"), opt{})
	wantStatus(t, rec, 200, "")
	if decode[map[string]any](t, rec)["device_credential"] == "" {
		t.Fatal("no credential")
	}
	// A regular user (no password) cannot pair this way.
	if _, err := e.svc.CreateUser(t.Context(), "anna", "Anna"); err != nil {
		t.Fatal(err)
	}
	wantStatus(t, e.do("POST", "/api/v1/local/pair", localBody("anna", ""), opt{}), 401, "")
}

func TestLocalSettings(t *testing.T) {
	restarts := 0
	e := newEnvNoAdmin(t, nil, WithRestart(func() { restarts++ }))
	e.remote = "[::1]:5000"
	e.svc.SetLocalDefaults(hub.LocalDefaults{NetworkSharing: true})
	if _, err := e.svc.CreateAdmin(t.Context(), "admin", "secret-12345"); err != nil {
		t.Fatal(err)
	}
	devID := uuid.NewString()
	body := localBody("admin", "secret-12345")
	body["device_id"] = devID
	cred := decode[map[string]any](t, e.do("POST", "/api/v1/local/pair", body, opt{}))["device_credential"].(string)
	tok := decode[struct {
		AccessToken string `json:"access_token"`
	}](t, e.accessToken(devID, cred)).AccessToken

	wantStatus(t, e.do("PUT", "/api/v1/local/settings", map[string]any{"network_sharing": false}, opt{}), 401, "")
	rec := e.do("PUT", "/api/v1/local/settings", map[string]any{"network_sharing": false}, opt{token: tok})
	wantStatus(t, rec, 200, "")
	if decode[map[string]any](t, rec)["network_sharing"] != false || restarts != 1 {
		t.Fatalf("%s restarts=%d", rec.Body.String(), restarts)
	}
	// Same value again: nothing changes, no restart.
	wantStatus(t, e.do("PUT", "/api/v1/local/settings", map[string]any{"network_sharing": false}, opt{token: tok}), 200, "")
	if restarts != 1 {
		t.Fatalf("restarts=%d", restarts)
	}
	// Import folder: must be an absolute, existing directory.
	for _, bad := range []string{"relative/dir", t.TempDir() + "/missing", ""} {
		rec = e.do("PUT", "/api/v1/local/settings", map[string]any{"import_dir": bad}, opt{token: tok})
		wantStatus(t, rec, 400, "")
		if errCode(t, rec) != "import_dir_unreadable" {
			t.Fatalf("%q: %s", bad, rec.Body.String())
		}
	}
	f := t.TempDir() + "/file"
	if err := os.WriteFile(f, []byte("x"), 0o600); err != nil {
		t.Fatal(err)
	}
	wantStatus(t, e.do("PUT", "/api/v1/local/settings", map[string]any{"import_dir": f}, opt{token: tok}), 400, "")
	dir := t.TempDir()
	rec = e.do("PUT", "/api/v1/local/settings", map[string]any{"import_dir": dir, "network_sharing": true}, opt{token: tok})
	wantStatus(t, rec, 200, "")
	got := decode[map[string]any](t, rec)
	if got["import_dir"] != dir || got["network_sharing"] != true || restarts != 2 {
		t.Fatalf("%v restarts=%d", got, restarts)
	}
	if st := decode[map[string]any](t, e.do("GET", "/api/v1/local/status", nil, opt{})); st["import_dir"] != dir {
		t.Fatalf("%v", st)
	}
}
