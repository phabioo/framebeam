package hub_test

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
	"github.com/phabioo/framebeam/server/internal/store"
)

func newAdmin(t *testing.T) (*hub.Service, *hubtest.Clock, hub.User) {
	t.Helper()
	svc, clk := hubtest.New(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	return svc, clk, admin
}

func redeemIn(code, name string) hub.RedeemInput {
	return hub.RedeemInput{Code: code, DisplayName: name, DeviceID: uuid.NewString(), DeviceName: name + "-PC", Platform: "linux",
		Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1, RemoteAddr: "192.0.2.1"}
}

var codeRe = regexp.MustCompile(`^FB-[2-9A-HJKMNP-Z]{4}-[2-9A-HJKMNP-Z]{4}$`)

func TestInviteCodeFormatAndOnlyHashStored(t *testing.T) {
	svc, _, admin := newAdmin(t)
	inv, code, err := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	if err != nil {
		t.Fatal(err)
	}
	if !codeRe.MatchString(code) {
		t.Fatalf("code %q", code)
	}
	if inv.Status != hub.InviteActive || !inv.ExpiresAt.Equal(inv.CreatedAt.Add(time.Hour)) {
		t.Fatalf("%+v", inv)
	}
	if _, _, err := svc.CreateInvite(ctx, admin.ID, 2*time.Hour, true); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("odd expiry: %v", err)
	}
	// The database holds a hash only.
	db, err := store.Open(filepath.Join(svc.DataDir(), "framebeam.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	var hash string
	if err := db.QueryRow(`SELECT code_hash FROM invites`).Scan(&hash); err != nil {
		t.Fatal(err)
	}
	if hash == "" || strings.Contains(hash, code[3:]) || strings.Contains(hash, strings.ReplaceAll(code, "-", "")) {
		t.Fatalf("stored value %q", hash)
	}
	// Lenient input: lowercase, no dashes, spaces.
	res, err := svc.RedeemInvite(ctx, redeemIn(" "+strings.ToLower(strings.ReplaceAll(code, "-", ""))+" ", "Anna"))
	if err != nil || !res.Approved || !strings.HasPrefix(res.Credential, "fbd_") {
		t.Fatalf("%v %+v", err, res)
	}
}

func TestRedeemApprovedCreatesUserAndTrustedDevice(t *testing.T) {
	svc, _, admin := newAdmin(t)
	_, code, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	in := redeemIn(code, "  Lena Müller ")
	res, err := svc.RedeemInvite(ctx, in)
	if err != nil || !res.Approved || res.UserID == "" || res.HubID != svc.Info().HubID {
		t.Fatalf("%v %+v", err, res)
	}
	u, err := svc.GetUser(ctx, res.UserID)
	if err != nil || u.Role != hub.RoleUser || u.DisplayName != "Lena Müller" || u.Username != "lena-m-ller" {
		t.Fatalf("%v %+v", err, u)
	}
	// No password: the user cannot sign in on the web.
	if _, err := svc.VerifyPassword(ctx, u.Username, ""); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatalf("password login: %v", err)
	}
	d, err := svc.GetDevice(ctx, in.DeviceID)
	if err != nil || d.UserID != u.ID || d.Status != hub.DeviceTrusted {
		t.Fatalf("%v %+v", err, d)
	}
	if _, err := svc.IssueAccessToken(ctx, in.DeviceID, res.Credential); err != nil {
		t.Fatal(err)
	}
	// Single use.
	if _, err := svc.RedeemInvite(ctx, redeemIn(code, "Other")); !errors.Is(err, hub.ErrInviteInvalid) {
		t.Fatalf("reuse: %v", err)
	}
	invs, _ := svc.ListInvites(ctx, 0)
	if len(invs) != 1 || invs[0].Status != hub.InviteRedeemed || invs[0].RedeemedBy != "Lena Müller" {
		t.Fatalf("%+v", invs)
	}
}

func TestRedeemPendingIsPreAssignedAndApprovable(t *testing.T) {
	svc, _, admin := newAdmin(t)
	_, code, _ := svc.CreateInvite(ctx, admin.ID, 15*time.Minute, false)
	in := redeemIn(code, "Jonas")
	res, err := svc.RedeemInvite(ctx, in)
	if err != nil || res.Approved || res.RequestID == "" || !strings.HasPrefix(res.PollToken, "fbp_") || res.Credential != "" {
		t.Fatalf("%v %+v", err, res)
	}
	if _, err := svc.GetDevice(ctx, in.DeviceID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("device before approval: %v", err)
	}
	pend, _ := svc.ListPendingRequests(ctx)
	if len(pend) != 1 || pend[0].UserID != res.UserID || pend[0].ID != res.RequestID {
		t.Fatalf("%+v", pend)
	}
	if p, err := svc.PollPairing(ctx, res.RequestID, res.PollToken); err != nil || p.Status != hub.PairingPending {
		t.Fatalf("%v %+v", err, p)
	}
	if err := svc.ApprovePairing(ctx, res.RequestID, res.UserID); err != nil {
		t.Fatal(err)
	}
	p, err := svc.PollPairing(ctx, res.RequestID, res.PollToken)
	if err != nil || p.Status != hub.PairingApproved || p.UserID != res.UserID || p.DeviceCredential == "" {
		t.Fatalf("%v %+v", err, p)
	}
}

func TestRedeemInvalidExpiredRevokedAndNameTaken(t *testing.T) {
	svc, clk, admin := newAdmin(t)
	bad := func(code string) {
		t.Helper()
		clk.Advance(time.Minute) // keep clear of the rate limit
		if _, err := svc.RedeemInvite(ctx, redeemIn(code, "X"+code)); !errors.Is(err, hub.ErrInviteInvalid) {
			t.Fatalf("%q: %v", code, err)
		}
	}
	bad("FB-AAAA-AAAA")
	bad("")
	bad("garbage")

	// Expired.
	_, code, _ := svc.CreateInvite(ctx, admin.ID, 15*time.Minute, true)
	clk.Advance(16 * time.Minute)
	bad(code)
	invs, _ := svc.ListInvites(ctx, 0)
	if invs[0].Status != hub.InviteExpired {
		t.Fatalf("%+v", invs[0])
	}
	if err := svc.RevokeInvite(ctx, invs[0].ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("revoke expired: %v", err)
	}

	// Revoked.
	inv, code, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	if err := svc.RevokeInvite(ctx, inv.ID); err != nil {
		t.Fatal(err)
	}
	bad(code)

	// Display name taken (case-insensitive, also the admin's name); the invite stays usable.
	_, code, _ = svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	clk.Advance(time.Minute)
	for _, n := range []string{"ADMIN", "admin"} {
		if _, err := svc.RedeemInvite(ctx, redeemIn(code, n)); !errors.Is(err, hub.ErrDisplayNameTaken) {
			t.Fatalf("%s: %v", n, err)
		}
	}
	// Length rules.
	for _, n := range []string{"", "   ", strings.Repeat("x", 33)} {
		clk.Advance(time.Minute)
		if _, err := svc.RedeemInvite(ctx, redeemIn(code, n)); !errors.Is(err, hub.ErrBadRequest) {
			t.Fatalf("%q: %v", n, err)
		}
	}
	clk.Advance(time.Minute)
	if _, err := svc.RedeemInvite(ctx, redeemIn(code, "Admin2")); err != nil {
		t.Fatalf("invite must still be usable: %v", err)
	}
	// Same display name again (new invite) -> taken; slug collisions get a suffix.
	_, code, _ = svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	clk.Advance(time.Minute)
	r, err := svc.RedeemInvite(ctx, redeemIn(code, "admin-2"))
	if err != nil {
		t.Fatal(err)
	}
	u, _ := svc.GetUser(ctx, r.UserID)
	if u.Username == "admin2" || u.Username == "" {
		t.Fatalf("username %q", u.Username)
	}
}

func TestRedeemUntrustedDeviceCannotBeTakenOver(t *testing.T) {
	svc, _, admin := newAdmin(t)
	_, c1, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	in := redeemIn(c1, "First")
	if _, err := svc.RedeemInvite(ctx, in); err != nil {
		t.Fatal(err)
	}
	_, c2, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	in2 := redeemIn(c2, "Second")
	in2.DeviceID = in.DeviceID
	if _, err := svc.RedeemInvite(ctx, in2); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("%v", err)
	}
	// The failed attempt left the invite usable.
	if _, err := svc.RedeemInvite(ctx, redeemIn(c2, "Second")); err != nil {
		t.Fatal(err)
	}
}

