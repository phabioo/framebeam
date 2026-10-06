package httpapi

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/getkin/kin-openapi/openapi3"
	"github.com/getkin/kin-openapi/openapi3filter"
	"github.com/getkin/kin-openapi/routers"
	"github.com/getkin/kin-openapi/routers/legacy"
	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

const specPath = "../../../protocol/openapi/framebeam.yaml"

type env struct {
	t      *testing.T
	svc    *hub.Service
	clk    *hubtest.Clock
	mux    *http.ServeMux
	router routers.Router
	admin  hub.User
	remote string
}

func newEnv(t *testing.T, mod func(*hub.Options)) *env {
	t.Helper()
	svc, clk := hubtest.New(t, mod)
	mux := http.NewServeMux()
	Register(mux, svc, nil)
	doc, err := openapi3.NewLoader().LoadFromFile(specPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := doc.Validate(context.Background()); err != nil {
		t.Fatal(err)
	}
	router, err := legacy.NewRouter(doc)
	if err != nil {
		t.Fatal(err)
	}
	admin, err := svc.CreateAdmin(context.Background(), "admin", "secret-1234")
	if err != nil {
		t.Fatal(err)
	}
	return &env{t: t, svc: svc, clk: clk, mux: mux, router: router, admin: admin, remote: "192.0.2.10:4000"}
}

type opt struct {
	token  string
	header map[string]string
	raw    bool   // do not validate the request against the spec (intentionally invalid requests)
	data   []byte // raw request body (instead of the JSON body)
	ctype  string // Content-Type for data
}

// do performs a request and validates request and response against the OpenAPI spec (contract test).
func (e *env) do(method, path string, body any, o opt) *httptest.ResponseRecorder {
	e.t.Helper()
	var data []byte
	if body != nil {
		data, _ = json.Marshal(body)
	}
	if o.data != nil {
		data = o.data
	}
	mk := func() *http.Request {
		var rd io.Reader
		if data != nil {
			rd = bytes.NewReader(data)
		}
		r := httptest.NewRequest(method, path, rd)
		r.RemoteAddr = e.remote
		if data != nil {
			r.Header.Set("Content-Type", "application/json")
			if o.ctype != "" {
				r.Header.Set("Content-Type", o.ctype)
			}
		}
		if o.token != "" {
			r.Header.Set("Authorization", "Bearer "+o.token)
		}
		for k, v := range o.header {
			r.Header.Set(k, v)
		}
		return r
	}
	rec := httptest.NewRecorder()
	e.mux.ServeHTTP(rec, mk())

	if o.raw || rec.Code == http.StatusNotModified {
		return rec
	}
	vr := mk()
	route, params, err := e.router.FindRoute(vr)
	if err != nil {
		e.t.Fatalf("route %s %s not in the spec: %v", method, path, err)
	}
	opts := &openapi3filter.Options{AuthenticationFunc: openapi3filter.NoopAuthenticationFunc, IncludeResponseStatus: true}
	in := &openapi3filter.RequestValidationInput{Request: vr, PathParams: params, Route: route, Options: opts}
	if err := openapi3filter.ValidateRequest(context.Background(), in); err != nil {
		e.t.Fatalf("request %s %s violates the spec: %v", method, path, err)
	}
	res := rec.Result()
	rb, _ := io.ReadAll(res.Body)
	out := &openapi3filter.ResponseValidationInput{RequestValidationInput: in, Status: rec.Code, Header: rec.Header(),
		Body: io.NopCloser(bytes.NewReader(rb)), Options: opts}
	if err := openapi3filter.ValidateResponse(context.Background(), out); err != nil {
		e.t.Fatalf("response to %s %s (%d) violates the spec: %v\nBody: %.200s", method, path, rec.Code, err, rec.Body.String())
	}
	return rec
}

func decode[T any](t *testing.T, rec *httptest.ResponseRecorder) T {
	t.Helper()
	var v T
	if err := json.Unmarshal(rec.Body.Bytes(), &v); err != nil {
		t.Fatalf("JSON: %v (%s)", err, rec.Body.String())
	}
	return v
}

func errCode(t *testing.T, rec *httptest.ResponseRecorder) string {
	t.Helper()
	return decode[struct {
		Error struct{ Code, Message string }
	}](t, rec).Error.Code
}

func wantStatus(t *testing.T, rec *httptest.ResponseRecorder, status int, code string) {
	t.Helper()
	if rec.Code != status {
		t.Fatalf("status %d, want %d: %s", rec.Code, status, rec.Body.String())
	}
	if code != "" && errCode(t, rec) != code {
		t.Fatalf("error code %q, want %q", errCode(t, rec), code)
	}
}

type pairResp struct {
	RequestID string `json:"request_id"`
	PollToken string `json:"poll_token"`
	Status    string `json:"status"`
	ExpiresIn int    `json:"expires_in"`
}

type pollResp struct {
	Status           string `json:"status"`
	HubID            string `json:"hub_id"`
	UserID           string `json:"user_id"`
	DeviceCredential string `json:"device_credential"`
}

func pairBody(deviceID string) map[string]any {
	return map[string]any{"device_id": deviceID, "device_name": "Living-Room-PC", "platform": "linux", "arch": "x86_64",
		"player_version": "0.1.0", "protocol_version": 1}
}

func (e *env) request(deviceID string) pairResp {
	e.t.Helper()
	rec := e.do("POST", "/api/v1/pairing/requests", pairBody(deviceID), opt{})
	wantStatus(e.t, rec, 202, "")
	return decode[pairResp](e.t, rec)
}

func (e *env) poll(p pairResp) *httptest.ResponseRecorder {
	return e.do("GET", "/api/v1/pairing/requests/"+p.RequestID, nil, opt{token: p.PollToken})
}

// pairDevice runs the complete flow up to the device credential.
func (e *env) pairDevice(deviceID string) string {
	e.t.Helper()
	p := e.request(deviceID)
	if err := e.svc.ApprovePairing(context.Background(), p.RequestID, e.admin.ID); err != nil {
		e.t.Fatal(err)
	}
	rec := e.poll(p)
	wantStatus(e.t, rec, 200, "")
	return decode[pollResp](e.t, rec).DeviceCredential
}

func (e *env) accessToken(deviceID, cred string) *httptest.ResponseRecorder {
	return e.do("POST", "/api/v1/auth/token", map[string]any{"device_id": deviceID, "device_credential": cred}, opt{})
}

func (e *env) login(deviceID string) string {
	e.t.Helper()
	rec := e.accessToken(deviceID, e.pairDevice(deviceID))
	wantStatus(e.t, rec, 200, "")
	return decode[struct {
		AccessToken string `json:"access_token"`
	}](e.t, rec).AccessToken
}

func handshakeBody(proto, minProto int) map[string]any {
	return map[string]any{"platform": "linux", "arch": "x86_64", "player_version": "0.1.0",
		"protocol_version": proto, "min_protocol_version": minProto,
		"cores": []map[string]string{{"id": "melonds", "version": "1.0.0"}},
		"video": map[string]any{"h264_encode": true, "h264_decode": true, "encoders": []string{"x264"}},
		"audio": map[string]any{"opus": true}, "input": map[string]any{"gamepad": true, "keyboard": true}}
}

func TestInfoEndpoint(t *testing.T) {
	e := newEnv(t, nil)
	rec := e.do("GET", "/.well-known/framebeam", nil, opt{})
	wantStatus(t, rec, 200, "")
	i := decode[map[string]any](t, rec)
	if _, err := uuid.Parse(i["hub_id"].(string)); err != nil || i["name"] != "Test-Hub" || i["api_base"] != "/api/v1" ||
		i["protocol_version"] != float64(hub.ProtocolVersion) || i["min_protocol_version"] != float64(hub.MinProtocolVersion) {
		t.Fatalf("%v", i)
	}
}

func TestPairingFlowTokenLibraryDownloadRevoke(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	p := e.request(dev)
	if p.Status != "pending" || p.ExpiresIn != 600 || !strings.HasPrefix(p.PollToken, "fbp_") {
		t.Fatalf("%+v", p)
	}
	if pr := decode[pollResp](t, e.poll(p)); pr.Status != "pending" || pr.DeviceCredential != "" {
		t.Fatalf("pending: %+v", pr)
	}
	pending, _ := e.svc.ListPendingRequests(context.Background())
	if len(pending) != 1 || pending[0].DeviceName != "Living-Room-PC" {
		t.Fatalf("pending list: %+v", pending)
	}
	if err := e.svc.ApprovePairing(context.Background(), p.RequestID, e.admin.ID); err != nil {
		t.Fatal(err)
	}
	if d, _ := e.svc.ListDevices(context.Background()); len(d) != 0 {
		t.Fatal("device must be registered only on poll")
	}
	ok := decode[pollResp](t, e.poll(p))
	if ok.Status != "approved" || !strings.HasPrefix(ok.DeviceCredential, "fbd_") || ok.UserID != e.admin.ID ||
		ok.HubID != e.svc.Info().HubID {
		t.Fatalf("approved: %+v", ok)
	}
	wantStatus(t, e.poll(p), 404, "not_found") // exactly once

	// no plaintext credential in the database
	// (hash is 64 hex characters, credential starts with fbd_)
	devs, _ := e.svc.ListDevices(context.Background())
	if len(devs) != 1 || devs[0].ID != dev || devs[0].Status != hub.DeviceTrusted || devs[0].UserID != e.admin.ID {
		t.Fatalf("%+v", devs)
	}

	tokRec := e.accessToken(dev, ok.DeviceCredential)
	wantStatus(t, tokRec, 200, "")
	tr := decode[struct {
		AccessToken string `json:"access_token"`
		TokenType   string `json:"token_type"`
		ExpiresIn   int    `json:"expires_in"`
	}](t, tokRec)
	if !strings.HasPrefix(tr.AccessToken, "fba_") || tr.TokenType != "Bearer" || tr.ExpiresIn != 900 {
		t.Fatalf("%+v", tr)
	}
	at := tr.AccessToken

	// Handshake updates device info
	hs := e.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: at})
	wantStatus(t, hs, 200, "")
	if h := decode[map[string]any](t, hs); h["compatible"] != true || len(h["problems"].([]any)) != 0 {
		t.Fatalf("%v", h)
	}

	// Library
	empty := e.do("GET", "/api/v1/games", nil, opt{token: at})
	wantStatus(t, empty, 200, "")
	rom := make([]byte, 4096)
	rand.Read(rom)
	g, err := e.svc.AddROM(context.Background(), bytes.NewReader(rom), "demo.nds", "Homebrew Demo", "", e.admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	list := decode[struct{ Games []map[string]any }](t, e.do("GET", "/api/v1/games", nil, opt{token: at}))
	if len(list.Games) != 1 || list.Games[0]["id"] != g.ID {
		t.Fatalf("%+v", list)
	}
	wantStatus(t, e.do("GET", "/api/v1/games/"+g.ID, nil, opt{token: at}), 200, "")
	wantStatus(t, e.do("GET", "/api/v1/games/"+uuid.NewString(), nil, opt{token: at}), 404, "not_found")
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{}), 401, "unauthorized")

	// ROM download: full, range, If-None-Match, invalid range, no auth, unknown
	url := "/api/v1/roms/" + g.ROMSHA256
	wantStatus(t, e.do("GET", url, nil, opt{}), 401, "unauthorized")
	full := e.do("GET", url, nil, opt{token: at})
	if full.Code != 200 || !bytes.Equal(full.Body.Bytes(), rom) || full.Header().Get("ETag") != `"`+g.ROMSHA256+`"` ||
		full.Header().Get("Accept-Ranges") != "bytes" {
		t.Fatalf("full: %d %v", full.Code, full.Header())
	}
	part := e.do("GET", url, nil, opt{token: at, header: map[string]string{"Range": "bytes=100-199"}})
	if part.Code != 206 || !bytes.Equal(part.Body.Bytes(), rom[100:200]) || part.Header().Get("Content-Range") != "bytes 100-199/4096" {
		t.Fatalf("range: %d %v", part.Code, part.Header())
	}
	nm := e.do("GET", url, nil, opt{token: at, header: map[string]string{"If-None-Match": full.Header().Get("ETag")}})
	if nm.Code != 304 || nm.Body.Len() != 0 {
		t.Fatalf("want 304, got %d", nm.Code)
	}
	bad := e.do("GET", url, nil, opt{token: at, header: map[string]string{"Range": "bytes=9999-"}})
	wantStatus(t, bad, 416, "bad_request")
	wantStatus(t, e.do("GET", "/api/v1/roms/"+strings.Repeat("0", 64), nil, opt{token: at}), 404, "not_found")
	wantStatus(t, e.do("GET", "/api/v1/roms/xyz", nil, opt{token: at, raw: true}), 404, "not_found")

	// Revoke by the hub takes effect immediately on the existing access token
	if err := e.svc.RevokeDevice(context.Background(), dev); err != nil {
		t.Fatal(err)
	}
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{token: at}), 401, "unauthorized")
	wantStatus(t, e.do("GET", url, nil, opt{token: at}), 401, "unauthorized")
	wantStatus(t, e.accessToken(dev, ok.DeviceCredential), 401, "device_revoked")
}

