package hub_test

import (
	"errors"
	"fmt"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
)

func pairIn(device, ip string) hub.PairingInput {
	return hub.PairingInput{DeviceID: device, DeviceName: "PC", Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0",
		ProtocolVersion: 1, RemoteAddr: ip}
}

func TestPairingPerIPv6Prefix(t *testing.T) {
	svc, _, _ := newAdmin(t)
	for i := 0; i < hub.MaxPairingPerIPPerMinute; i++ { // different addresses inside one /64
		if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), fmt.Sprintf("2001:db8:1:2::%x", i+1))); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), "2001:db8:1:2:ffff::9")); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("same /64: %v", err)
	}
	if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), "2001:db8:1:3::1")); err != nil {
		t.Fatalf("other /64: %v", err)
	}
}

func TestInviteRedemptionsDoNotUseAnonymousPairingBudget(t *testing.T) {
	svc, clk, admin := newAdmin(t)
	for i := 0; i < hub.MaxOpenPairingRequests; i++ {
		if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), fmt.Sprintf("198.51.100.%d", i+1))); err != nil {
			t.Fatal(err)
		}
	}
	_, code, err := svc.CreateInvite(ctx, admin.ID, time.Hour, false)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := svc.RedeemInvite(ctx, redeemIn(code, "Anna")); err != nil {
		t.Fatalf("redeem with full anonymous budget: %v", err)
	}
	_ = clk
}

func TestRedeemFailuresOfOneIPDoNotLockOthers(t *testing.T) {
	svc, clk, admin := newAdmin(t)
	_, code, err := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 40; i++ { // many bad codes from many addresses never reach a per-IP limit
		in := redeemIn("FB-AAAA-AAAA", "x")
		in.RemoteAddr = fmt.Sprintf("198.51.100.%d", i+1)
		if _, err := svc.RedeemInvite(ctx, in); !errors.Is(err, hub.ErrInviteInvalid) {
			t.Fatalf("%d: %v", i, err)
		}
	}
	good := redeemIn(code, "Anna")
	good.RemoteAddr = "192.0.2.50"
	if _, err := svc.RedeemInvite(ctx, good); err != nil {
		t.Fatalf("legit redemption after others' failures: %v", err)
	}
	// One IPv6 /64 shares its budget.
	for i := 0; i < hub.MaxRedeemPerIPPerMinute; i++ {
		in := redeemIn("FB-AAAA-AAAA", "x")
		in.RemoteAddr = fmt.Sprintf("2001:db8:9:9::%x", i+1)
		svc.RedeemInvite(ctx, in)
	}
	in := redeemIn("FB-AAAA-AAAA", "x")
	in.RemoteAddr = "2001:db8:9:9:1::1"
	if _, err := svc.RedeemInvite(ctx, in); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("same /64: %v", err)
	}
	clk.Advance(time.Minute + time.Second)
}

func TestPairingCannotTakeOverTrustedDeviceOfAnotherUser(t *testing.T) {
	svc, _, admin := newAdmin(t)
	other, err := svc.CreateUser(ctx, "max", "Max")
	if err != nil {
		t.Fatal(err)
	}
	dev := pairedDevice(t, svc, admin.ID)
	c, err := svc.CreatePairingRequest(ctx, pairIn(dev, "192.0.2.7"))
	if err != nil {
		t.Fatal(err)
	}
	if err := svc.ApprovePairing(ctx, c.RequestID, other.ID); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("approve for another user: %v", err)
	}
	// Same user keeps working (re-pairing).
	if err := svc.ApprovePairing(ctx, c.RequestID, admin.ID); err != nil {
		t.Fatal(err)
	}
	if r, err := svc.PollPairing(ctx, c.RequestID, c.PollToken); err != nil || r.Status != hub.PairingApproved {
		t.Fatalf("%+v %v", r, err)
	}
	// Redemption without device authorization refuses a trusted device ID.
	_, code, _ := svc.CreateInvite(ctx, admin.ID, time.Hour, false)
	in := redeemIn(code, "Anna")
	in.DeviceID = dev
	if _, err := svc.RedeemInvite(ctx, in); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("redeem: %v", err)
	}
	in.DeviceID = uuid.NewString() // the invite stays usable
	if _, err := svc.RedeemInvite(ctx, in); err != nil {
		t.Fatal(err)
	}
}