func TestRedeemConcurrentSingleUse(t *testing.T) {
	svc, _, admin := newAdmin(t)
	_, code, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	const n = 8
	var wg sync.WaitGroup
	var mu sync.Mutex
	ok, invalid := 0, 0
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			in := redeemIn(code, fmt.Sprintf("User%d", i))
			in.RemoteAddr = fmt.Sprintf("192.0.2.%d", i+1)
			_, err := svc.RedeemInvite(ctx, in)
			mu.Lock()
			defer mu.Unlock()
			switch {
			case err == nil:
				ok++
			case errors.Is(err, hub.ErrInviteInvalid):
				invalid++
			default:
				t.Errorf("unexpected: %v", err)
			}
		}(i)
	}
	wg.Wait()
	if ok != 1 || invalid != n-1 {
		t.Fatalf("ok=%d invalid=%d", ok, invalid)
	}
	users, _ := svc.ListUsers(ctx)
	if len(users) != 2 { // admin + one new user
		t.Fatalf("%d users", len(users))
	}
}

func TestRedeemRateLimitLikePairing(t *testing.T) {
	svc, clk, _ := newAdmin(t)
	for i := 0; i < hub.MaxRedeemPerIPPerMinute; i++ {
		if _, err := svc.RedeemInvite(ctx, redeemIn("FB-AAAA-AAAA", "x")); !errors.Is(err, hub.ErrInviteInvalid) {
			t.Fatalf("%d: %v", i, err)
		}
	}
	if _, err := svc.RedeemInvite(ctx, redeemIn("FB-AAAA-AAAA", "x")); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("limit: %v", err)
	}
	other := redeemIn("FB-AAAA-AAAA", "x")
	other.RemoteAddr = "192.0.2.99"
	if _, err := svc.RedeemInvite(ctx, other); !errors.Is(err, hub.ErrInviteInvalid) {
		t.Fatalf("other IP: %v", err)
	}
	clk.Advance(61 * time.Second)
	if _, err := svc.RedeemInvite(ctx, redeemIn("FB-AAAA-AAAA", "x")); !errors.Is(err, hub.ErrInviteInvalid) {
		t.Fatalf("after a minute: %v", err)
	}
}

