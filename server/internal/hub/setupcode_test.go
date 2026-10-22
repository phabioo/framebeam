package hub_test

import (
	"errors"
	"regexp"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

func setupIn(code, user, pw, addr string) hub.SetupCodeInput {
	return hub.SetupCodeInput{Code: code, Username: user, Password: pw, Device: hub.PairingInput{DeviceID: uuid.NewString(), DeviceName: "Wohnzimmer-PC",
		Platform: "windows", Arch: "x86_64", PlayerVersion: "0.18.0", ProtocolVersion: 1, RemoteAddr: addr}}
}

const setupPassword = "a-long-admin-password"

func TestSetupCodeFormatAndReplace(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	re := regexp.MustCompile(`^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{2}$`)
	a, err := svc.NewSetupCode(ctx)
	if err != nil || !re.MatchString(a.Code) {
		t.Fatalf("%+v %v", a, err)
	}
	b, err := svc.NewSetupCode(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if cur, _ := svc.CurrentSetupCode(ctx); cur == nil || cur.Code != b.Code {
		t.Fatalf("current: %+v", cur)
	}
	if a.Code != b.Code { // the earlier code no longer works (a collision of two random codes is 1 in a billion)
		_, _, err := svc.SetupWithCode(ctx, setupIn(a.Code, "admin", setupPassword, "192.0.2.1"))
		if errCodeOf(err) != hub.CodeInvalidSetupCode {
			t.Fatalf("replaced code: %v", err)
		}
	}
	if got := b.Expires.Sub(svc.Now()); got < 59*time.Minute || got > 60*time.Minute {
		t.Fatalf("validity %v", got)
	}
	ensured, err := svc.EnsureSetupCode(ctx)
	if err != nil || ensured.Code != b.Code {
		t.Fatalf("ensure must keep the valid code: %+v %v", ensured, err)
	}
}

func TestSetupCodeRefusedWhenSetUp(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	if _, err := svc.CreateAdmin(ctx, "admin", setupPassword); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.NewSetupCode(ctx); !errors.Is(err, hub.ErrAlreadySetUp) {
		t.Fatalf("new code: %v", err)
	}
	if c, err := svc.CurrentSetupCode(ctx); c != nil || err != nil {
		t.Fatalf("current: %+v %v", c, err)
	}
	if req, _ := svc.SetupRequired(ctx); req {
		t.Fatal("setup must not be required")
	}
}

func TestSetupWithCodeSuccess(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	if req, _ := svc.SetupRequired(ctx); !req {
		t.Fatal("setup must be required")
	}
	code, _ := svc.NewSetupCode(ctx)
	typed := " " + strings.ToLower(strings.ReplaceAll(code.Code, "-", " ")) + " "
	in := setupIn(typed, "max", setupPassword, "192.0.2.7")
	res, u, err := svc.SetupWithCode(ctx, in)
	if err != nil {
		t.Fatal(err)
	}
	if u.Role != hub.RoleAdmin || u.Username != "max" || res.UserID != u.ID || res.DeviceCredential == "" || res.Status != hub.PairingApproved {
		t.Fatalf("%+v %+v", res, u)
	}
	d, err := svc.GetDevice(ctx, in.Device.DeviceID)
	if err != nil || d.UserID != u.ID || d.Status != hub.DeviceTrusted || d.SignInKind != hub.SignInPassword {
		t.Fatalf("%+v %v", d, err)
	}
	if _, err := svc.Authenticate(ctx, mustToken(t, svc, in.Device.DeviceID, res.DeviceCredential)); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.VerifyPassword(ctx, "max", setupPassword); err != nil {
		t.Fatalf("password: %v", err)
	}
	// Single use: the code is gone and the server is set up.
	_, _, err = svc.SetupWithCode(ctx, setupIn(code.Code, "other", setupPassword, "192.0.2.8"))
	if !errors.Is(err, hub.ErrAlreadySetUp) {
		t.Fatalf("second use: %v", err)
	}
	if c, _ := svc.CurrentSetupCode(ctx); c != nil {
		t.Fatalf("code must be gone: %+v", c)
	}
}

func mustToken(t *testing.T, svc *hub.Service, deviceID, cred string) string {
	t.Helper()
	tok, err := svc.IssueAccessToken(ctx, deviceID, cred)
	if err != nil {
		t.Fatal(err)
	}
	return tok.Token
}

func TestSetupWithCodeFieldErrorKeepsCode(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	code, _ := svc.NewSetupCode(ctx)
	for _, c := range []struct{ user, pw string }{{"max", "short"}, {"bad name!", setupPassword}, {"", setupPassword}} {
		_, _, err := svc.SetupWithCode(ctx, setupIn(code.Code, c.user, c.pw, "192.0.2.9"))
		if !errors.Is(err, hub.ErrBadRequest) {
			t.Fatalf("%v: %v", c, err)
		}
	}
	in := setupIn(code.Code, "max", setupPassword, "192.0.2.9")
	in.Device.DeviceID = "not-a-uuid"
	if _, _, err := svc.SetupWithCode(ctx, in); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("device: %v", err)
	}
	if _, _, err := svc.SetupWithCode(ctx, setupIn(code.Code, "max", setupPassword, "192.0.2.9")); err != nil {
		t.Fatalf("the code must still work after field errors: %v", err)
	}
}

func TestSetupWithCodeExpires(t *testing.T) {
	svc, clk := hubtest.New(t, nil)
	code, _ := svc.NewSetupCode(ctx)
	clk.Advance(59 * time.Minute)
	if c, _ := svc.CurrentSetupCode(ctx); c == nil {
		t.Fatal("still valid after 59 minutes")
	}
	clk.Advance(2 * time.Minute)
	if c, _ := svc.CurrentSetupCode(ctx); c != nil {
		t.Fatal("must be expired after 61 minutes")
	}
	// A wrong code on an expired code is just wrong; the right one says expired.
	if _, _, err := svc.SetupWithCode(ctx, setupIn("ZZZZ-ZZ", "max", setupPassword, "192.0.2.10")); errCodeOf(err) != hub.CodeInvalidSetupCode {
		t.Fatalf("wrong: %v", err)
	}
	_, _, err := svc.SetupWithCode(ctx, setupIn(code.Code, "max", setupPassword, "192.0.2.10"))
	if errCodeOf(err) != hub.CodeSetupCodeExpired {
		t.Fatalf("expired: %v", err)
	}
	if req, _ := svc.SetupRequired(ctx); !req {
		t.Fatal("an expired code must not create the admin")
	}
	// Ensure makes a new one.
	fresh, err := svc.EnsureSetupCode(ctx)
	if err != nil || fresh.Expires.Before(svc.Now()) {
		t.Fatalf("%+v %v", fresh, err)
	}
	if _, _, err := svc.SetupWithCode(ctx, setupIn(fresh.Code, "max", setupPassword, "192.0.2.10")); err != nil {
		t.Fatal(err)
	}
}

func TestSetupCodeAttemptLimit(t *testing.T) {
	svc, clk := hubtest.New(t, nil)
	code, _ := svc.NewSetupCode(ctx)
	wrong := "AAAA-AA"
	if code.Code == wrong {
		wrong = "BBBB-BB"
	}
	for i := 0; i < hub.SetupCodeMaxAttempts-1; i++ {
		// A different address every time and time passing: the attempt limit is about the code, not the IP limit.
		_, _, err := svc.SetupWithCode(ctx, setupIn(wrong, "max", setupPassword, "198.51.100."+string(rune('1'+i))))
		if errCodeOf(err) != hub.CodeInvalidSetupCode {
			t.Fatalf("try %d: %v", i, err)
		}
		clk.Advance(time.Second)
	}
	if c, _ := svc.CurrentSetupCode(ctx); c == nil {
		t.Fatal("the code must survive 9 wrong tries")
	}
	if _, _, err := svc.SetupWithCode(ctx, setupIn(wrong, "max", setupPassword, "198.51.100.99")); errCodeOf(err) != hub.CodeInvalidSetupCode {
		t.Fatalf("tenth: %v", err)
	}
	// The tenth wrong try deleted the code: even the right one is refused now.
	if _, _, err := svc.SetupWithCode(ctx, setupIn(code.Code, "max", setupPassword, "198.51.100.98")); errCodeOf(err) != hub.CodeInvalidSetupCode {
		t.Fatalf("after the limit: %v", err)
	}
	if c, _ := svc.CurrentSetupCode(ctx); c != nil {
		t.Fatal("code must be invalidated")
	}
	// A new code starts again.
	fresh, _ := svc.NewSetupCode(ctx)
	if _, _, err := svc.SetupWithCode(ctx, setupIn(fresh.Code, "max", setupPassword, "198.51.100.97")); err != nil {
		t.Fatal(err)
	}
}

func TestSetupCodeRateLimitPerIP(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	if _, err := svc.NewSetupCode(ctx); err != nil {
		t.Fatal(err)
	}
	var last error
	for i := 0; i < hub.MaxSignInPerIPPerMinute+1; i++ {
		_, _, last = svc.SetupWithCode(ctx, setupIn("AAAA-AA", "max", setupPassword, "203.0.113.5"))
	}
	if !errors.Is(last, hub.ErrRateLimited) {
		t.Fatalf("want rate limited, got %v", last)
	}
}

func TestSetupWithCodeParallelOneWins(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	code, _ := svc.NewSetupCode(ctx)
	const n = 6
	errs := make([]error, n)
	var wg sync.WaitGroup
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, _, errs[i] = svc.SetupWithCode(ctx, setupIn(code.Code, "admin"+string(rune('a'+i)), setupPassword, "192.0.2."+string(rune('1'+i))))
		}()
	}
	wg.Wait()
	wins := 0
	for _, err := range errs {
		switch {
		case err == nil:
			wins++
		case errCodeOf(err) != hub.CodeAlreadySetUp:
			t.Fatalf("unexpected: %v", err)
		}
	}
	admins, _ := svc.ListAdmins(ctx)
	if wins != 1 || len(admins) != 1 {
		t.Fatalf("wins=%d admins=%d", wins, len(admins))
	}
}

