package hub_test

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"github.com/phabioo/framebeam/server/internal/auth"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/store"
)

func TestOpenRemovesLegacyCores(t *testing.T) {
	dir := t.TempDir()
	db, err := store.Open(filepath.Join(dir, "framebeam.db"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	exec := func(q string, a ...any) {
		t.Helper()
		if _, err := db.Exec(q, a...); err != nil {
			t.Fatal(err)
		}
	}
	exec(`INSERT INTO system_cores(system_id, core_id, version, installed_at, origin, display_name, license) VALUES
		('nds','melonds_ds','1.4.0',1,'framebeam','melonDS DS','GPL-3.0'),
		('nds','desmume','2026.10.09',1,'libretro-buildbot','DeSmuME','GPLv2')`)
	exec(`UPDATE systems SET default_core_id = 'melonds_ds' WHERE id = 'nds'`)
	exec(`INSERT INTO core_packages(core_id, version, platform, license, source_url, source_ref, origin, synced_at) VALUES
		('melonds_ds','1.4.0','linux-x86_64','GPL-3.0','','','framebeam',1),
		('desmume','2026.10.09','linux-x86_64','GPLv2','','','libretro-buildbot',1)`)
	legacyDir := filepath.Join(dir, "cores", "melonds_ds", "1.4.0", "linux-x86_64")
	keepDir := filepath.Join(dir, "cores", "desmume", "2026.10.09", "linux-x86_64")
	for _, d := range []string{legacyDir, keepDir} {
		if err := os.MkdirAll(d, 0o750); err != nil || os.WriteFile(filepath.Join(d, "lib.so"), []byte("x"), 0o640) != nil {
			t.Fatal(err)
		}
	}
	fast := auth.Params{Time: 1, MemoryKiB: 64, Threads: 1, KeyLen: 32, SaltLen: 16}
	for i := 0; i < 2; i++ { // the second start is a no-op
		if _, err := hub.Open(context.Background(), db, hub.Options{DataDir: dir, PasswordParams: &fast}); err != nil {
			t.Fatal(err)
		}
		var n int
		db.QueryRow(`SELECT COUNT(*) FROM system_cores WHERE origin = 'framebeam'`).Scan(&n)
		var p int
		db.QueryRow(`SELECT COUNT(*) FROM core_packages WHERE origin = 'framebeam'`).Scan(&p)
		if n != 0 || p != 0 {
			t.Fatalf("legacy rows left: %d %d", n, p)
		}
		if _, err := os.Stat(filepath.Join(dir, "cores", "melonds_ds")); !os.IsNotExist(err) {
			t.Fatalf("legacy package files left: %v", err)
		}
		if _, err := os.Stat(keepDir); err != nil {
			t.Fatal("buildbot package removed")
		}
		var def *string
		db.QueryRow(`SELECT default_core_id FROM systems WHERE id = 'nds'`).Scan(&def)
		if def == nil || *def != "desmume" {
			t.Fatalf("default %v", def)
		}
	}
	// Only legacy core: the default is cleared and the system has no core.
	exec(`DELETE FROM system_cores`)
	exec(`INSERT INTO system_cores(system_id, core_id, version, installed_at, origin, display_name, license)
		VALUES ('nds','melonds_ds','1.4.0',1,'framebeam','melonDS DS','GPL-3.0')`)
	exec(`UPDATE systems SET default_core_id = 'melonds_ds' WHERE id = 'nds'`)
	svc, err := hub.Open(context.Background(), db, hub.Options{DataDir: dir, PasswordParams: &fast})
	if err != nil {
		t.Fatal(err)
	}
	e, err := svc.GetRegistryEntry(context.Background(), "nds")
	if err != nil || e.CoreID != "" || len(e.Cores) != 0 {
		t.Fatalf("%v %+v", err, e)
	}
}

func TestCheckCoresAliasAndServedCores(t *testing.T) {
	e := hub.SystemEntry{ID: "nds", Name: "Nintendo DS", CoreID: "melonds_ds", ExpectedCoreVersion: "2026.10.09",
		Cores: []hub.InstalledCore{{CoreID: "melonds_ds", Version: "2026.10.09"}, {CoreID: "desmume", Version: "2026.10.01"}}}
	check := func(e hub.SystemEntry, rep ...hub.CoreReport) []hub.Problem {
		return hub.CheckCoresForTest([]hub.SystemEntry{e}, rep)
	}
	if p := check(e, hub.CoreReport{ID: "melondsds", Version: "2026.10.09"}); len(p) != 0 {
		t.Fatalf("alias (player canonical): %+v", p)
	}
	e2 := e
	e2.CoreID = "melondsds"
	if p := check(e2, hub.CoreReport{ID: "melonds_ds", Version: "2026.10.09"}); len(p) != 0 {
		t.Fatalf("alias (registry canonical): %+v", p)
	}
	// A different served core is fine, compared with its own version.
	if p := check(e, hub.CoreReport{ID: "desmume", Version: "2026.10.01"}); len(p) != 0 {
		t.Fatalf("served non-default: %+v", p)
	}
	if p := check(e, hub.CoreReport{ID: "desmume", Version: "old"}); len(p) != 1 || p[0].Code != hub.ProblemCoreVersionMismatch {
		t.Fatalf("%+v", p)
	}
	// A core the Hub does not serve for the system does not count.
	if p := check(e, hub.CoreReport{ID: "noods", Version: "1"}); len(p) != 1 || p[0].Code != hub.ProblemCoreMissing {
		t.Fatalf("%+v", p)
	}
	if p := check(e); len(p) != 1 || p[0].Code != hub.ProblemCoreMissing {
		t.Fatalf("%+v", p)
	}
	e3 := hub.SystemEntry{ID: "3ds", Name: "3DS"}
	if p := check(e3); len(p) != 0 {
		t.Fatalf("no core installed: %+v", p)
	}
}