func TestDisableUserRules(t *testing.T) {
	svc, _, admin := newAdmin(t)
	if err := svc.DisableUser(ctx, admin.ID); !errors.Is(err, hub.ErrForbidden) {
		t.Fatalf("admin: %v", err)
	}
	if err := svc.DisableUser(ctx, "u_missing"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown: %v", err)
	}
	u, _ := svc.CreateUser(ctx, "max", "Max")
	_, code, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	if err := svc.DisableUser(ctx, u.ID); err != nil {
		t.Fatal(err)
	}
	// Pending invites are unaffected.
	if _, err := svc.RedeemInvite(ctx, redeemIn(code, "Newbie")); err != nil {
		t.Fatalf("invite after disable: %v", err)
	}
	rows, _ := svc.ListUserRows(ctx)
	var got hub.UserRow
	for _, r := range rows {
		if r.ID == u.ID {
			got = r
		}
	}
	if !got.Disabled() {
		t.Fatalf("%+v", got)
	}
	if err := svc.EnableUser(ctx, u.ID); err != nil {
		t.Fatal(err)
	}
	if g, _ := svc.GetUser(ctx, u.ID); g.Disabled() {
		t.Fatal("still disabled")
	}
}

// ---- Registry, firmware, reports ----

func dummy(n int) []byte { return bytes.Repeat([]byte{0x5a}, n) }

func sum(b []byte) string { h := sha256.Sum256(b); return hex.EncodeToString(h[:]) }

