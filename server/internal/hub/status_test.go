package hub_test

import (
	"bytes"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestActiveSessionSummaries(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	admin, _ := svc.CreateAdmin(ctx, "fabio", "secret-12345")
	g, err := svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "Demo", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	if got, err := svc.ActiveSessionSummaries(ctx); err != nil || len(got) != 0 {
		t.Fatalf("no Session: %v %v", got, err)
	}
	owner, viewer := principal(t, svc, admin.ID, "Desktop"), principal(t, svc, admin.ID, "Laptop")
	s, err := svc.PublishSession(ctx, owner, g.ID, hub.VisibilityHubUsers)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := svc.JoinSession(ctx, viewer, s.SessionID); err != nil {
		t.Fatal(err)
	}
	got, err := svc.ActiveSessionSummaries(ctx)
	if err != nil || len(got) != 1 {
		t.Fatalf("%v %v", got, err)
	}
	if x := got[0]; x.GameTitle != "Demo" || x.Viewers != 1 || x.DeviceName != "Desktop" || x.Visibility != hub.VisibilityHubUsers || x.Owner == "" {
		t.Fatalf("%+v", x)
	}
	// Nobody is connected over WSS in this test.
	if pl, err := svc.ConnectedPlayers(ctx); err != nil || len(pl) != 0 {
		t.Fatalf("players: %v %v", pl, err)
	}
}

func TestMetricsTokenLifecycle(t *testing.T) {
	svc, _, db := hubtest.OpenIn(t, t.TempDir(), nil)
	if svc.MetricsEnabled(ctx) || svc.MetricsTokenSet(ctx) || svc.CheckMetricsToken(ctx, "x") {
		t.Fatal("off by default")
	}
	tok, err := svc.SetMetricsEnabled(ctx, true)
	if err != nil || !strings.HasPrefix(tok, "fbm_") || len(tok) < 40 {
		t.Fatalf("first enable creates a token: %q %v", tok, err)
	}
	if !svc.MetricsEnabled(ctx) || !svc.CheckMetricsToken(ctx, tok) {
		t.Fatal("token must work while on")
	}
	if svc.CheckMetricsToken(ctx, "") || svc.CheckMetricsToken(ctx, tok+"x") || svc.CheckMetricsToken(ctx, "fbm_wrong") {
		t.Fatal("wrong tokens")
	}
	// Only a hash is stored.
	var raw string
	if err := db.QueryRow(`SELECT value FROM settings WHERE key = 'metrics_token_hash'`).Scan(&raw); err != nil {
		t.Fatal(err)
	}
	if raw == "" || strings.Contains(raw, tok) || raw == tok {
		t.Fatalf("stored value must be a hash: %q", raw)
	}
	// Off and on again keeps the token (and does not show it again).
	if _, err := svc.SetMetricsEnabled(ctx, false); err != nil {
		t.Fatal(err)
	}
	if svc.CheckMetricsToken(ctx, tok) {
		t.Fatal("off: the token must not pass")
	}
	if again, err := svc.SetMetricsEnabled(ctx, true); err != nil || again != "" || !svc.CheckMetricsToken(ctx, tok) {
		t.Fatalf("re-enable keeps the token: %q %v", again, err)
	}
	// Regenerate: the old token stops working.
	nt, err := svc.RegenerateMetricsToken(ctx)
	if err != nil || nt == tok {
		t.Fatalf("%q %v", nt, err)
	}
	if svc.CheckMetricsToken(ctx, tok) || !svc.CheckMetricsToken(ctx, nt) {
		t.Fatal("old token revoked, new one valid")
	}
}
