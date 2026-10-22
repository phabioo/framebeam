package hub_test

import (
	"errors"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

func TestCoreAutoUpdateSystemOverrideAndCorePin(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	refresh(t, svc)
	for _, id := range [][2]string{{"nds", "desmume"}, {"3ds", "azahar"}} {
		if _, err := svc.InstallCore(ctx, id[0], id[1]); err != nil {
			t.Fatal(err)
		}
	}
	// Server default off, nds overridden to on: only nds updates.
	if err := svc.SetCoreAutoUpdate(ctx, false); err != nil {
		t.Fatal(err)
	}
	if err := svc.SetSystemCoreAutoUpdate(ctx, "nds", hub.SystemAutoOn); err != nil {
		t.Fatal(err)
	}
	if err := svc.SetSystemCoreAutoUpdate(ctx, "nope", hub.SystemAutoOn); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown system: %v", err)
	}
	if err := svc.SetSystemCoreAutoUpdate(ctx, "nds", "sometimes"); err == nil {
		t.Fatal("invalid mode accepted")
	}
	e, _ := svc.GetRegistryEntry(ctx, "nds")
	if e.AutoUpdate != hub.SystemAutoOn || !e.Cores[0].AutoUpdateEffective {
		t.Fatalf("%+v", e)
	}
	newBuild(t, bb, "desmume", "2026-10-12")
	newBuild(t, bb, "azahar", "2026-10-12")
	rep, err := syncOnce(t, svc)
	if err != nil || !rep.AutoUpdateRan || len(rep.Updated) != 1 || rep.Updated[0].SystemID != "nds" {
		t.Fatalf("%+v %v", rep, err)
	}
	// A pinned core inside an enabled system stays; unpinning lets it follow again.
	newBuild(t, bb, "desmume", "2026-10-13")
	if err := svc.SetCoreAutoUpdatePin(ctx, "nds", "desmume", true); err != nil {
		t.Fatal(err)
	}
	e, _ = svc.GetRegistryEntry(ctx, "nds")
	if e.Cores[0].AutoUpdate != hub.CoreAutoOff || e.Cores[0].AutoUpdateEffective {
		t.Fatalf("%+v", e.Cores[0])
	}
	rep, err = syncOnce(t, svc)
	if err != nil || len(rep.Updated) != 0 {
		t.Fatalf("pinned core was updated: %+v %v", rep, err)
	}
	if err := svc.SetCoreAutoUpdatePin(ctx, "nds", "desmume", false); err != nil {
		t.Fatal(err)
	}
	if err := svc.SetCoreAutoUpdatePin(ctx, "nds", "missing", true); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown core: %v", err)
	}
	rep, _ = syncOnce(t, svc)
	if len(rep.Updated) != 1 || rep.Updated[0].CoreID != "desmume" {
		t.Fatalf("%+v", rep)
	}
	// System off beats the server default on.
	if err := svc.SetCoreAutoUpdate(ctx, true); err != nil {
		t.Fatal(err)
	}
	if err := svc.SetSystemCoreAutoUpdate(ctx, "3ds", hub.SystemAutoOff); err != nil {
		t.Fatal(err)
	}
	e, _ = svc.GetRegistryEntry(ctx, "3ds")
	if e.Cores[0].AutoUpdateEffective {
		t.Fatalf("3ds should be off: %+v", e.Cores[0])
	}
	// A manual update keeps the pin.
	if err := svc.SetCoreAutoUpdatePin(ctx, "nds", "desmume", true); err != nil {
		t.Fatal(err)
	}
	newBuild(t, bb, "desmume", "2026-10-14")
	refresh(t, svc)
	if _, err := svc.UpdateCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	e, _ = svc.GetRegistryEntry(ctx, "nds")
	if e.Cores[0].AutoUpdate != hub.CoreAutoOff {
		t.Fatalf("pin lost on manual update: %+v", e.Cores[0])
	}
}