func TestRegistrySeed(t *testing.T) {
	svc, _, _ := newAdmin(t)
	reg, err := svc.ListRegistry(ctx)
	if err != nil || len(reg) != 2 || reg[0].ID != "3ds" {
		t.Fatalf("%v %+v", err, reg)
	}
	e := reg[1]
	if e.ID != "nds" || e.CoreID != "" || e.ExpectedCoreVersion != "" || len(e.Cores) != 0 || strings.Join(e.LibretroIDs, ",") != "nds" || e.FirmwareMode != hub.FirmwareBuiltin ||
		e.Provisioning != "Included in the Player" || strings.Join(e.Platforms, ",") != "windows-x86_64,linux-x86_64" {
		t.Fatalf("%+v", e)
	}
	if len(e.Firmware) != 3 || e.Firmware[0].ID != "bios7" || e.Firmware[1].ID != "bios9" || e.Firmware[2].ID != "firmware" {
		t.Fatalf("%+v", e.Firmware)
	}
	for _, f := range e.Firmware {
		if f.Required || f.Present || f.State != hub.FirmwareOptional {
			t.Fatalf("%+v", f)
		}
	}
	if n, _ := svc.FirmwareProblems(ctx); n != 0 {
		t.Fatalf("badge in builtin mode: %d", n)
	}
	if err := svc.SetFirmwareMode(ctx, "nds", "weird"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
}

func TestFirmwareValidationStatusAndBadge(t *testing.T) {
	svc, _, _ := newAdmin(t)
	if err := svc.SetFirmwareMode(ctx, "nds", hub.FirmwareNative); err != nil {
		t.Fatal(err)
	}
	if n, _ := svc.FirmwareProblems(ctx); n != 3 {
		t.Fatalf("badge %d", n)
	}
	// Wrong sizes are rejected and nothing is stored.
	for _, c := range []struct {
		id string
		n  int
	}{{"bios7", 100}, {"bios7", 4096}, {"bios9", 16384}, {"firmware", 131073}, {"firmware", 600000}, {"bios7", 0}} {
		if _, err := svc.ProvideFirmware(ctx, "nds", c.id, bytes.NewReader(dummy(c.n))); !errors.Is(err, hub.ErrBadRequest) {
			t.Fatalf("%s %d: %v", c.id, c.n, err)
		}
	}
	if _, err := svc.ProvideFirmware(ctx, "nds", "nope", bytes.NewReader(dummy(10))); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "firmware", "nds", "bios7")); !os.IsNotExist(err) {
		t.Fatalf("file stored after a failed upload: %v", err)
	}

	b7, b9, fw := dummy(16384), dummy(4096), dummy(262144)
	f, err := svc.ProvideFirmware(ctx, "nds", "bios7", bytes.NewReader(b7))
	if err != nil || !f.Present || f.State != hub.FirmwareValid || f.SHA256 != sum(b7) || f.Size != 16384 {
		t.Fatalf("%v %+v", err, f)
	}
	fi, err := os.Stat(filepath.Join(svc.DataDir(), "firmware", "nds", "bios7"))
	if err != nil || runtime.GOOS != "windows" && fi.Mode().Perm() != 0o600 { // Windows has no unix modes
		t.Fatalf("%v %v", err, fi)
	}
	if _, err := svc.ProvideFirmware(ctx, "nds", "bios9", bytes.NewReader(b9)); err != nil {
		t.Fatal(err)
	}
	if n, _ := svc.FirmwareProblems(ctx); n != 1 {
		t.Fatalf("badge %d", n)
	}

	// Pin: a differing hash is rejected on upload, a differing pin on an existing file gives a mismatch.
	if err := svc.SetFirmwarePin(ctx, "nds", "firmware", "XYZ"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
	if err := svc.SetFirmwarePin(ctx, "nds", "firmware", strings.Repeat("a", 64)); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.ProvideFirmware(ctx, "nds", "firmware", bytes.NewReader(fw)); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("pinned hash differs: %v", err)
	}
	if err := svc.SetFirmwarePin(ctx, "nds", "firmware", sum(fw)); err != nil {
		t.Fatal(err)
	}
	if f, err := svc.ProvideFirmware(ctx, "nds", "firmware", bytes.NewReader(fw)); err != nil || f.State != hub.FirmwareValid {
		t.Fatalf("%v %+v", err, f)
	}
	if err := svc.SetFirmwarePin(ctx, "nds", "bios9", strings.Repeat("b", 64)); err != nil {
		t.Fatal(err)
	}
	e, _ := svc.GetRegistryEntry(ctx, "nds")
	if e.Firmware[1].State != hub.FirmwareMismatch {
		t.Fatalf("%+v", e.Firmware[1])
	}
	if n, _ := svc.FirmwareProblems(ctx); n != 1 {
		t.Fatalf("badge %d", n)
	}
	// Clearing the pin restores "valid"; replace works.
	if err := svc.SetFirmwarePin(ctx, "nds", "bios9", ""); err != nil {
		t.Fatal(err)
	}
	if f, err := svc.ProvideFirmware(ctx, "nds", "bios9", bytes.NewReader(append(dummy(4095), 1))); err != nil || f.SHA256 == sum(b9) || f.State != hub.FirmwareValid {
		t.Fatalf("%v %+v", err, f)
	}
	if n, _ := svc.FirmwareProblems(ctx); n != 0 {
		t.Fatalf("badge %d", n)
	}

	// A pinned-hash mismatch is not downloadable.
	if err := svc.SetFirmwarePin(ctx, "nds", "bios7", strings.Repeat("c", 64)); err != nil {
		t.Fatal(err)
	}
	if _, _, err := svc.OpenFirmware(ctx, "nds", "bios7"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("mismatching file downloadable: %v", err)
	}
	svc.SetFirmwarePin(ctx, "nds", "bios7", "")

	// Download and remove.
	fh, def, err := svc.OpenFirmware(ctx, "nds", "bios7")
	if err != nil {
		t.Fatal(err)
	}
	got, _ := io.ReadAll(fh)
	fh.Close()
	if !bytes.Equal(got, b7) || def.SHA256 != sum(b7) {
		t.Fatal("content")
	}
	if err := svc.RemoveFirmware(ctx, "nds", "bios7"); err != nil {
		t.Fatal(err)
	}
	if _, _, err := svc.OpenFirmware(ctx, "nds", "bios7"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	if err := svc.RemoveFirmware(ctx, "nds", "bios7"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	if _, _, err := svc.OpenFirmware(ctx, "nds", "../bios7"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	if n, _ := svc.FirmwareProblems(ctx); n != 1 {
		t.Fatalf("badge after remove %d", n)
	}
	// Back to builtin: nothing required.
	svc.SetFirmwareMode(ctx, "nds", hub.FirmwareBuiltin)
	if n, _ := svc.FirmwareProblems(ctx); n != 0 {
		t.Fatalf("badge %d", n)
	}
}

func pairedDevice(t *testing.T, svc *hub.Service, adminID string) string {
	t.Helper()
	id := uuid.NewString()
	c, err := svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: id, DeviceName: "PC", Platform: "linux", Arch: "x86_64",
		PlayerVersion: "0.0.1", ProtocolVersion: 1, RemoteAddr: "192.0.2.5"})
	if err != nil {
		t.Fatal(err)
	}
	svc.ApprovePairing(ctx, c.RequestID, adminID)
	if _, err := svc.PollPairing(ctx, c.RequestID, c.PollToken); err != nil {
		t.Fatal(err)
	}
	return id
}

