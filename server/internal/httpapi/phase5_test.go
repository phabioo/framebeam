package httpapi

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// Phase 5 over HTTP: invites, user disable, uploads, systems and firmware, handshake core warnings.
// All requests go through env.do and are validated against protocol/openapi/framebeam.yaml.

func redeemBody(code, name, deviceID string) map[string]any {
	return map[string]any{"code": code, "display_name": name, "device_id": deviceID, "device_name": name + "-PC",
		"platform": "linux", "arch": "x86_64", "player_version": "0.1.0", "protocol_version": 1}
}

func (e *env) newInvite(authorize bool) string {
	e.t.Helper()
	_, code, err := e.svc.CreateInvite(context.Background(), e.admin.ID, time.Hour, authorize)
	if err != nil {
		e.t.Fatal(err)
	}
	return code
}

func TestRedeemInviteApprovedOverHTTP(t *testing.T) {
	e := newEnv(t, nil)
	code := e.newInvite(true)
	dev := uuid.NewString()
	rec := e.do("POST", "/api/v1/invites/redeem", redeemBody(code, "Anna", dev), opt{})
	wantStatus(t, rec, 200, "")
	body := decode[map[string]string](t, rec)
	if body["status"] != "approved" || body["hub_id"] != e.svc.Info().HubID || !strings.HasPrefix(body["device_credential"], "fbd_") {
		t.Fatalf("%v", body)
	}
	u, err := e.svc.GetUser(context.Background(), body["user_id"])
	if err != nil || u.Role != hub.RoleUser || u.DisplayName != "Anna" {
		t.Fatalf("%v %+v", err, u)
	}
	// The new device works right away.
	tok := e.accessToken(dev, body["device_credential"])
	wantStatus(t, tok, 200, "")
	// Reuse of the code: 404 invite_invalid (no hint why).
	e.remote = "192.0.2.11:1"
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", redeemBody(code, "Bob", uuid.NewString()), opt{}), 404, "invite_invalid")
}

func TestRedeemInvitePendingOverHTTP(t *testing.T) {
	e := newEnv(t, nil)
	code := e.newInvite(false)
	dev := uuid.NewString()
	rec := e.do("POST", "/api/v1/invites/redeem", redeemBody(code, "Jonas", dev), opt{})
	wantStatus(t, rec, 202, "")
	p := decode[pairResp](t, rec)
	if p.Status != "pending" || p.RequestID == "" || !strings.HasPrefix(p.PollToken, "fbp_") || p.ExpiresIn != 600 {
		t.Fatalf("%+v", p)
	}
	pend, _ := e.svc.ListPendingRequests(context.Background())
	if len(pend) != 1 || pend[0].UserID == "" || pend[0].UserID == e.admin.ID {
		t.Fatalf("%+v", pend)
	}
	// Poll via the existing endpoint; after Allow the device gets its credential.
	if got := decode[pollResp](t, e.poll(p)); got.Status != "pending" {
		t.Fatalf("%+v", got)
	}
	if err := e.svc.ApprovePairing(context.Background(), p.RequestID, pend[0].UserID); err != nil {
		t.Fatal(err)
	}
	got := decode[pollResp](t, e.poll(p))
	if got.Status != "approved" || got.UserID != pend[0].UserID || got.DeviceCredential == "" {
		t.Fatalf("%+v", got)
	}
}

func TestRedeemInviteErrors(t *testing.T) {
	e := newEnv(t, nil)
	good := e.newInvite(true)
	// Taken display name (case-insensitive): 409, the invite stays valid.
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", redeemBody(good, "ADMIN", uuid.NewString()), opt{}), 409, "display_name_taken")
	// Bad request: missing display name length (spec says 1-32; send raw to skip request validation).
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", redeemBody(good, strings.Repeat("x", 40), uuid.NewString()), opt{raw: true}), 400, "bad_request")
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", redeemBody(good, "Ok", uuid.NewString()), opt{}), 200, "")
	// Rate limit like pairing: 5 per minute and IP.
	e.remote = "192.0.2.77:1"
	for i := 0; i < hub.MaxRedeemPerIPPerMinute; i++ {
		wantStatus(t, e.do("POST", "/api/v1/invites/redeem", redeemBody("FB-ZZZZ-ZZZZ", "Nobody", uuid.NewString()), opt{}), 404, "invite_invalid")
	}
	wantStatus(t, e.do("POST", "/api/v1/invites/redeem", redeemBody("FB-ZZZZ-ZZZZ", "Nobody", uuid.NewString()), opt{}), 429, "rate_limited")
}