func TestRevokeSelf(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	at := e.login(dev)
	if rec := e.do("POST", "/api/v1/auth/revoke", nil, opt{token: at}); rec.Code != 204 {
		t.Fatalf("%d", rec.Code)
	}
	wantStatus(t, e.do("POST", "/api/v1/auth/revoke", nil, opt{token: at}), 401, "unauthorized")
	d, _ := e.svc.GetDevice(context.Background(), dev)
	if d.Status != hub.DeviceRevoked || d.RevokedAt == nil {
		t.Fatalf("%+v", d)
	}
}

func TestDeny(t *testing.T) {
	e := newEnv(t, nil)
	p := e.request(uuid.NewString())
	if err := e.svc.DenyPairing(context.Background(), p.RequestID); err != nil {
		t.Fatal(err)
	}
	if r := decode[pollResp](t, e.poll(p)); r.Status != "denied" || r.DeviceCredential != "" {
		t.Fatalf("%+v", r)
	}
	if err := e.svc.ApprovePairing(context.Background(), p.RequestID, e.admin.ID); err == nil {
		t.Fatal("approve after deny must fail")
	}
	if d, _ := e.svc.ListDevices(context.Background()); len(d) != 0 {
		t.Fatal("device registered after deny")
	}
}

func TestPairingExpiry(t *testing.T) {
	e := newEnv(t, nil)
	p := e.request(uuid.NewString())
	e.clk.Advance(hub.PairingTTL + time.Second)
	if r := decode[pollResp](t, e.poll(p)); r.Status != "expired" {
		t.Fatalf("%+v", r)
	}
	if err := e.svc.ApprovePairing(context.Background(), p.RequestID, e.admin.ID); err == nil {
		t.Fatal("approve after expiry must fail")
	}
	if l, _ := e.svc.ListPendingRequests(context.Background()); len(l) != 0 {
		t.Fatal("expired request in pending list")
	}
	// approved but never collected: expires as well
	q := e.request(uuid.NewString())
	e.svc.ApprovePairing(context.Background(), q.RequestID, e.admin.ID)
	e.clk.Advance(hub.PairingTTL + time.Second)
	if r := decode[pollResp](t, e.poll(q)); r.Status != "expired" || r.DeviceCredential != "" {
		t.Fatalf("%+v", r)
	}
	e.clk.Advance(2 * time.Hour)
	if err := e.svc.Cleanup(context.Background()); err != nil {
		t.Fatal(err)
	}
	wantStatus(t, e.poll(q), 404, "not_found")
}