func TestHandshakeCoreChecksAndReports(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	dev := pairedDevice(t, svc, admin.ID)
	hs := func(cores *[]hub.CoreReport) hub.HandshakeResult {
		t.Helper()
		r, err := svc.Handshake(ctx, dev, hub.HandshakeInput{Platform: "windows", Arch: "x86_64", PlayerVersion: "0.2.0",
			ProtocolVersion: 1, MinProtocolVersion: 1, Cores: cores})
		if err != nil {
			t.Fatal(err)
		}
		return r
	}
	if _, err := svc.RefreshCatalog(ctx); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.InstallCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	_ = bb
	// Not reported: no check, no report.
	if r := hs(nil); len(r.Problems) != 0 || !r.Compatible {
		t.Fatalf("%+v", r)
	}
	if reps, _ := svc.ListClientReports(ctx, mustEntry(t, svc)); len(reps) != 0 {
		t.Fatalf("%+v", reps)
	}
	// Missing core: warning only.
	r := hs(&[]hub.CoreReport{})
	if !r.Compatible || len(r.Problems) != 1 || r.Problems[0].Code != hub.ProblemCoreMissing || r.Problems[0].CoreID != "desmume" {
		t.Fatalf("%+v", r)
	}
	// Other version: warning only.
	r = hs(&[]hub.CoreReport{{ID: "desmume", Version: "2026.10.01"}})
	if !r.Compatible || len(r.Problems) != 1 || r.Problems[0].Code != hub.ProblemCoreVersionMismatch || !strings.Contains(r.Problems[0].Detail, "2026.10.09") {
		t.Fatalf("%+v", r)
	}
	reps, _ := svc.ListClientReports(ctx, mustEntry(t, svc))
	if len(reps) != 1 || reps[0].Status != hub.ClientCoreMismatch || reps[0].Expected != "2026.10.09" || reps[0].CoreVersion != "2026.10.01" ||
		reps[0].Platform != "windows" || reps[0].PlayerVersion != "0.2.0" {
		t.Fatalf("%+v", reps)
	}
	if d, _ := svc.GetDevice(ctx, dev); d.PlayerVersion != "0.2.0" || d.Platform != "windows" {
		t.Fatalf("%+v", d)
	}
	// Exact version, other cores ignored.
	r = hs(&[]hub.CoreReport{{ID: "desmume", Version: "2026.10.09"}, {ID: "other", Version: "9"}})
	if !r.Compatible || len(r.Problems) != 0 {
		t.Fatalf("%+v", r)
	}
	if reps, _ = svc.ListClientReports(ctx, mustEntry(t, svc)); reps[0].Status != hub.ClientCompatible {
		t.Fatalf("%+v", reps)
	}
	// A protocol problem still makes the Player incompatible next to core warnings.
	r2, err := svc.Handshake(ctx, dev, hub.HandshakeInput{Platform: "windows", Arch: "x86_64", PlayerVersion: "0.2.0",
		ProtocolVersion: 5, MinProtocolVersion: 5, Cores: &[]hub.CoreReport{}})
	if err != nil || r2.Compatible || len(r2.Problems) != 2 {
		t.Fatalf("%v %+v", err, r2)
	}
	// No core installed for the system: nothing to compare, no warning.
	if err := svc.RemoveCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	if r = hs(&[]hub.CoreReport{}); len(r.Problems) != 0 || !r.Compatible {
		t.Fatalf("%+v", r)
	}
	// A revoked device disappears from the list.
	svc.RevokeDevice(ctx, dev)
	if reps, _ = svc.ListClientReports(ctx, mustEntry(t, svc)); len(reps) != 0 {
		t.Fatalf("%+v", reps)
	}
}