// deviceWithCred pairs a device for a user and returns it with its credential.
func (s *sessEnv) deviceWithCred(userID string) (dev, string) {
	s.t.Helper()
	ctx := context.Background()
	id := uuid.NewString()
	s.ips++
	c, err := s.svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: id, DeviceName: "PC", Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: fmt.Sprintf("10.1.%d.%d", s.ips/250, s.ips%250)})
	if err != nil {
		s.t.Fatal(err)
	}
	s.svc.ApprovePairing(ctx, c.RequestID, userID)
	res, err := s.svc.PollPairing(ctx, c.RequestID, c.PollToken)
	if err != nil {
		s.t.Fatal(err)
	}
	tok, err := s.svc.IssueAccessToken(ctx, id, res.DeviceCredential)
	if err != nil {
		s.t.Fatal(err)
	}
	return dev{id: id, tok: tok.Token, user: userID, name: "PC"}, res.DeviceCredential
}

func TestUserDisabledEffects(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	annaDev, annaCred := s.deviceWithCred(s.anna.ID)
	bobDev := s.device(s.bob.ID, "Bob PC")
	wa, wb := s.dial(annaDev), s.dial(bobDev)
	sess := s.publish(annaDev, "hub_users")
	s.mustJoin(bobDev, sess.SessionID)
	wa.settle()
	wb.settle()
	pending := s.newInvite(true) // pending invites are unaffected

	if err := s.svc.DisableUser(ctx, s.admin.ID); err == nil {
		t.Fatal("admin must not be disableable")
	}
	if err := s.svc.DisableUser(ctx, s.anna.ID); err != nil {
		t.Fatal(err)
	}
	// WSS closed, Session ended with owner_disconnected for the viewer.
	wa.wantClosed()
	if m := wb.await("session_ended"); m.field("session_id") != sess.SessionID || m.field("reason") != "owner_disconnected" {
		t.Fatalf("%s", m.Payload)
	}
	wantStatus(t, s.get(bobDev, sess.SessionID), 410, "session_ended")
	// Every bearer call: 401 user_disabled, also the WSS upgrade.
	for _, c := range [][2]string{{"GET", "/api/v1/games"}, {"GET", "/api/v1/saves"}, {"GET", "/api/v1/systems"}, {"GET", "/api/v1/sessions"}, {"GET", "/api/v1/users"}} {
		wantStatus(t, s.do(c[0], c[1], nil, opt{token: annaDev.tok}), 401, "user_disabled")
	}
	wantStatus(t, s.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: annaDev.tok}), 401, "user_disabled")
	if _, resp, err := websocket.Dial(ctx, s.wsURL(), &websocket.DialOptions{HTTPHeader: http.Header{"Authorization": {"Bearer " + annaDev.tok}}}); err == nil || resp == nil || resp.StatusCode != 401 {
		t.Fatalf("dial: %v %v", resp, err)
	}
	// Token exchange: user_disabled, but only with a valid credential (no oracle for wrong ones).
	wantStatus(t, s.accessToken(annaDev.id, annaCred), 401, "user_disabled")
	wantStatus(t, s.accessToken(annaDev.id, "fbd_wrong"), 401, "invalid_credentials")
	// Other users are not affected; the invite can still be redeemed.
	wantStatus(t, s.do("GET", "/api/v1/games", nil, opt{token: bobDev.tok}), 200, "")
	wantStatus(t, s.do("POST", "/api/v1/invites/redeem", redeemBody(pending, "Cleo", uuid.NewString()), opt{}), 200, "")
	// Enabling restores access with the same token and credential.
	if err := s.svc.EnableUser(ctx, s.anna.ID); err != nil {
		t.Fatal(err)
	}
	wantStatus(t, s.do("GET", "/api/v1/games", nil, opt{token: annaDev.tok}), 200, "")
	wantStatus(t, s.accessToken(annaDev.id, annaCred), 200, "")
}

func (e *env) upload(token, query string, body []byte) *httptest.ResponseRecorder {
	e.t.Helper()
	return e.do("POST", "/api/v1/games?"+query, nil, opt{token: token, data: body, ctype: "application/octet-stream", raw: len(body) == 0})
}

