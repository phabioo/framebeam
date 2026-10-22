package hub_test

import (
	"errors"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

const memberPassword = "member-password-1"

type acctEnv struct {
	t     *testing.T
	svc   *hub.Service
	clk   *hubtest.Clock
	admin hub.User
	adevs string // device id of the admin
	n     int
}

func newAcctEnv(t *testing.T) *acctEnv {
	t.Helper()
	svc, clk := hubtest.New(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "admin-password-1")
	if err != nil {
		t.Fatal(err)
	}
	e := &acctEnv{t: t, svc: svc, clk: clk, admin: admin}
	e.adevs = e.signIn("admin", "admin-password-1", "").DeviceID
	return e
}

type signedIn struct {
	DeviceID string
	hub.SignInResult
}

func (e *acctEnv) in(user, pw, install string) hub.SignInInput {
	e.n++
	return hub.SignInInput{Username: user, Password: pw, DeviceID: uuid.NewString(), DeviceName: "PC", Platform: "linux", Arch: "x86_64",
		AppVersion: "0.18.0", ProtocolVersion: 1, InstallID: install, RemoteAddr: "192.0.2.1"}
}

// signIn signs a new device in and fails the test on an error.
func (e *acctEnv) signIn(user, pw, install string) signedIn {
	e.t.Helper()
	in := e.in(user, pw, install)
	r, err := e.svc.SignIn(ctx, in)
	if err != nil {
		e.t.Fatalf("sign-in %s: %v", user, err)
	}
	return signedIn{DeviceID: in.DeviceID, SignInResult: r}
}

// token exchanges the device credential for an access token.
func (e *acctEnv) token(s signedIn) string {
	e.t.Helper()
	tok, err := e.svc.IssueAccessToken(ctx, s.DeviceID, s.DeviceCredential)
	if err != nil {
		e.t.Fatal(err)
	}
	return tok.Token
}

// member creates a member through an onboarding invite that sets a password; it returns the user and the first device.
func (e *acctEnv) member(name, pw string) (hub.User, string) {
	e.t.Helper()
	_, code, err := e.svc.CreateInvite(ctx, e.admin.ID, time.Hour, true)
	if err != nil {
		e.t.Fatal(err)
	}
	dev := uuid.NewString()
	res, err := e.svc.RedeemInvite(ctx, hub.RedeemInput{Code: code, DisplayName: name, DeviceID: dev, DeviceName: "PC", Platform: "linux",
		Arch: "x86_64", PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: "198.51.100." + string(rune('1'+e.n%8)), Password: pw})
	e.n++
	if err != nil || !res.Approved {
		e.t.Fatalf("redeem: %v %+v", err, res)
	}
	u, err := e.svc.GetUser(ctx, res.UserID)
	if err != nil {
		e.t.Fatal(err)
	}
	return u, dev
}

func errCodeOf(err error) hub.Code {
	var he *hub.Error
	if errors.As(err, &he) {
		return he.Code
	}
	return ""
}

func TestSignInSuccessAndOneGenericFailure(t *testing.T) {
	e := newAcctEnv(t)
	anna, _ := e.member("Anna", memberPassword)
	if _, err := e.svc.CreateUser(ctx, "nopw", "No Password"); err != nil {
		t.Fatal(err)
	}
	s := e.signIn("anna", memberPassword, "")
	if s.UserID != anna.ID || s.HubID != e.svc.Info().HubID || !strings.HasPrefix(s.DeviceCredential, "fbd_") {
		t.Fatalf("%+v", s)
	}
	d, err := e.svc.GetDevice(ctx, s.DeviceID)
	if err != nil || d.SignInKind != hub.SignInPassword || d.Status != hub.DeviceTrusted || d.UserID != anna.ID {
		t.Fatalf("%+v %v", d, err)
	}
	if _, err := e.svc.Authenticate(ctx, e.token(s)); err != nil {
		t.Fatal(err)
	}

	// Wrong password, unknown user and a user without a password: the very same error.
	var msgs []string
	for _, c := range [][2]string{{"anna", "wrong-password-1"}, {"nobody", memberPassword}, {"nopw", memberPassword}, {"nopw", ""}} {
		in := e.in(c[0], c[1], "")
		if c[1] == "" {
			in.Password = "x"
		}
		_, err := e.svc.SignIn(ctx, in)
		if !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatalf("%v: %v", c, err)
		}
		msgs = append(msgs, err.Error())
		e.clk.Advance(10 * time.Second)
	}
	for _, m := range msgs {
		if m != msgs[0] {
			t.Fatalf("answers differ: %v", msgs)
		}
	}
	// Field checks.
	in := e.in("anna", memberPassword, strings.Repeat("x", 65))
	if _, err := e.svc.SignIn(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("install id: %v", err)
	}
	in = e.in("anna", memberPassword, "")
	in.DeviceID = "nope"
	if _, err := e.svc.SignIn(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("device id: %v", err)
	}
}

