package hub_test

import (
	"errors"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

func TestRenameUserKeepsNamesUnique(t *testing.T) {
	svc, _, admin := newAdmin(t)
	lena, err := svc.CreateUser(ctx, "lena", "Lena")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := svc.CreateUser(ctx, "jonas", "Jonas"); err != nil {
		t.Fatal(err)
	}
	if err := svc.RenameUser(ctx, lena.ID, "  Lena K  "); err != nil {
		t.Fatal(err)
	}
	if u, _ := svc.GetUser(ctx, lena.ID); u.DisplayName != "Lena K" || u.Username != "lena" {
		t.Fatalf("%+v", u)
	}
	if err := svc.RenameUser(ctx, lena.ID, "jonas"); !errors.Is(err, hub.ErrDisplayNameTaken) {
		t.Fatalf("duplicate: %v", err)
	}
	if err := svc.RenameUser(ctx, lena.ID, "   "); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("empty: %v", err)
	}
	if err := svc.RenameUser(ctx, "nope", "Sam"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	if err := svc.RenameUser(ctx, admin.ID, "Max"); err != nil {
		t.Fatal(err)
	}
}

func TestSetUserRoleNeedsPasswordAndKeepsAnAdmin(t *testing.T) {
	svc, _, admin := newAdmin(t)
	lena, _ := svc.CreateUser(ctx, "lena", "Lena")
	if err := svc.SetUserRole(ctx, admin.ID, lena.ID, hub.RoleAdmin, "short"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("short password: %v", err)
	}
	if err := svc.SetUserRole(ctx, admin.ID, lena.ID, hub.RoleAdmin, "a-long-enough-pass"); err != nil {
		t.Fatal(err)
	}
	if u, err := svc.VerifyPassword(ctx, "lena", "a-long-enough-pass"); err != nil || u.Role != hub.RoleAdmin {
		t.Fatalf("%+v %v", u, err)
	}
	if err := svc.SetUserRole(ctx, lena.ID, lena.ID, hub.RoleUser, ""); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("self demotion: %v", err)
	}
	if err := svc.SetUserRole(ctx, admin.ID, lena.ID, hub.RoleUser, ""); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.VerifyPassword(ctx, "lena", "a-long-enough-pass"); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("password must be gone: %v", err)
	}
	// The last admin cannot be demoted by anyone else either.
	if err := svc.SetUserRole(ctx, lena.ID, admin.ID, hub.RoleUser, ""); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("last admin: %v", err)
	}
	if err := svc.SetUserRole(ctx, admin.ID, "nope", hub.RoleAdmin, "a-long-enough-pass"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
}

func TestCreateUserWithCodePairsThatUser(t *testing.T) {
	svc, _, admin := newAdmin(t)
	u, inv, code, err := svc.CreateUserWithCode(ctx, admin.ID, "Sam", time.Hour)
	if err != nil || code == "" || !inv.AuthorizeDevice || u.DisplayName != "Sam" || u.Role != hub.RoleUser {
		t.Fatalf("%+v %+v %q %v", u, inv, code, err)
	}
	if _, _, _, err := svc.CreateUserWithCode(ctx, admin.ID, "sam", time.Hour); !errors.Is(err, hub.ErrDisplayNameTaken) {
		t.Fatalf("duplicate: %v", err)
	}
	// The name the app sends is ignored: the device belongs to the created user.
	res, err := svc.RedeemInvite(ctx, redeemIn(code, "Whoever"))
	if err != nil || !res.Approved || res.UserID != u.ID {
		t.Fatalf("%+v %v", res, err)
	}
	if rows, _ := svc.ListUserRows(ctx); len(rows) != 2 {
		t.Fatalf("a second user was created: %+v", rows)
	}
	if _, err := svc.RedeemInvite(ctx, redeemIn(code, "Whoever")); !errors.Is(err, hub.ErrInviteInvalid) {
		t.Fatalf("code reuse: %v", err)
	}
	// A second code for an existing user; not for admins, disabled users or odd lifetimes.
	if _, c2, err := svc.CreateCodeForUser(ctx, admin.ID, u.ID, 24*time.Hour); err != nil || c2 == "" {
		t.Fatalf("%v", err)
	}
	if _, _, err := svc.CreateCodeForUser(ctx, admin.ID, admin.ID, time.Hour); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("admin: %v", err)
	}
	if _, _, err := svc.CreateCodeForUser(ctx, admin.ID, u.ID, 3*time.Hour); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("lifetime: %v", err)
	}
	// Disabled after the code was created: the code no longer works.
	u2, _, code2, _ := svc.CreateUserWithCode(ctx, admin.ID, "Jonas", time.Hour)
	if err := svc.DisableUser(ctx, u2.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.RedeemInvite(ctx, redeemIn(code2, "Jonas")); !errors.Is(err, hub.ErrInviteInvalid) {
		t.Fatalf("disabled: %v", err)
	}
}

