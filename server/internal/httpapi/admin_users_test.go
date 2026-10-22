package httpapi

import (
	"context"
	"fmt"
	"strings"
	"testing"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
)

// adminAuthzTable checks that every route answers 401 without a token and 403 for a member token (the requests
// are not validated against the spec: some of them are sent without their required body on purpose).
func adminAuthzTable(t *testing.T, s *sessEnv, member dev, routes [][2]string) {
	t.Helper()
	for _, r := range routes {
		o := opt{raw: true}
		if r[0] != "GET" && r[0] != "DELETE" {
			o.data = []byte("{}")
		}
		wantStatus(t, s.do(r[0], r[1], nil, o), 401, "unauthorized")
		o.token = member.tok
		wantStatus(t, s.do(r[0], r[1], nil, o), 403, "forbidden")
	}
}

func TestAdminUsersDevicesRequireAdmin(t *testing.T) {
	s := newSessEnv(t, nil)
	member := s.device(s.anna.ID, "Annas PC")
	adminAuthzTable(t, s, member, [][2]string{
		{"GET", "/api/v1/admin/users"}, {"POST", "/api/v1/admin/users"}, {"PATCH", "/api/v1/admin/users/x"}, {"DELETE", "/api/v1/admin/users/x"},
		{"POST", "/api/v1/admin/users/x/code"}, {"POST", "/api/v1/admin/users/x/role"}, {"POST", "/api/v1/admin/users/x/disable"},
		{"POST", "/api/v1/admin/users/x/enable"},
		{"GET", "/api/v1/admin/invites"}, {"POST", "/api/v1/admin/invites"}, {"DELETE", "/api/v1/admin/invites/x"},
		{"GET", "/api/v1/admin/pairing-requests"}, {"POST", "/api/v1/admin/pairing-requests/x/allow"}, {"POST", "/api/v1/admin/pairing-requests/x/deny"},
		{"GET", "/api/v1/admin/devices"}, {"PATCH", "/api/v1/admin/devices/x"}, {"DELETE", "/api/v1/admin/devices/x"}, {"POST", "/api/v1/admin/devices/x/revoke"},
	})
	// Nothing changed by the refused calls.
	if u, err := s.svc.GetUser(context.Background(), s.anna.ID); err != nil || u.DisplayName != "Anna" {
		t.Fatalf("%v %+v", err, u)
	}
}

func TestHandshakeAdvertisesAdminV1ToAdminsOnly(t *testing.T) {
	s := newSessEnv(t, nil)
	features := func(d dev) []string {
		rec := s.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: d.tok})
		wantStatus(t, rec, 200, "")
		return decode[struct{ Features []string }](t, rec).Features
	}
	if f := features(s.device(s.admin.ID, "Admin PC")); !contains(f, "admin_v1") {
		t.Fatalf("admin: %v", f)
	}
	if f := features(s.device(s.anna.ID, "Annas PC")); contains(f, "admin_v1") {
		t.Fatalf("member: %v", f)
	}
}

type adminUserT struct {
	ID          string `json:"id"`
	Username    string
	DisplayName string `json:"display_name"`
	Role        string
	Disabled    bool
	Devices     int
	LastSeenAt  *string `json:"last_seen_at"`
}

func TestAdminUsersAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	adm := s.device(s.admin.ID, "Admin PC")
	anna := s.device(s.anna.ID, "Annas PC")
	tok := opt{token: adm.tok}
	s.confirm(adm.tok)

	// List: admin, anna, bob with device counts.
	list := decode[struct{ Users []adminUserT }](t, s.do("GET", "/api/v1/admin/users", nil, tok)).Users
	if len(list) != 3 || list[0].Role != "admin" || list[0].Devices != 1 || list[1].DisplayName != "Anna" || list[1].Devices != 1 || list[2].Devices != 0 {
		t.Fatalf("%+v", list)
	}

	// Create: the code is returned once; the user exists.
	rec := s.do("POST", "/api/v1/admin/users", map[string]any{"display_name": "Sam"}, tok)
	wantStatus(t, rec, 201, "")
	created := decode[struct {
		User        adminUserT `json:"user"`
		PairingCode struct {
			Code      string
			ExpiresAt string `json:"expires_at"`
		} `json:"pairing_code"`
	}](t, rec)
	if !strings.HasPrefix(created.PairingCode.Code, "UZ-") || created.User.DisplayName != "Sam" || created.User.Role != "user" {
		t.Fatalf("%+v", created)
	}
	if u, err := s.svc.GetUser(ctx, created.User.ID); err != nil || u.DisplayName != "Sam" {
		t.Fatalf("%v %+v", err, u)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/users", map[string]any{"display_name": "sam"}, tok), 409, "display_name_taken")
	wantStatus(t, s.do("POST", "/api/v1/admin/users", map[string]any{"display_name": "  "}, tok), 400, "")

	// Rename.
	rec = s.do("PATCH", "/api/v1/admin/users/"+s.anna.ID, map[string]any{"display_name": "Annie"}, tok)
	wantStatus(t, rec, 200, "")
	if u, _ := s.svc.GetUser(ctx, s.anna.ID); u.DisplayName != "Annie" || decode[adminUserT](t, rec).DisplayName != "Annie" {
		t.Fatalf("%+v", u)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/users/"+s.anna.ID, map[string]any{"display_name": "Bob"}, tok), 409, "display_name_taken")
	wantStatus(t, s.do("PATCH", "/api/v1/admin/users/nope", map[string]any{"display_name": "X"}, tok), 404, "not_found")

	// New pairing code for a regular user; not for an admin.
	rec = s.do("POST", "/api/v1/admin/users/"+s.bob.ID+"/code", nil, tok)
	wantStatus(t, rec, 201, "")
	if c := decode[struct{ Code string }](t, rec).Code; !strings.HasPrefix(c, "UZ-") {
		t.Fatal(c)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.admin.ID+"/code", nil, tok), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/users/nope/code", nil, tok), 404, "not_found")

	// Role: a password is needed to make an admin; you cannot demote yourself.
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.bob.ID+"/role", map[string]any{"role": "admin", "password": "short"}, tok), 400, "")
	rec = s.do("POST", "/api/v1/admin/users/"+s.bob.ID+"/role", map[string]any{"role": "admin", "password": "placeholder-password-123"}, tok)
	wantStatus(t, rec, 200, "")
	if u, _ := s.svc.GetUser(ctx, s.bob.ID); u.Role != hub.RoleAdmin || decode[adminUserT](t, rec).Role != "admin" {
		t.Fatalf("%+v", u)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.admin.ID+"/role", map[string]any{"role": "user"}, tok), 403, "forbidden")
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.bob.ID+"/role", map[string]any{"role": "user"}, tok), 200, "")
	if u, _ := s.svc.GetUser(ctx, s.bob.ID); u.Role != hub.RoleUser {
		t.Fatalf("%+v", u)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.bob.ID+"/role", map[string]any{"role": "owner"}, opt{token: tok.token, raw: true}), 400, "")

	// Disable and enable; not yourself.
	rec = s.do("POST", "/api/v1/admin/users/"+s.anna.ID+"/disable", nil, tok)
	wantStatus(t, rec, 200, "")
	if u, _ := s.svc.GetUser(ctx, s.anna.ID); !u.Disabled() || !decode[adminUserT](t, rec).Disabled {
		t.Fatalf("%+v", u)
	}
	wantStatus(t, s.do("GET", "/api/v1/plugins", nil, opt{token: anna.tok}), 401, "user_disabled")
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.admin.ID+"/disable", nil, tok), 403, "forbidden")
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.anna.ID+"/enable", nil, tok), 200, "")
	if u, _ := s.svc.GetUser(ctx, s.anna.ID); u.Disabled() {
		t.Fatalf("%+v", u)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/users/nope/disable", nil, tok), 404, "not_found")

	// Delete: not yourself, not an unknown id.
	wantStatus(t, s.do("DELETE", "/api/v1/admin/users/"+s.admin.ID, nil, tok), 403, "forbidden")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/users/"+s.bob.ID, nil, tok), 204, "")
	if _, err := s.svc.GetUser(ctx, s.bob.ID); err == nil {
		t.Fatal("bob still exists")
	}
	wantStatus(t, s.do("DELETE", "/api/v1/admin/users/"+s.bob.ID, nil, tok), 404, "not_found")
}

func TestAdminInvitesAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	tok := opt{token: s.device(s.admin.ID, "Admin PC").tok}

	// Default body: 1 hour; the code is returned once and is not listed.
	rec := s.do("POST", "/api/v1/admin/invites", nil, tok)
	wantStatus(t, rec, 201, "")
	type inviteT struct {
		ID              string
		Status          string
		AuthorizeDevice bool   `json:"authorize_device"`
		CreatedAt       string `json:"created_at"`
		ExpiresAt       string `json:"expires_at"`
	}
	c1 := decode[struct {
		Invite inviteT
		Code   struct {
			Code      string
			ExpiresAt string `json:"expires_at"`
		}
	}](t, rec)
	if c1.Invite.Status != "active" || !strings.HasPrefix(c1.Code.Code, "UZ-") || c1.Invite.ExpiresAt != c1.Code.ExpiresAt || c1.Invite.AuthorizeDevice {
		t.Fatalf("%+v", c1)
	}
	rec = s.do("POST", "/api/v1/admin/invites", map[string]any{"expiry": "7d", "authorize_device": true}, tok)
	wantStatus(t, rec, 201, "")
	c2 := decode[struct{ Invite inviteT }](t, rec)
	if !c2.Invite.AuthorizeDevice || c2.Invite.ExpiresAt == c1.Invite.ExpiresAt {
		t.Fatalf("%+v", c2)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/invites", map[string]any{"expiry": "2h"}, opt{token: tok.token, raw: true}), 400, "")
	list := decode[struct{ Invites []inviteT }](t, s.do("GET", "/api/v1/admin/invites", nil, tok)).Invites
	if len(list) != 2 || list[0].ID != c2.Invite.ID || list[1].Status != "active" {
		t.Fatalf("%+v", list)
	}
	if strings.Contains(s.do("GET", "/api/v1/admin/invites", nil, tok).Body.String(), c1.Code.Code) {
		t.Fatal("the code is listed")
	}
	if l := decode[struct{ Invites []inviteT }](t, s.do("GET", "/api/v1/admin/invites?limit=1", nil, tok)).Invites; len(l) != 1 {
		t.Fatalf("%+v", l)
	}

	// The code works until it is revoked.
	wantStatus(t, s.do("DELETE", "/api/v1/admin/invites/"+c1.Invite.ID, nil, tok), 204, "")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/invites/"+c1.Invite.ID, nil, tok), 404, "not_found")
	rec = s.do("POST", "/api/v1/invites/redeem", redeemBody(c1.Code.Code, "Late", uuid.NewString()), opt{})
	wantStatus(t, rec, 404, "invite_invalid")
	list = decode[struct{ Invites []inviteT }](t, s.do("GET", "/api/v1/admin/invites", nil, tok)).Invites
	if len(list) != 2 || list[1].Status != "revoked" {
		t.Fatalf("%+v", list)
	}
}

func TestAdminPairingAndDevicesAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	ctx := context.Background()
	tok := opt{token: s.device(s.admin.ID, "Admin PC").tok}

	// Two pending requests: allow one for anna, deny the other.
	mkReq := func(name string) hub.PairingCreated {
		s.ips++
		c, err := s.svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: uuid.NewString(), DeviceName: name, Platform: "linux", Arch: "x86_64",
			PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: fmt.Sprintf("10.9.0.%d", s.ips)})
		if err != nil {
			t.Fatal(err)
		}
		return c
	}
	ok, no := mkReq("Laptop"), mkReq("Tablet")
	l := decode[struct {
		Requests           []struct{ ID, Status, Platform string }
		MinProtocolVersion int `json:"min_protocol_version"`
	}](t, s.do("GET", "/api/v1/admin/pairing-requests", nil, tok))
	if len(l.Requests) != 2 || l.Requests[0].Status != "pending" || l.MinProtocolVersion < 1 {
		t.Fatalf("%+v", l)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/pairing-requests/"+ok.RequestID+"/allow", map[string]any{"user_id": "nope"}, tok), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/pairing-requests/"+ok.RequestID+"/allow", map[string]any{"user_id": s.anna.ID}, tok), 204, "")
	if r, err := s.svc.PollPairing(ctx, ok.RequestID, ok.PollToken); err != nil || r.Status != hub.PairingApproved || r.UserID != s.anna.ID {
		t.Fatalf("%v %+v", err, r)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/pairing-requests/"+no.RequestID+"/deny", nil, tok), 204, "")
	if r, err := s.svc.PollPairing(ctx, no.RequestID, no.PollToken); err == nil && r.Status == hub.PairingApproved {
		t.Fatalf("denied request approved: %+v", r)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/pairing-requests/"+no.RequestID+"/deny", nil, tok), 409, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/pairing-requests/nope/deny", nil, tok), 404, "")
	if l := decode[struct{ Requests []struct{ ID string } }](t, s.do("GET", "/api/v1/admin/pairing-requests", nil, tok)).Requests; len(l) != 0 {
		t.Fatalf("%+v", l)
	}
	den := decode[struct{ Requests []struct{ Status string } }](t, s.do("GET", "/api/v1/admin/pairing-requests?status=denied", nil, tok))
	if len(den.Requests) != 1 || den.Requests[0].Status != "denied" {
		t.Fatalf("%+v", den)
	}

	// Devices: list, rename, revoke, delete.
	d1 := s.device(s.anna.ID, "Annas PC")
	d2 := s.device(s.bob.ID, "Bobs PC")
	type devT struct {
		ID       string
		UserName string `json:"user_name"`
		Name     string
		Status   string
		Online   bool
		Platform string
	}
	devs := decode[struct{ Devices []devT }](t, s.do("GET", "/api/v1/admin/devices", nil, tok)).Devices
	byID := map[string]devT{}
	for _, d := range devs {
		byID[d.ID] = d
	}
	if len(devs) != 4 || byID[d1.id].UserName != "Anna" || byID[d1.id].Status != "trusted" || byID[d2.id].Platform != "linux" {
		t.Fatalf("%+v", devs)
	}
	rec := s.do("PATCH", "/api/v1/admin/devices/"+d1.id, map[string]any{"name": "Living room PC"}, tok)
	wantStatus(t, rec, 200, "")
	if d, _ := s.svc.GetDevice(ctx, d1.id); d.Name != "Living room PC" || decode[devT](t, rec).Name != "Living room PC" {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/devices/"+d1.id, map[string]any{"name": ""}, opt{token: tok.token, raw: true}), 400, "")
	wantStatus(t, s.do("PATCH", "/api/v1/admin/devices/nope", map[string]any{"name": "X"}, tok), 404, "")

	wantStatus(t, s.do("GET", "/api/v1/plugins", nil, opt{token: d1.tok}), 200, "")
	rec = s.do("POST", "/api/v1/admin/devices/"+d1.id+"/revoke", nil, tok)
	wantStatus(t, rec, 200, "")
	if d, _ := s.svc.GetDevice(ctx, d1.id); d.Status != hub.DeviceRevoked || decode[devT](t, rec).Status != "revoked" {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, s.do("GET", "/api/v1/plugins", nil, opt{token: d1.tok}), 401, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/devices/nope/revoke", nil, tok), 404, "")

	wantStatus(t, s.do("DELETE", "/api/v1/admin/devices/"+d2.id, nil, tok), 204, "")
	if _, err := s.svc.GetDevice(ctx, d2.id); err == nil {
		t.Fatal("device still exists")
	}
	wantStatus(t, s.do("DELETE", "/api/v1/admin/devices/"+d2.id, nil, tok), 404, "")
}

// The dangerous steps of the admin API answer 403 confirmation_required until the calling device confirmed.
func TestAdminDangerousStepsNeedConfirmation(t *testing.T) {
	s := newSessEnv(t, nil)
	adm := s.device(s.admin.ID, "Admin PC")
	tok := opt{token: adm.tok}
	need := func(method, path string, body any) {
		t.Helper()
		wantStatus(t, s.do(method, path, body, tok), 403, "confirmation_required")
	}
	need("DELETE", "/api/v1/admin/games/"+s.game.ID, nil)
	need("POST", "/api/v1/admin/users/"+s.anna.ID+"/disable", nil)
	need("DELETE", "/api/v1/admin/users/"+s.anna.ID, nil)
	need("POST", "/api/v1/admin/updates/install", map[string]any{})
	need("DELETE", "/api/v1/admin/users/"+s.anna.ID+"/games/"+s.game.ID+"/saves/main?confirm_revision=1", nil)
	need("POST", "/api/v1/admin/users/"+s.anna.ID+"/games/"+s.game.ID+"/saves/main/history/1/restore", map[string]any{"expected_revision": 1})
	// Unguarded steps keep working without a confirmation.
	wantStatus(t, s.do("GET", "/api/v1/admin/users", nil, tok), 200, "")
	s.confirm(adm.tok)
	wantStatus(t, s.do("POST", "/api/v1/admin/users/"+s.anna.ID+"/disable", nil, tok), 200, "")
}