func TestSignInReplacesOldAccessTokensAndTrustsRevokedDevice(t *testing.T) {
	e := newAcctEnv(t)
	e.member("Anna", memberPassword)
	in := e.in("anna", memberPassword, "")
	r1, err := e.svc.SignIn(ctx, in)
	if err != nil {
		t.Fatal(err)
	}
	old, err := e.svc.IssueAccessToken(ctx, in.DeviceID, r1.DeviceCredential)
	if err != nil {
		t.Fatal(err)
	}
	r2, err := e.svc.SignIn(ctx, in) // same device id again
	if err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Authenticate(ctx, old.Token); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatalf("old access token must be gone: %v", err)
	}
	if _, err := e.svc.IssueAccessToken(ctx, in.DeviceID, r1.DeviceCredential); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("old credential must be gone: %v", err)
	}
	if _, err := e.svc.IssueAccessToken(ctx, in.DeviceID, r2.DeviceCredential); err != nil {
		t.Fatal(err)
	}
	// A signed-out device of the user is trusted again by signing in.
	if err := e.svc.RevokeDevice(ctx, in.DeviceID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, in); err != nil {
		t.Fatal(err)
	}
	if d, _ := e.svc.GetDevice(ctx, in.DeviceID); d.Status != hub.DeviceTrusted || d.RevokedAt != nil {
		t.Fatalf("%+v", d)
	}
}

func TestSignInUserLockoutAndUnknownNameLockout(t *testing.T) {
	e := newAcctEnv(t)
	e.member("Anna", memberPassword)
	for _, name := range []string{"anna", "ghost"} {
		e.clk.Advance(61 * time.Second) // the IP limit is not under test here
		for i := 1; i <= hub.SignInFailureLimit; i++ {
			_, err := e.svc.SignIn(ctx, e.in(name, "wrong-password-1", ""))
			if i < hub.SignInFailureLimit && !errors.Is(err, hub.ErrInvalidCredentials) {
				t.Fatalf("%s failure %d: %v", name, i, err)
			}
			if i == hub.SignInFailureLimit { // the failure that locks already answers rate_limited
				var he *hub.Error
				if !errors.As(err, &he) || he.Code != hub.CodeRateLimited || he.RetryAfter != int(hub.SignInLockDuration.Seconds()) {
					t.Fatalf("%s lock: %v", name, err)
				}
			}
			e.clk.Advance(7 * time.Second)
		}
		// Locked: even the right password is refused.
		if _, err := e.svc.SignIn(ctx, e.in(name, memberPassword, "")); !errors.Is(err, hub.ErrRateLimited) {
			t.Fatalf("%s locked: %v", name, err)
		}
		if name == "anna" {
			// Other names are not affected.
			if _, err := e.svc.SignIn(ctx, e.in("admin", "admin-password-1", "")); err != nil {
				t.Fatal(err)
			}
		}
	}
	e.clk.Advance(hub.SignInLockDuration)
	if _, err := e.svc.SignIn(ctx, e.in("anna", memberPassword, "")); err != nil {
		t.Fatalf("after the lock: %v", err)
	}
	// A success resets the count: 9 more failures do not lock.
	for i := 0; i < hub.SignInFailureLimit-1; i++ {
		if _, err := e.svc.SignIn(ctx, e.in("anna", "wrong-password-1", "")); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatal(err)
		}
		e.clk.Advance(7 * time.Second)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", memberPassword, "")); err != nil {
		t.Fatal(err)
	}
}