func TestRenameDevice(t *testing.T) {
	svc, _, admin := newAdmin(t)
	_, code, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	in := redeemIn(code, "Lena")
	if _, err := svc.RedeemInvite(ctx, in); err != nil {
		t.Fatal(err)
	}
	if err := svc.RenameDevice(ctx, in.DeviceID, " Living room PC\n"); err != nil {
		t.Fatal(err)
	}
	if d, _ := svc.GetDevice(ctx, in.DeviceID); d.Name != "Living room PC" {
		t.Fatalf("%q", d.Name)
	}
	if err := svc.RenameDevice(ctx, in.DeviceID, " \n"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("empty: %v", err)
	}
	if err := svc.RenameDevice(ctx, "00000000-0000-0000-0000-000000000000", "X"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	if svc.DeviceOnline(in.DeviceID) {
		t.Fatal("a device without a connection is not online")
	}
}

func TestRunAdminStartFinishAndInvites(t *testing.T) {
	svc, _, admin, g := runFixture(t)
	sam, _ := svc.CreateUser(ctx, "sam", "Sam")
	lena, _ := svc.CreateUser(ctx, "lena", "Lena")
	run := newRun(t, svc, admin, g)
	if _, err := svc.InviteRun(ctx, admin.ID, run.ID, []string{sam.ID, lena.ID}); err != nil {
		t.Fatal(err)
	}
	// Resend and remove need an open invitation and write a server event each.
	if err := svc.ResendRunInvite(ctx, admin.ID, run.ID, sam.ID); err != nil {
		t.Fatal(err)
	}
	if err := svc.RemoveRunInvite(ctx, admin.ID, run.ID, lena.ID); err != nil {
		t.Fatal(err)
	}
	if err := svc.RemoveRunInvite(ctx, admin.ID, run.ID, lena.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("already removed: %v", err)
	}
	if err := svc.ResendRunInvite(ctx, admin.ID, run.ID, admin.ID); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("creator is joined: %v", err)
	}
	evs, _, err := svc.ListRunEventsAdmin(ctx, run.ID, 0, 50)
	if err != nil {
		t.Fatal(err)
	}
	types := map[string]bool{}
	for _, e := range evs {
		types[e.Type] = true
	}
	if !types["run.invite_resent"] || !types["run.invite_removed"] {
		t.Fatalf("events %v", types)
	}
	// Start without the invited user: Sam's invitation goes, the run is active.
	got, err := svc.StartRunAdmin(ctx, admin.ID, run.ID, true)
	if err != nil || got.Status != hub.RunActive {
		t.Fatalf("%+v %v", got, err)
	}
	if _, ok := got.Participant(sam.ID); ok {
		t.Fatal("Sam is still a participant")
	}
	if _, err := svc.StartRunAdmin(ctx, admin.ID, run.ID, false); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("already started: %v", err)
	}
	if _, err := svc.SetRunStatusAdmin(ctx, admin.ID, run.ID, hub.RunFinished); err != nil {
		t.Fatal(err)
	}
	if err := svc.ResendRunInvite(ctx, admin.ID, run.ID, sam.ID); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("finished run: %v", err)
	}
	// Start now keeps invited participants.
	run2 := newRun(t, svc, admin, g)
	svc.InviteRun(ctx, admin.ID, run2.ID, []string{sam.ID})
	got, err = svc.StartRunAdmin(ctx, admin.ID, run2.ID, false)
	if err != nil || got.Status != hub.RunActive {
		t.Fatalf("%+v %v", got, err)
	}
	if p, ok := got.Participant(sam.ID); !ok || p.Status != hub.ParticipantInvited {
		t.Fatalf("%+v", got.Participants)
	}
}