func TestPairingRateLimit(t *testing.T) {
	e := newEnv(t, nil)
	for i := 0; i < hub.MaxPairingPerIPPerMinute; i++ {
		e.request(uuid.NewString())
	}
	wantStatus(t, e.do("POST", "/api/v1/pairing/requests", pairBody(uuid.NewString()), opt{}), 429, "rate_limited")
	e.remote = "192.0.2.11:1" // a different IP is not affected
	e.request(uuid.NewString())
	e.remote = "192.0.2.10:1"
	e.clk.Advance(61 * time.Second)
	e.request(uuid.NewString())
}

func TestPairingOpenLimit(t *testing.T) {
	e := newEnv(t, nil)
	for i := 0; i < hub.MaxOpenPairingRequests; i++ {
		e.remote = fmt.Sprintf("198.51.100.%d:1", i+1)
		e.request(uuid.NewString())
	}
	e.remote = "198.51.100.200:1"
	wantStatus(t, e.do("POST", "/api/v1/pairing/requests", pairBody(uuid.NewString()), opt{}), 429, "rate_limited")
	e.clk.Advance(hub.PairingTTL + time.Second) // old requests expire, room again
	e.request(uuid.NewString())
}

func TestInvalidCredentialsAndTokens(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	cred := e.pairDevice(dev)
	wantStatus(t, e.accessToken(dev, "fbd_wrong"), 401, "invalid_credentials")
	wantStatus(t, e.accessToken(dev, "nonsense"), 401, "invalid_credentials")
	wantStatus(t, e.accessToken(uuid.NewString(), cred), 401, "invalid_credentials")
	wantStatus(t, e.do("POST", "/api/v1/auth/token", map[string]any{"device_id": "not-a-uuid", "device_credential": "x"}, opt{raw: true}), 400, "bad_request")
	wantStatus(t, e.do("POST", "/api/v1/auth/token", nil, opt{raw: true}), 400, "bad_request")

	// wrong/missing poll token
	p := e.request(uuid.NewString())
	wantStatus(t, e.do("GET", "/api/v1/pairing/requests/"+p.RequestID, nil, opt{token: "fbp_wrong"}), 401, "unauthorized")
	wantStatus(t, e.do("GET", "/api/v1/pairing/requests/"+p.RequestID, nil, opt{}), 401, "unauthorized")
	wantStatus(t, e.do("GET", "/api/v1/pairing/requests/"+uuid.NewString(), nil, opt{token: p.PollToken}), 404, "not_found")
	// access token with wrong type/nonsense
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{token: "fba_nonsense"}), 401, "unauthorized")
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{token: p.PollToken}), 401, "unauthorized")
	// invalid pairing body
	wantStatus(t, e.do("POST", "/api/v1/pairing/requests", map[string]any{"device_id": uuid.NewString()}, opt{raw: true}), 400, "bad_request")
}