func TestSignInFailuresFadeAfterFifteenMinutes(t *testing.T) {
	e := newAcctEnv(t)
	e.member("Anna", memberPassword)
	for round := 0; round < 3; round++ { // 27 failures in total, never 10 within 15 minutes
		for i := 0; i < hub.SignInFailureLimit-1; i++ {
			if _, err := e.svc.SignIn(ctx, e.in("anna", "wrong-password-1", "")); !errors.Is(err, hub.ErrInvalidCredentials) {
				t.Fatalf("round %d: %v", round, err)
			}
			e.clk.Advance(7 * time.Second)
		}
		e.clk.Advance(hub.SignInLockDuration + time.Minute)
	}
}

func TestSignInIPRateLimit(t *testing.T) {
	e := newAcctEnv(t)
	e.clk.Advance(61 * time.Second) // setup signed in once
	for i := 0; i < hub.MaxSignInPerIPPerMinute; i++ {
		in := e.in(strings.Repeat("a", i+1), "wrong-password-1", "") // different names, so no per-name lock
		if _, err := e.svc.SignIn(ctx, in); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatalf("%d: %v", i, err)
		}
	}
	in := e.in("admin", "admin-password-1", "")
	_, err := e.svc.SignIn(ctx, in)
	var he *hub.Error
	if !errors.As(err, &he) || he.Code != hub.CodeRateLimited || he.RetryAfter < 1 || he.RetryAfter > 60 {
		t.Fatalf("%v", err)
	}
	// Another source is not affected; the limit ends after a minute.
	in.RemoteAddr = "192.0.2.77"
	if _, err := e.svc.SignIn(ctx, in); err != nil {
		t.Fatal(err)
	}
	e.clk.Advance(61 * time.Second)
	in.RemoteAddr = "192.0.2.1"
	if _, err := e.svc.SignIn(ctx, in); err != nil {
		t.Fatal(err)
	}
}

func TestSignInDisabledUserAndMustChangeStillSignsIn(t *testing.T) {
	e := newAcctEnv(t)
	anna, _ := e.member("Anna", memberPassword)
	// A wrong password of a disabled user is still the generic answer; the right one says user_disabled.
	if err := e.svc.DisableUser(ctx, anna.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", "wrong-password-1", "")); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", memberPassword, "")); !errors.Is(err, hub.ErrUserDisabled) {
		t.Fatal(err)
	}
}

func TestSeveralUsersOnOneInstallAndNoTakeover(t *testing.T) {
	e := newAcctEnv(t)
	anna, _ := e.member("Anna", memberPassword)
	bob, _ := e.member("Bob", "bobs-password-12")
	const install = "install-1"
	a := e.signIn("anna", memberPassword, install)
	b := e.signIn("bob", "bobs-password-12", install) // another device id, same install
	if a.DeviceID == b.DeviceID || a.UserID == b.UserID {
		t.Fatal("test setup")
	}
	da, _ := e.svc.GetDevice(ctx, a.DeviceID)
	db, _ := e.svc.GetDevice(ctx, b.DeviceID)
	if da.InstallID != install || db.InstallID != install || da.UserID != anna.ID || db.UserID != bob.ID {
		t.Fatalf("%+v %+v", da, db)
	}
	if _, err := e.svc.Authenticate(ctx, e.token(a)); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Authenticate(ctx, e.token(b)); err != nil {
		t.Fatal(err)
	}

	// Bob cannot take over Anna's device id, even with the right password.
	in := e.in("bob", "bobs-password-12", install)
	in.DeviceID = a.DeviceID
	if _, err := e.svc.SignIn(ctx, in); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("takeover: %v", err)
	}
	if d, _ := e.svc.GetDevice(ctx, a.DeviceID); d.UserID != anna.ID {
		t.Fatalf("owner changed: %+v", d)
	}
	if _, err := e.svc.IssueAccessToken(ctx, a.DeviceID, a.DeviceCredential); err != nil {
		t.Fatalf("Anna's credential must still work: %v", err)
	}
}