func TestCreateAdminDropsSetupCode(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	if _, err := svc.NewSetupCode(ctx); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.CreateAdmin(ctx, "admin", setupPassword); err != nil {
		t.Fatal(err)
	}
	if c, _ := svc.CurrentSetupCode(ctx); c != nil {
		t.Fatalf("code must go with the first admin: %+v", c)
	}
}

func TestResetAdminPasswordOnServer(t *testing.T) {
	e := newAcctEnv(t)
	member, _ := e.member("Anna", memberPassword)
	// Lock the admin out first.
	for i := 0; i < hub.SignInFailureLimit; i++ {
		in := e.in("admin", "wrong-password-1", "")
		in.RemoteAddr = "198.51.100." + string(rune('1'+i))
		_, _ = e.svc.SignIn(ctx, in)
	}
	in := e.in("admin", "admin-password-1", "")
	in.RemoteAddr = "198.51.100.200"
	if _, err := e.svc.SignIn(ctx, in); err == nil {
		t.Fatal("the admin must be locked")
	}
	before, _ := e.svc.ListOwnDevices(ctx, e.admin.ID, "")
	temp, err := e.svc.ResetAdminPasswordOnServer(ctx, e.admin.ID)
	if err != nil || temp == "" {
		t.Fatalf("%q %v", temp, err)
	}
	s := e.signIn("admin", temp, "")
	acc, err := e.svc.GetAccount(ctx, e.admin.ID, s.DeviceID)
	if err != nil || !acc.MustChangePassword {
		t.Fatalf("%+v %v", acc, err)
	}
	if _, err := e.svc.VerifyPassword(ctx, "admin", "admin-password-1"); err == nil {
		t.Fatal("the old password must be gone")
	}
	after, _ := e.svc.ListOwnDevices(ctx, e.admin.ID, "")
	if len(after) != len(before)+1 {
		t.Fatalf("devices were touched: %d -> %d", len(before), len(after))
	}
	if _, err := e.svc.ResetAdminPasswordOnServer(ctx, member.ID); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("a member is not an admin: %v", err)
	}
	if _, err := e.svc.ResetAdminPasswordOnServer(ctx, "u_nobody"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	admins, _ := e.svc.ListAdmins(ctx)
	if len(admins) != 1 || admins[0].Username != "admin" {
		t.Fatalf("%+v", admins)
	}
}