func TestAccessTokensPerDeviceAreBounded(t *testing.T) {
	svc, _, admin := newAdmin(t)
	cred := ""
	dev := uuid.NewString()
	c, _ := svc.CreatePairingRequest(ctx, pairIn(dev, "192.0.2.8"))
	svc.ApprovePairing(ctx, c.RequestID, admin.ID)
	r, err := svc.PollPairing(ctx, c.RequestID, c.PollToken)
	if err != nil {
		t.Fatal(err)
	}
	cred = r.DeviceCredential
	var toks []string
	for i := 0; i < hub.MaxAccessTokensPerDevice+3; i++ {
		at, err := svc.IssueAccessToken(ctx, dev, cred)
		if err != nil {
			t.Fatal(err)
		}
		toks = append(toks, at.Token)
	}
	if _, err := svc.Authenticate(ctx, toks[0]); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatalf("oldest token still valid: %v", err)
	}
	if _, err := svc.Authenticate(ctx, toks[len(toks)-1]); err != nil {
		t.Fatalf("newest token: %v", err)
	}
	if _, err := svc.Authenticate(ctx, toks[3]); err != nil {
		t.Fatalf("token within the cap: %v", err)
	}
}

func TestPreHelloConnectionsPerDeviceAreLimited(t *testing.T) {
	svc, _, _ := newAdmin(t)
	dev := uuid.NewString()
	r1, ok1 := svc.AcquirePreHello(dev)
	_, ok2 := svc.AcquirePreHello(dev)
	_, ok3 := svc.AcquirePreHello(dev)
	if !ok1 || !ok2 || ok3 {
		t.Fatalf("%v %v %v", ok1, ok2, ok3)
	}
	r1()
	r1() // idempotent
	if _, ok := svc.AcquirePreHello(dev); !ok {
		t.Fatal("slot not freed")
	}
	if _, ok := svc.AcquirePreHello(dev); ok {
		t.Fatal("release must free one slot only")
	}
}

func TestMessageRateLimitClosesClient(t *testing.T) {
	svc, _, admin := newAdmin(t)
	dev := pairedDevice(t, svc, admin.ID)
	d, err := svc.GetDevice(ctx, dev)
	if err != nil {
		t.Fatal(err)
	}
	cl := svc.NewClient(hub.Principal{Device: d, User: admin})
	hello := []byte(fmt.Sprintf(`{"type":"hello","payload":{"protocol_version":1,"device_id":%q}}`, dev))
	if err := cl.Handle(hello); err != nil {
		t.Fatal(err)
	}
	var failed error
	for i := 0; i < 1000 && failed == nil; i++ {
		failed = cl.Handle([]byte(`{"type":"presence_update","payload":{"state":"online"}}`))
	}
	if failed == nil {
		t.Fatal("flood not stopped")
	}
	select {
	case <-cl.Done():
	default:
		t.Fatal("client not closed")
	}
	if code, _ := cl.CloseInfo(); code != hub.CloseCodePolicy {
		t.Fatalf("close code %d", code)
	}
}

func TestPresenceFloodDoesNotCloseReceiver(t *testing.T) {
	svc, _, admin := newAdmin(t)
	mk := func() (*hub.Client, string) {
		dev := pairedDevice(t, svc, admin.ID)
		d, _ := svc.GetDevice(ctx, dev)
		cl := svc.NewClient(hub.Principal{Device: d, User: admin})
		if err := cl.Handle([]byte(fmt.Sprintf(`{"type":"hello","payload":{"protocol_version":1,"device_id":%q}}`, dev))); err != nil {
			t.Fatal(err)
		}
		return cl, dev
	}
	recv, _ := mk() // never drains its queue
	send, _ := mk()
	for i := 0; i < 55; i++ { // below the rate limit, far above the queue size
		if err := send.Handle([]byte(`{"type":"presence_update","payload":{"state":"in_game"}}`)); err != nil {
			t.Fatal(err)
		}
	}
	select {
	case <-recv.Done():
		t.Fatal("receiver closed as slow consumer by presence traffic")
	default:
	}
}