func TestAccountGetRenameAndPasswordRules(t *testing.T) {
	e := newAcctEnv(t)
	anna, adev := e.member("Anna", memberPassword)
	nopw, err := e.svc.CreateUser(ctx, "nopw", "No Password")
	if err != nil {
		t.Fatal(err)
	}
	_ = nopw

	acc, err := e.svc.GetAccount(ctx, anna.ID, adev)
	if err != nil || acc.Username != "anna" || acc.DisplayName != "Anna" || acc.Role != hub.RoleUser || !acc.HasPassword ||
		acc.PasswordRequired || acc.MustChangePassword || acc.ConfirmedUntil != nil || acc.ConfirmLockedUntil != nil {
		t.Fatalf("%+v %v", acc, err)
	}
	if _, err := e.svc.GetAccount(ctx, anna.ID, e.adevs); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("device of another user: %v", err)
	}
	adm, _ := e.svc.GetAccount(ctx, e.admin.ID, e.adevs)
	if !adm.PasswordRequired || !adm.HasPassword || adm.Role != hub.RoleAdmin {
		t.Fatalf("%+v", adm)
	}

	// Rename: same rules as everywhere.
	if err := e.svc.RenameUser(ctx, anna.ID, "  Annika "); err != nil {
		t.Fatal(err)
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, adev); a.DisplayName != "Annika" {
		t.Fatalf("%+v", a)
	}
	if err := e.svc.RenameUser(ctx, anna.ID, "admin"); !errors.Is(err, hub.ErrDisplayNameTaken) {
		t.Fatalf("name taken: %v", err)
	}

	// Set and change.
	if err := e.svc.SetPassword(ctx, anna.ID, adev, memberPassword, "short"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("too short: %v", err)
	}
	if err := e.svc.SetPassword(ctx, anna.ID, adev, memberPassword, strings.Repeat("x", hub.MaxPasswordBytes+1)); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("too long: %v", err)
	}
	if err := e.svc.SetPassword(ctx, anna.ID, adev, "", "brand-new-password"); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("current required: %v", err)
	}
	if err := e.svc.SetPassword(ctx, anna.ID, adev, memberPassword, "brand-new-password"); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", memberPassword, "")); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("old password: %v", err)
	}
	other := e.signIn("anna", "brand-new-password", "") // sign-in with the new one; the first device stays signed in
	if !e.svc.DeviceAuthorized(ctx, adev) || !e.svc.DeviceAuthorized(ctx, other.DeviceID) {
		t.Fatal("devices must stay signed in")
	}
	// A user without a password sets one without a current password.
	nd := e.deviceFor(nopw.ID)
	if err := e.svc.SetPassword(ctx, nopw.ID, nd, "", "first-password-1"); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, e.in("nopw", "first-password-1", "")); err != nil {
		t.Fatal(err)
	}
}

// deviceFor gives the user a trusted device the way an approved pairing does.
func (e *acctEnv) deviceFor(userID string) string {
	e.t.Helper()
	id := uuid.NewString()
	e.n++
	c, err := e.svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: id, DeviceName: "PC", Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: "203.0.113." + string(rune('1'+e.n%8))})
	if err != nil {
		e.t.Fatal(err)
	}
	if err := e.svc.ApprovePairing(ctx, c.RequestID, userID); err != nil {
		e.t.Fatal(err)
	}
	if r, err := e.svc.PollPairing(ctx, c.RequestID, c.PollToken); err != nil || r.Status != hub.PairingApproved {
		e.t.Fatalf("%v %+v", err, r)
	}
	if d, _ := e.svc.GetDevice(ctx, id); d.SignInKind != hub.SignInApproval {
		e.t.Fatalf("approval kind: %+v", d)
	}
	return id
}