func TestUploadPermissionMatrix(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	adminDev := s.device(s.admin.ID, "Admin PC")
	userDev := s.device(s.anna.ID, "Anna PC")
	features := func(d dev) []string {
		rec := s.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: d.tok})
		wantStatus(t, rec, 200, "")
		return decode[struct{ Features []string }](t, rec).Features
	}
	has := func(fs []string, f string) bool {
		for _, x := range fs {
			if x == f {
				return true
			}
		}
		return false
	}
	// Setting off: users 403 uploads_disabled and no uploads_v1; admin allowed.
	if has(features(userDev), "uploads_v1") || !has(features(userDev), "users_v1") || !has(features(userDev), "firmware_v1") {
		t.Fatalf("%v", features(userDev))
	}
	if !has(features(adminDev), "uploads_v1") {
		t.Fatalf("%v", features(adminDev))
	}
	rom := []byte("homebrew-dummy-rom-one")
	wantStatus(t, s.upload(userDev.tok, "filename=u.nds", rom), 403, "uploads_disabled")
	rec := s.upload(adminDev.tok, "filename=a.nds&title=Admin%20Game", []byte("homebrew-dummy-rom-admin"))
	wantStatus(t, rec, 201, "")
	body := decode[map[string]any](t, rec)
	if body["title"] != "Admin Game" || body["uploaded_by"] != s.admin.ID || body["system"] != "nds" {
		t.Fatalf("%v", body)
	}
	// Setting on: user 201 with uploaded_by = user, uploads_v1 advertised.
	if err := s.svc.SetAllowUserUploads(ctx, true); err != nil {
		t.Fatal(err)
	}
	if !has(features(userDev), "uploads_v1") {
		t.Fatal("uploads_v1 missing")
	}
	rec = s.upload(userDev.tok, "filename=u.nds", rom)
	wantStatus(t, rec, 201, "")
	ub := decode[map[string]any](t, rec)
	if ub["uploaded_by"] != s.anna.ID || ub["title"] != "u" {
		t.Fatalf("%v", ub)
	}
	rm := ub["rom"].(map[string]any)
	sum := sha256.Sum256(rom)
	if rm["sha256"] != hex.EncodeToString(sum[:]) || int(rm["size"].(float64)) != len(rom) || rm["filename"] != "u.nds" {
		t.Fatalf("%v", rm)
	}
	// Duplicate: 409 with the existing game id.
	dup := s.upload(adminDev.tok, "filename=copy.nds", rom)
	wantStatus(t, dup, 409, "conflict")
	if id := decode[map[string]any](t, dup)["existing_game_id"]; id != ub["id"] {
		t.Fatalf("existing_game_id %v, want %v", id, ub["id"])
	}
	// Validation: unknown extension, empty body, no token.
	wantStatus(t, s.upload(adminDev.tok, "filename=x.bin", []byte("abc")), 400, "bad_request")
	wantStatus(t, s.upload(adminDev.tok, "filename=e.nds", []byte{}), 400, "bad_request")
	wantStatus(t, s.upload("", "filename=n.nds", []byte("abc")), 401, "unauthorized")
	// The upload is in the library for everybody.
	list := decode[struct{ Games []map[string]any }](t, s.do("GET", "/api/v1/games", nil, opt{token: userDev.tok}))
	if len(list.Games) != 3 { // demo + admin + user upload
		t.Fatalf("%d games", len(list.Games))
	}
	// Turning the setting off again blocks users, admins stay allowed.
	s.svc.SetAllowUserUploads(ctx, false)
	wantStatus(t, s.upload(userDev.tok, "filename=u2.nds", []byte("two")), 403, "uploads_disabled")
	wantStatus(t, s.upload(adminDev.tok, "filename=a2.nds", []byte("two")), 201, "")
}

func TestUploadStreamsLargeBodyToDisk(t *testing.T) {
	s := newSessEnv(t, nil)
	d := s.device(s.admin.ID, "Admin PC")
	big := bytes.Repeat([]byte("0123456789abcdef"), 1<<18) // 4 MiB
	rec := s.upload(d.tok, "filename=big.nds", big)
	wantStatus(t, rec, 201, "")
	sum := sha256.Sum256(big)
	if got := decode[map[string]any](t, rec)["rom"].(map[string]any)["sha256"]; got != hex.EncodeToString(sum[:]) {
		t.Fatalf("hash %v", got)
	}
}

func TestSystemsAndFirmwareAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	d := s.device(s.anna.ID, "Anna PC") // regular user: any trusted device may read
	type sys struct {
		FirmwareMode        string  `json:"firmware_mode"`
		PreferredCoreID     string  `json:"preferred_core_id"`
		ExpectedCoreVersion *string `json:"expected_core_version"`
		Firmware            []struct {
			ID       string  `json:"id"`
			Required bool    `json:"required"`
			Present  bool    `json:"present"`
			Size     *int64  `json:"size"`
			SHA256   *string `json:"sha256"`
		} `json:"firmware"`
	}
	list := func() sys {
		rec := s.do("GET", "/api/v1/systems", nil, opt{token: d.tok})
		wantStatus(t, rec, 200, "")
		l := decode[struct{ Systems []sys }](t, rec).Systems
		if len(l) != 1 {
			t.Fatalf("%+v", l)
		}
		return l[0]
	}
	nds := list()
	if nds.FirmwareMode != "builtin" || nds.PreferredCoreID != "melonds_ds" || nds.ExpectedCoreVersion == nil || *nds.ExpectedCoreVersion != "1.4.0" || len(nds.Firmware) != 3 {
		t.Fatalf("%+v", nds)
	}
	for _, f := range nds.Firmware {
		if f.Required || f.Present || f.Size != nil || f.SHA256 != nil {
			t.Fatalf("%+v", f)
		}
	}
	wantStatus(t, s.do("GET", "/api/v1/systems", nil, opt{}), 401, "unauthorized")

	// Absent file: 404; unknown ids: 404.
	wantStatus(t, s.do("GET", "/api/v1/systems/nds/firmware/bios7", nil, opt{token: d.tok}), 404, "not_found")
	wantStatus(t, s.do("GET", "/api/v1/systems/nope/firmware/bios7", nil, opt{token: d.tok}), 404, "not_found")
	wantStatus(t, s.do("GET", "/api/v1/systems/nds/firmware/bios7", nil, opt{}), 401, "unauthorized")

	// Provide dummy files (right size only) and switch to native mode.
	b7 := bytes.Repeat([]byte{7}, 16384)
	if _, err := s.svc.ProvideFirmware(ctx, "nds", "bios7", bytes.NewReader(b7)); err != nil {
		t.Fatal(err)
	}
	s.svc.SetFirmwareMode(ctx, "nds", hub.FirmwareNative)
	s.svc.SetExpectedCoreVersion(ctx, "nds", "")
	nds = list()
	if nds.FirmwareMode != "native" || nds.ExpectedCoreVersion != nil || !nds.Firmware[0].Required || !nds.Firmware[0].Present ||
		*nds.Firmware[0].Size != 16384 || nds.Firmware[1].Present {
		t.Fatalf("%+v", nds)
	}
	sum := sha256.Sum256(b7)
	want := hex.EncodeToString(sum[:])
	if *nds.Firmware[0].SHA256 != want {
		t.Fatalf("sha %s", *nds.Firmware[0].SHA256)
	}

	rec := s.do("GET", "/api/v1/systems/nds/firmware/bios7", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	if !bytes.Equal(rec.Body.Bytes(), b7) || rec.Header().Get("ETag") != `"`+want+`"` || rec.Header().Get("Content-Type") != "application/octet-stream" {
		t.Fatalf("download: etag %q type %q len %d", rec.Header().Get("ETag"), rec.Header().Get("Content-Type"), rec.Body.Len())
	}
	if cc := rec.Header().Get("Cache-Control"); !strings.Contains(cc, "no-cache") {
		t.Fatalf("cache-control %q", cc)
	}
	// Conditional request.
	rec = s.do("GET", "/api/v1/systems/nds/firmware/bios7", nil, opt{token: d.tok, header: map[string]string{"If-None-Match": `"` + want + `"`}})
	if rec.Code != http.StatusNotModified {
		t.Fatalf("status %d", rec.Code)
	}
	// A disabled user gets 401 user_disabled here as well.
	s.svc.DisableUser(ctx, s.anna.ID)
	wantStatus(t, s.do("GET", "/api/v1/systems/nds/firmware/bios7", nil, opt{token: d.tok}), 401, "user_disabled")
}

func TestHandshakeCoreWarningsOverHTTP(t *testing.T) {
	e := newEnv(t, nil)
	dev := uuid.NewString()
	tok := e.login(dev)
	hs := func(cores []map[string]string) (bool, []map[string]any) {
		body := handshakeBody(1, 1)
		body["cores"] = cores
		rec := e.do("POST", "/api/v1/handshake", body, opt{token: tok})
		wantStatus(t, rec, 200, "")
		r := decode[struct {
			Compatible bool
			Problems   []map[string]any
		}](t, rec)
		return r.Compatible, r.Problems
	}
	ok, pr := hs([]map[string]string{})
	if !ok || len(pr) != 1 || pr[0]["code"] != "core_missing" || pr[0]["core_id"] != "melonds_ds" {
		t.Fatalf("%v %v", ok, pr)
	}
	ok, pr = hs([]map[string]string{{"id": "melonds_ds", "version": "1.3.9"}})
	if !ok || len(pr) != 1 || pr[0]["code"] != "core_version_mismatch" {
		t.Fatalf("%v %v", ok, pr)
	}
	reg, _ := e.svc.GetRegistryEntry(context.Background(), "nds")
	reps, _ := e.svc.ListClientReports(context.Background(), reg)
	if len(reps) != 1 || reps[0].CoreVersion != "1.3.9" || reps[0].Status != hub.ClientCoreMismatch {
		t.Fatalf("%+v", reps)
	}
	ok, pr = hs([]map[string]string{{"id": "melonds_ds", "version": "1.4.0"}})
	if !ok || len(pr) != 0 {
		t.Fatalf("%v %v", ok, pr)
	}
}