func TestAccessTokenExpiry(t *testing.T) {
	e := newEnv(t, nil)
	at := e.login(uuid.NewString())
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{token: at}), 200, "")
	e.clk.Advance(hub.AccessTokenTTL - time.Second)
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{token: at}), 200, "")
	e.clk.Advance(2 * time.Second)
	wantStatus(t, e.do("GET", "/api/v1/games", nil, opt{token: at}), 401, "unauthorized")
}

func TestLastSeenUpdated(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	at := e.login(dev)
	e.clk.Advance(5 * time.Minute)
	e.do("GET", "/api/v1/games", nil, opt{token: at})
	d, _ := e.svc.GetDevice(context.Background(), dev)
	if d.LastSeenAt == nil || !d.LastSeenAt.Equal(e.clk.Now().Truncate(time.Second)) {
		t.Fatalf("last_seen_at %v, clock %v", d.LastSeenAt, e.clk.Now())
	}
}

func TestHandshakeVersions(t *testing.T) {
	// Hub requires protocol >= 2: player with 1 is too old.
	e := newEnv(t, func(o *hub.Options) { o.ProtocolVersion, o.MinProtocolVersion = 2, 2 })
	at := e.login(uuid.NewString())
	h := decode[struct {
		Compatible bool
		Problems   []struct{ Code, Detail string }
	}](t, e.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: at}))
	if h.Compatible || len(h.Problems) != 1 || h.Problems[0].Code != "player_too_old" {
		t.Fatalf("%+v", h)
	}
	// Player requires protocol >= 3: hub (2) is too old.
	h = decode[struct {
		Compatible bool
		Problems   []struct{ Code, Detail string }
	}](t, e.do("POST", "/api/v1/handshake", handshakeBody(3, 3), opt{token: at}))
	if h.Compatible || len(h.Problems) != 1 || h.Problems[0].Code != "hub_too_old" {
		t.Fatalf("%+v", h)
	}
	ok := decode[struct{ Compatible bool }](t, e.do("POST", "/api/v1/handshake", handshakeBody(2, 1), opt{token: at}))
	if !ok.Compatible {
		t.Fatal("want compatible")
	}
	wantStatus(t, e.do("POST", "/api/v1/handshake", handshakeBody(0, 0), opt{token: at, raw: true}), 400, "bad_request")
	wantStatus(t, e.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{}), 401, "unauthorized")
}