func TestRemovePasswordRules(t *testing.T) {
	e := newAcctEnv(t)
	anna, adev := e.member("Anna", memberPassword)

	// Admins can never remove theirs.
	if err := e.svc.RemovePassword(ctx, e.admin.ID, e.adevs, "admin-password-1"); errCodeOf(err) != hub.CodePasswordRequired {
		t.Fatalf("admin: %v", err)
	}
	// Wrong current password.
	if err := e.svc.RemovePassword(ctx, anna.ID, adev, "wrong-password-1"); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("wrong: %v", err)
	}
	// The policy forbids it for members.
	if err := e.svc.SetRequirePasswords(ctx, true); err != nil {
		t.Fatal(err)
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, adev); !a.PasswordRequired {
		t.Fatalf("%+v", a)
	}
	if err := e.svc.RemovePassword(ctx, anna.ID, adev, memberPassword); errCodeOf(err) != hub.CodePasswordRequired {
		t.Fatalf("policy: %v", err)
	}
	if err := e.svc.SetRequirePasswords(ctx, false); err != nil {
		t.Fatal(err)
	}
	// Confirmed devices are unconfirmed when the password goes.
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 0); err != nil {
		t.Fatal(err)
	}
	if err := e.svc.RemovePassword(ctx, anna.ID, adev, memberPassword); err != nil {
		t.Fatal(err)
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, adev); a.HasPassword || a.ConfirmedUntil != nil {
		t.Fatalf("%+v", a)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", memberPassword, "")); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("sign-in after removal: %v", err)
	}
	// Idempotent, and confirming without a password has its own error.
	if err := e.svc.RemovePassword(ctx, anna.ID, adev, ""); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, "whatever-password", 0); errCodeOf(err) != hub.CodePasswordNotSet {
		t.Fatalf("no password: %v", err)
	}
}

func TestDemoteKeepsThePassword(t *testing.T) {
	e := newAcctEnv(t)
	anna, _ := e.member("Anna", memberPassword)
	if err := e.svc.SetUserRole(ctx, e.admin.ID, anna.ID, hub.RoleAdmin, "a-long-enough-pass"); err != nil {
		t.Fatal(err)
	}
	if err := e.svc.SetUserRole(ctx, e.admin.ID, anna.ID, hub.RoleUser, ""); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", "a-long-enough-pass", "")); err != nil {
		t.Fatalf("the demoted admin keeps the password: %v", err)
	}
}

