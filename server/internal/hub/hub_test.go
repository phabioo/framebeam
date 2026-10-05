package hub_test

import (
	"bytes"
	"context"
	"crypto/rand"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

var ctx = context.Background()

func TestAdminSetupTwiceRefused(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	if has, _ := svc.HasAdmin(ctx); has {
		t.Fatal("admin before setup")
	}
	if _, err := svc.CreateAdmin(ctx, "fabio", "short"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("short password: %v", err)
	}
	u, err := svc.CreateAdmin(ctx, "fabio", "secret-1234")
	if err != nil || u.Role != hub.RoleAdmin {
		t.Fatalf("%v %+v", err, u)
	}
	if _, err := svc.CreateAdmin(ctx, "second", "secret-1234"); !errors.Is(err, hub.ErrAdminExists) {
		t.Fatalf("second admin: %v", err)
	}
}

func TestVerifyAndChangePassword(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	u, _ := svc.CreateAdmin(ctx, "fabio", "secret-1234")
	if _, err := svc.VerifyPassword(ctx, "FABIO", "secret-1234"); err != nil {
		t.Fatalf("verify: %v", err)
	}
	for _, c := range [][2]string{{"fabio", "wrong"}, {"nobody", "secret-1234"}} {
		if _, err := svc.VerifyPassword(ctx, c[0], c[1]); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatalf("%v: %v", c, err)
		}
	}
	plain, _ := svc.CreateUser(ctx, "anna", "Anna")
	if _, err := svc.VerifyPassword(ctx, "anna", ""); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatal("user without password must not be able to sign in")
	}
	tok, ws, err := svc.CreateWebSession(ctx, u.ID)
	if err != nil || ws.CSRFToken == "" {
		t.Fatal(err)
	}
	if _, err := svc.LookupWebSession(ctx, tok); err != nil {
		t.Fatal(err)
	}
	if err := svc.ChangePassword(ctx, u.ID, "new-password"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.LookupWebSession(ctx, tok); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatal("session still valid after password change")
	}
	if _, err := svc.VerifyPassword(ctx, "fabio", "secret-1234"); err == nil {
		t.Fatal("old password still valid")
	}
	if _, err := svc.VerifyPassword(ctx, "fabio", "new-password"); err != nil {
		t.Fatal(err)
	}
	if err := svc.ChangePassword(ctx, plain.ID, "new-password"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("user without password: %v", err)
	}
}

func TestWebSessionExpiry(t *testing.T) {
	svc, clk := hubtest.New(t, nil)
	u, _ := svc.CreateAdmin(ctx, "fabio", "secret-1234")
	tok, _, _ := svc.CreateWebSession(ctx, u.ID)
	clk.Advance(hub.WebSessionTTL + time.Second)
	if _, err := svc.LookupWebSession(ctx, tok); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatal("expired session valid")
	}
	if err := svc.Cleanup(ctx); err != nil {
		t.Fatal(err)
	}
}

func randomROM(n int) []byte {
	b := make([]byte, n)
	rand.Read(b)
	return b
}

func TestLibraryAddDuplicateDeleteStorage(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	u, _ := svc.CreateAdmin(ctx, "fabio", "secret-1234")
	rom := randomROM(5000)
	g, err := svc.AddROM(ctx, bytes.NewReader(rom), "demo.NDS", "", "", u.ID)
	if err != nil {
		t.Fatal(err)
	}
	if g.System != "nds" || g.Title != "demo" || g.ROMSize != 5000 || !hub.ValidSHA256(g.ROMSHA256) {
		t.Fatalf("%+v", g)
	}
	p := filepath.Join(svc.DataDir(), "roms", g.ROMSHA256[:2], g.ROMSHA256)
	if got, _ := os.ReadFile(p); !bytes.Equal(got, rom) {
		t.Fatal("ROM file missing or different")
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader(rom), "other.nds", "Other title", "nds", u.ID); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("duplicate: %v", err)
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader(rom), "x.bin", "", "", u.ID); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("system not derivable: %v", err)
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader(nil), "empty.nds", "", "", u.ID); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("empty: %v", err)
	}
	if tmps, _ := os.ReadDir(filepath.Join(svc.DataDir(), "tmp")); len(tmps) != 0 {
		t.Fatalf("temp files left over: %d", len(tmps))
	}
	st, err := svc.Storage(ctx)
	if err != nil || st.GameCount != 1 || st.ROMBytes != 5000 {
		t.Fatalf("%+v %v", st, err)
	}
	if err := svc.DeleteGame(ctx, g.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(p); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("file still present after delete")
	}
	if _, err := svc.GetGame(ctx, g.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal("entry still present after delete")
	}
}