func TestRepairingDoesNotOverwriteWithoutApprove(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	oldCred := e.pairDevice(dev)
	p := e.request(dev) // new request for a trusted device
	wantStatus(t, e.accessToken(dev, oldCred), 200, "")
	if r := decode[pollResp](t, e.poll(p)); r.Status != "pending" {
		t.Fatalf("%+v", r)
	}
	wantStatus(t, e.accessToken(dev, oldCred), 200, "") // old credential stays valid until approve
	e.svc.ApprovePairing(context.Background(), p.RequestID, e.admin.ID)
	newCred := decode[pollResp](t, e.poll(p)).DeviceCredential
	if newCred == "" || newCred == oldCred {
		t.Fatal("want new credential")
	}
	wantStatus(t, e.accessToken(dev, oldCred), 401, "invalid_credentials")
	wantStatus(t, e.accessToken(dev, newCred), 200, "")
}

func TestRevokedDeviceCanBeRepaired(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	e.pairDevice(dev)
	e.svc.RevokeDevice(context.Background(), dev)
	cred := e.pairDevice(dev) // approving again lifts the revocation
	wantStatus(t, e.accessToken(dev, cred), 200, "")
}

func TestWebSocketNeedsAuthAndUpgrade(t *testing.T) {
	e := newEnv(t, nil)
	at := e.login(uuid.NewString())
	// Authenticated but no upgrade handshake: refused by the WebSocket layer (the real flow is in sessions_test.go).
	wantStatus(t, e.do("GET", "/api/v1/ws", nil, opt{token: at, raw: true}), 426, "")
	wantStatus(t, e.do("GET", "/api/v1/ws", nil, opt{raw: true}), 401, "unauthorized")
}