func TestConfirmFlowLockoutExpiryAndEarlyEnd(t *testing.T) {
	e := newAcctEnv(t)
	anna, adev := e.member("Anna", memberPassword)
	if err := e.svc.RequireConfirmed(ctx, anna.ID, adev); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("not confirmed: %v", err)
	}
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 60); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("duration: %v", err)
	}
	until, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 0)
	if err != nil || !until.Equal(e.clk.Now().Add(5*time.Minute).Truncate(time.Second)) {
		t.Fatalf("default 300 s: %v %v", until, err)
	}
	if err := e.svc.RequireConfirmed(ctx, anna.ID, adev); err != nil {
		t.Fatal(err)
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, adev); a.ConfirmedUntil == nil || !a.ConfirmedUntil.Equal(until) {
		t.Fatalf("%+v", a)
	}
	// Per device: Anna's other device is not confirmed; another user's device id does not count for her.
	other := e.signIn("anna", memberPassword, "")
	if err := e.svc.RequireConfirmed(ctx, anna.ID, other.DeviceID); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("other device: %v", err)
	}
	if err := e.svc.RequireConfirmed(ctx, e.admin.ID, adev); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("foreign device: %v", err)
	}
	// Expiry.
	e.clk.Advance(5*time.Minute + time.Second)
	if err := e.svc.RequireConfirmed(ctx, anna.ID, adev); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("expired: %v", err)
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, adev); a.ConfirmedUntil != nil {
		t.Fatalf("%+v", a)
	}
	// Early end.
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 900); err != nil {
		t.Fatal(err)
	}
	if err := e.svc.EndConfirmation(ctx, anna.ID, adev); err != nil {
		t.Fatal(err)
	}
	if err := e.svc.RequireConfirmed(ctx, anna.ID, adev); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("ended: %v", err)
	}
	// Two wrong passwords, a success resets the count, then three wrong ones lock.
	for i := 0; i < 2; i++ {
		if _, err := e.svc.Confirm(ctx, anna.ID, adev, "wrong-password-1", 0); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatal(err)
		}
	}
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 120); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 2; i++ {
		if _, err := e.svc.Confirm(ctx, anna.ID, adev, "wrong-password-1", 0); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatalf("after reset: %v", err)
		}
	}
	_, err = e.svc.Confirm(ctx, anna.ID, adev, "wrong-password-1", 0)
	var he *hub.Error
	if !errors.As(err, &he) || he.Code != hub.CodeRateLimited || he.RetryAfter != 300 {
		t.Fatalf("third wrong password locks: %v", err)
	}
	if err := e.svc.RequireConfirmed(ctx, anna.ID, adev); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("a lock also ends the confirmation: %v", err)
	}
	a, _ := e.svc.GetAccount(ctx, anna.ID, adev)
	if a.ConfirmLockedUntil == nil || !a.ConfirmLockedUntil.Equal(e.clk.Now().Add(5*time.Minute).Truncate(time.Second)) {
		t.Fatalf("%+v", a)
	}
	// Locked: the right password is refused too, and only on this device.
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 0); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("locked: %v", err)
	}
	if _, err := e.svc.Confirm(ctx, anna.ID, other.DeviceID, memberPassword, 0); err != nil {
		t.Fatalf("other device: %v", err)
	}
	// A wrong current password at the password change counts the same way (shared lock).
	e.clk.Advance(5*time.Minute + time.Second)
	for i := 0; i < 2; i++ {
		if err := e.svc.SetPassword(ctx, anna.ID, adev, "wrong-password-1", "brand-new-password"); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatal(err)
		}
	}
	if err := e.svc.SetPassword(ctx, anna.ID, adev, "wrong-password-1", "brand-new-password"); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("change counts: %v", err)
	}
	e.clk.Advance(5*time.Minute + time.Second)
	if _, err := e.svc.Confirm(ctx, anna.ID, adev, memberPassword, 0); err != nil {
		t.Fatalf("after the lock: %v", err)
	}
}

func TestSignedInAgainEndsConfirmation(t *testing.T) {
	e := newAcctEnv(t)
	anna, _ := e.member("Anna", memberPassword)
	in := e.in("anna", memberPassword, "")
	if _, err := e.svc.SignIn(ctx, in); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Confirm(ctx, anna.ID, in.DeviceID, memberPassword, 0); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SignIn(ctx, in); err != nil {
		t.Fatal(err)
	}
	if err := e.svc.RequireConfirmed(ctx, anna.ID, in.DeviceID); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("%v", err)
	}
}