func mustEntry(t *testing.T, svc *hub.Service) hub.SystemEntry {
	t.Helper()
	e, err := svc.GetRegistryEntry(ctx, "nds")
	if err != nil {
		t.Fatal(err)
	}
	return e
}

func TestSettingsAndUploadPermission(t *testing.T) {
	svc, _, admin := newAdmin(t)
	user, _ := svc.CreateUser(ctx, "max", "Max")
	if on, _ := svc.AllowUserUploads(ctx); on {
		t.Fatal("default must be off")
	}
	if ok, _ := svc.CanUpload(ctx, admin); !ok {
		t.Fatal("admin")
	}
	if ok, _ := svc.CanUpload(ctx, user); ok {
		t.Fatal("user while off")
	}
	svc.SetAllowUserUploads(ctx, true)
	if ok, _ := svc.CanUpload(ctx, user); !ok {
		t.Fatal("user while on")
	}
	if a, _ := svc.Appearance(ctx); a != hub.AppearanceLight {
		t.Fatal(a)
	}
	for _, m := range []string{"dark", "system", "light"} {
		if err := svc.SetAppearance(ctx, m); err != nil {
			t.Fatal(err)
		}
		if a, _ := svc.Appearance(ctx); a != m {
			t.Fatalf("%s != %s", a, m)
		}
	}
	if err := svc.SetAppearance(ctx, "neon"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
}

func TestProvideFirmwareKeepsOldFileWhenMetadataUpdateFails(t *testing.T) {
	svc, _, _ := newAdmin(t)
	v1, v2 := dummy(16384), bytes.Repeat([]byte{0x33}, 16384)
	if _, err := svc.ProvideFirmware(ctx, "nds", "bios7", bytes.NewReader(v1)); err != nil {
		t.Fatal(err)
	}
	db, err := store.Open(filepath.Join(svc.DataDir(), "framebeam.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	if _, err := db.Exec(`CREATE TRIGGER fw_fail BEFORE INSERT ON firmware_files BEGIN SELECT RAISE(ABORT, 'boom'); END`); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.ProvideFirmware(ctx, "nds", "bios7", bytes.NewReader(v2)); err == nil {
		t.Fatal("expected failure")
	}
	got, err := os.ReadFile(filepath.Join(svc.DataDir(), "firmware", "nds", "bios7"))
	if err != nil || !bytes.Equal(got, v1) {
		t.Fatalf("old file not restored: %v", err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "firmware", "nds", "bios7.bak")); !os.IsNotExist(err) {
		t.Fatalf("backup left behind: %v", err)
	}
	e, _ := svc.GetRegistryEntry(ctx, "nds")
	if e.Firmware[0].SHA256 != sum(v1) {
		t.Fatal("metadata changed")
	}
	// First-time provide failing leaves no file.
	if _, err := svc.ProvideFirmware(ctx, "nds", "bios9", bytes.NewReader(dummy(4096))); err == nil {
		t.Fatal("expected failure")
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "firmware", "nds", "bios9")); !os.IsNotExist(err) {
		t.Fatalf("orphan file: %v", err)
	}
	// Success path removes the backup.
	db.Exec(`DROP TRIGGER fw_fail`)
	if _, err := svc.ProvideFirmware(ctx, "nds", "bios7", bytes.NewReader(v2)); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "firmware", "nds", "bios7.bak")); !os.IsNotExist(err) {
		t.Fatalf("backup left behind: %v", err)
	}
}
