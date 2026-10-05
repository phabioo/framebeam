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
		t.Fatal("Admin vor Setup")
	}
	if _, err := svc.CreateAdmin(ctx, "fabio", "kurz"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("kurzes Passwort: %v", err)
	}
	u, err := svc.CreateAdmin(ctx, "fabio", "geheim-1234")
	if err != nil || u.Role != hub.RoleAdmin {
		t.Fatalf("%v %+v", err, u)
	}
	if _, err := svc.CreateAdmin(ctx, "zweiter", "geheim-1234"); !errors.Is(err, hub.ErrAdminExists) {
		t.Fatalf("zweiter Admin: %v", err)
	}
}

func TestVerifyAndChangePassword(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	u, _ := svc.CreateAdmin(ctx, "fabio", "geheim-1234")
	if _, err := svc.VerifyPassword(ctx, "FABIO", "geheim-1234"); err != nil {
		t.Fatalf("verify: %v", err)
	}
	for _, c := range [][2]string{{"fabio", "falsch"}, {"niemand", "geheim-1234"}} {
		if _, err := svc.VerifyPassword(ctx, c[0], c[1]); !errors.Is(err, hub.ErrInvalidCredentials) {
			t.Fatalf("%v: %v", c, err)
		}
	}
	plain, _ := svc.CreateUser(ctx, "anna", "Anna")
	if _, err := svc.VerifyPassword(ctx, "anna", ""); !errors.Is(err, hub.ErrInvalidCredentials) {
		t.Fatal("User ohne Passwort darf sich nicht anmelden")
	}
	tok, ws, err := svc.CreateWebSession(ctx, u.ID)
	if err != nil || ws.CSRFToken == "" {
		t.Fatal(err)
	}
	if _, err := svc.LookupWebSession(ctx, tok); err != nil {
		t.Fatal(err)
	}
	if err := svc.ChangePassword(ctx, u.ID, "neues-passwort"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.LookupWebSession(ctx, tok); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatal("Session nach Passwortwechsel noch gültig")
	}
	if _, err := svc.VerifyPassword(ctx, "fabio", "geheim-1234"); err == nil {
		t.Fatal("altes Passwort gilt noch")
	}
	if _, err := svc.VerifyPassword(ctx, "fabio", "neues-passwort"); err != nil {
		t.Fatal(err)
	}
	if err := svc.ChangePassword(ctx, plain.ID, "neues-passwort"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("User ohne Passwort: %v", err)
	}
}

func TestWebSessionExpiry(t *testing.T) {
	svc, clk := hubtest.New(t, nil)
	u, _ := svc.CreateAdmin(ctx, "fabio", "geheim-1234")
	tok, _, _ := svc.CreateWebSession(ctx, u.ID)
	clk.Advance(hub.WebSessionTTL + time.Second)
	if _, err := svc.LookupWebSession(ctx, tok); !errors.Is(err, hub.ErrUnauthorized) {
		t.Fatal("abgelaufene Session gültig")
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
	u, _ := svc.CreateAdmin(ctx, "fabio", "geheim-1234")
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
		t.Fatal("ROM-Datei fehlt oder abweichend")
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader(rom), "other.nds", "Anderer Titel", "nds", u.ID); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("Duplikat: %v", err)
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader(rom), "x.bin", "", "", u.ID); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("System nicht ableitbar: %v", err)
	}
	if _, err := svc.AddROM(ctx, bytes.NewReader(nil), "leer.nds", "", "", u.ID); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("leer: %v", err)
	}
	if tmps, _ := os.ReadDir(filepath.Join(svc.DataDir(), "tmp")); len(tmps) != 0 {
		t.Fatalf("Temp-Dateien übrig: %d", len(tmps))
	}
	st, err := svc.Storage(ctx)
	if err != nil || st.GameCount != 1 || st.ROMBytes != 5000 {
		t.Fatalf("%+v %v", st, err)
	}
	if err := svc.DeleteGame(ctx, g.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(p); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("Datei nach Delete noch da")
	}
	if _, err := svc.GetGame(ctx, g.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal("Eintrag nach Delete noch da")
	}
}