func TestOwnDevicesListAndSignOut(t *testing.T) {
	e := newAcctEnv(t)
	anna, d1 := e.member("Anna", memberPassword)
	bob, bdev := e.member("Bob", "bobs-password-12")
	s2 := e.signIn("anna", memberPassword, "inst")
	s3 := e.signIn("anna", memberPassword, "inst")
	tok1, tok2 := e.token(signedIn{DeviceID: s2.DeviceID, SignInResult: s2.SignInResult}), e.token(s3)

	list, err := e.svc.ListOwnDevices(ctx, anna.ID, d1)
	if err != nil || len(list) != 3 {
		t.Fatalf("%v %+v", err, list)
	}
	kinds := map[string]string{}
	cur := 0
	for _, d := range list {
		kinds[d.ID] = d.SignInKind
		if d.UserID != anna.ID {
			t.Fatalf("foreign device: %+v", d)
		}
		if d.Current {
			cur++
			if d.ID != d1 {
				t.Fatalf("current: %+v", d)
			}
		}
	}
	if cur != 1 || kinds[d1] != hub.SignInInvite || kinds[s2.DeviceID] != hub.SignInPassword {
		t.Fatalf("%v", kinds)
	}

	// Another user's device is neither removable nor visible.
	if err := e.svc.SignOutDevice(ctx, anna.ID, bdev); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("foreign device: %v", err)
	}
	if err := e.svc.SignOutDevice(ctx, anna.ID, "no-such-device"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	if !e.svc.DeviceAuthorized(ctx, bdev) {
		t.Fatal("Bob's device was touched")
	}

	if err := e.svc.SignOutDevice(ctx, anna.ID, s2.DeviceID); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.Authenticate(ctx, tok1); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatalf("token of a signed-out device: %v", err)
	}
	if _, err := e.svc.IssueAccessToken(ctx, s2.DeviceID, s2.DeviceCredential); !errors.Is(err, hub.ErrDeviceRevoked) {
		t.Fatalf("credential: %v", err)
	}

	// All but the current one.
	n, err := e.svc.SignOutAllDevices(ctx, anna.ID, d1, false)
	if err != nil || n != 1 { // s2 is already signed out
		t.Fatalf("%d %v", n, err)
	}
	if _, err := e.svc.Authenticate(ctx, tok2); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatalf("%v", err)
	}
	if !e.svc.DeviceAuthorized(ctx, d1) || !e.svc.DeviceAuthorized(ctx, bdev) {
		t.Fatal("current device or Bob's device signed out")
	}
	// Including the current one.
	s4 := e.signIn("anna", memberPassword, "")
	n, err = e.svc.SignOutAllDevices(ctx, anna.ID, d1, true)
	if err != nil || n != 2 {
		t.Fatalf("%d %v", n, err)
	}
	if e.svc.DeviceAuthorized(ctx, d1) || e.svc.DeviceAuthorized(ctx, s4.DeviceID) || !e.svc.DeviceAuthorized(ctx, bdev) {
		t.Fatal("state after sign out everywhere")
	}
	if bl, _ := e.svc.ListOwnDevices(ctx, bob.ID, bdev); len(bl) != 1 {
		t.Fatalf("%+v", bl)
	}
}

func TestAdminPasswordReset(t *testing.T) {
	e := newAcctEnv(t)
	anna, adev := e.member("Anna", memberPassword)
	// Locked out by failures: the reset ends the lock.
	for i := 0; i < hub.SignInFailureLimit; i++ {
		e.svc.SignIn(ctx, e.in("anna", "wrong-password-1", ""))
		e.clk.Advance(7 * time.Second)
	}

	// Needs a confirmation, and never for the caller themself.
	if _, err := e.svc.ResetPassword(ctx, e.admin.ID, e.adevs, anna.ID); !errors.Is(err, hub.ErrConfirmationRequired) {
		t.Fatalf("unconfirmed: %v", err)
	}
	if _, err := e.svc.Confirm(ctx, e.admin.ID, e.adevs, "admin-password-1", 0); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.ResetPassword(ctx, e.admin.ID, e.adevs, e.admin.ID); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("self: %v", err)
	}
	if _, err := e.svc.ResetPassword(ctx, e.admin.ID, e.adevs, "u_nobody"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	temp, err := e.svc.ResetPassword(ctx, e.admin.ID, e.adevs, anna.ID)
	if err != nil {
		t.Fatal(err)
	}
	parts := strings.Split(temp, "-")
	if len(parts) != 4 || len(temp) < hub.MinPasswordLen {
		t.Fatalf("shape: %q", temp)
	}
	for _, p := range parts {
		if len(p) != 4 || strings.ContainsAny(p, "01OIL") || p != strings.ToUpper(p) {
			t.Fatalf("shape: %q", temp)
		}
	}
	if t2, _ := e.svc.ResetPassword(ctx, e.admin.ID, e.adevs, anna.ID); t2 == temp {
		t.Fatal("temporary passwords must differ")
	}
	temp, _ = e.svc.ResetPassword(ctx, e.admin.ID, e.adevs, anna.ID)

	// Her device stays signed in and the account reports the flag; the old password is gone.
	if !e.svc.DeviceAuthorized(ctx, adev) {
		t.Fatal("device signed out by the reset")
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, adev); !a.MustChangePassword || !a.HasPassword {
		t.Fatalf("%+v", a)
	}
	if _, err := e.svc.SignIn(ctx, e.in("anna", memberPassword, "")); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("old password: %v", err)
	}
	s := e.signIn("anna", temp, "") // the temporary password signs in
	if a, _ := e.svc.GetAccount(ctx, anna.ID, s.DeviceID); !a.MustChangePassword {
		t.Fatalf("%+v", a)
	}
	// A change clears the flag.
	if err := e.svc.SetPassword(ctx, anna.ID, s.DeviceID, temp, "my-own-password-1"); err != nil {
		t.Fatal(err)
	}
	if a, _ := e.svc.GetAccount(ctx, anna.ID, s.DeviceID); a.MustChangePassword {
		t.Fatalf("%+v", a)
	}
}

func TestRedeemInviteWithPassword(t *testing.T) {
	e := newAcctEnv(t)
	_, code, _ := e.svc.CreateInvite(ctx, e.admin.ID, time.Hour, true)
	in := hub.RedeemInput{Code: code, DisplayName: "Cleo", DeviceID: uuid.NewString(), DeviceName: "PC", Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: "198.51.100.9", InstallID: "inst-9", Password: "short"}
	if _, err := e.svc.RedeemInvite(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("short password: %v", err)
	}
	in.Password = memberPassword // the invite was not consumed by the refusal
	res, err := e.svc.RedeemInvite(ctx, in)
	if err != nil || !res.Approved {
		t.Fatalf("%v %+v", err, res)
	}
	d, _ := e.svc.GetDevice(ctx, in.DeviceID)
	if d.SignInKind != hub.SignInInvite || d.InstallID != "inst-9" {
		t.Fatalf("%+v", d)
	}
	if _, err := e.svc.SignIn(ctx, e.in("cleo", memberPassword, "")); err != nil {
		t.Fatalf("the new user has the password: %v", err)
	}

	// Without a password: none is set. Without authorization the pending pairing carries the kind to the device.
	_, code, _ = e.svc.CreateInvite(ctx, e.admin.ID, time.Hour, false)
	in2 := hub.RedeemInput{Code: code, DisplayName: "Dan", DeviceID: uuid.NewString(), DeviceName: "PC", Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: "198.51.100.10"}
	r2, err := e.svc.RedeemInvite(ctx, in2)
	if err != nil || r2.Approved {
		t.Fatalf("%v %+v", err, r2)
	}
	if _, err := e.svc.SignIn(ctx, e.in("dan", "anything-12345", "")); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("no password set: %v", err)
	}
	if err := e.svc.ApprovePairing(ctx, r2.RequestID, r2.UserID); err != nil {
		t.Fatal(err)
	}
	if p, err := e.svc.PollPairing(ctx, r2.RequestID, r2.PollToken); err != nil || p.Status != hub.PairingApproved {
		t.Fatalf("%v %+v", err, p)
	}
	if d, _ := e.svc.GetDevice(ctx, in2.DeviceID); d.SignInKind != hub.SignInInvite {
		t.Fatalf("%+v", d)
	}
}

func TestLocalSetupMarksTheDeviceLocal(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	dev := uuid.NewString()
	_, _, err := svc.LocalSetup(ctx, hub.LocalInput{Username: "owner", Password: "owner-password-1", Device: hub.PairingInput{DeviceID: dev,
		DeviceName: "PC", Platform: "linux", Arch: "x86_64", PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: "127.0.0.1"}})
	if err != nil {
		t.Fatal(err)
	}
	if d, _ := svc.GetDevice(ctx, dev); d.SignInKind != hub.SignInLocal {
		t.Fatalf("%+v", d)
	}
}

func TestRequirePasswordsSetting(t *testing.T) {
	e := newAcctEnv(t)
	if on, _ := e.svc.RequirePasswords(ctx); on {
		t.Fatal("default must be off")
	}
	if err := e.svc.SetRequirePasswords(ctx, true); err != nil {
		t.Fatal(err)
	}
	if on, _ := e.svc.RequirePasswords(ctx); !on {
		t.Fatal("not stored")
	}
}
