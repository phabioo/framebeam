package hub_test

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestGameBoyCoresAreFilteredByExtension(t *testing.T) {
	bb := hubtest.NewBuildbot(t)
	bb.AddCore(t, hubtest.BuildbotCore{ID: "mgba", SystemID: "game_boy_advance", DisplayName: "Nintendo - GBA (mGBA)", License: "MPL-2.0", Extensions: "gba|gb|gbc|sgb"})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "gambatte", SystemID: "game_boy", DisplayName: "Nintendo - GB/GBC (Gambatte)", License: "GPLv2", Extensions: "gb|gbc|dmg"})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "gpsp", SystemID: "game_boy_advance", DisplayName: "Nintendo - GBA (gpSP)", License: "GPLv2", Extensions: "gba|bin"})
	svc, _ := hubtest.New(t, func(o *hub.Options) { bb.Apply(o) })
	refresh(t, svc)
	offered := func(system string) string {
		t.Helper()
		sc, err := svc.SystemCores(ctx, system)
		if err != nil {
			t.Fatal(err)
		}
		var ids []string
		for _, a := range sc.Available {
			ids = append(ids, a.CoreID)
		}
		return strings.Join(ids, ",")
	}
	for system, want := range map[string]string{"gb": "gambatte,mgba", "gbc": "gambatte,mgba", "gba": "gpsp,mgba", "nds": ""} {
		if got := offered(system); got != want && !(want != "" && sameSet(got, want)) {
			t.Errorf("%s: offered %q, want %q", system, got, want)
		}
	}
	// Installing a non-matching core is rejected; a matching one works.
	if _, err := svc.InstallCore(ctx, "gba", "gambatte"); err == nil {
		t.Error("gambatte installed for gba")
	}
	if _, err := svc.InstallCore(ctx, "gb", "gpsp"); err == nil {
		t.Error("gpsp installed for gb")
	}
	if _, err := svc.InstallCore(ctx, "gba", "mgba"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.InstallCore(ctx, "gb", "mgba"); err != nil {
		t.Fatal(err)
	}
	// Installed cores stay, even if the catalog entry is no longer offered.
	e, err := svc.GetRegistryEntry(ctx, "gba")
	if err != nil || len(e.Cores) != 1 || e.Cores[0].Experimental {
		t.Fatalf("%v %+v", err, e.Cores)
	}
}

func sameSet(a, b string) bool {
	m := map[string]bool{}
	for _, x := range strings.Split(a, ",") {
		m[x] = true
	}
	for _, x := range strings.Split(b, ",") {
		if !m[x] {
			return false
		}
		delete(m, x)
	}
	return len(m) == 0
}

func TestImportCoresGoesToEveryMatchingSystem(t *testing.T) {
	bb := hubtest.NewBuildbot(t)
	bb.AddCore(t, hubtest.BuildbotCore{ID: "mgba", SystemID: "game_boy_advance", DisplayName: "mGBA", License: "MPL-2.0", Extensions: "gba|gb|gbc"})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "gambatte", SystemID: "game_boy", DisplayName: "Gambatte", License: "GPLv2", Extensions: "gb|gbc"})
	svc, _ := hubtest.New(t, func(o *hub.Options) { bb.Apply(o) })
	dir := t.TempDir()
	bb.WriteImportDir(t, dir)
	sum, err := svc.ImportCores(ctx, dir)
	if err != nil || sum.Installed != 5 || sum.Updated != 0 || sum.Unchanged != 0 || len(sum.Problems) != 0 {
		t.Fatalf("%+v %v", sum, err)
	}
	for system, want := range map[string]string{"gb": "gambatte,mgba", "gbc": "gambatte,mgba", "gba": "mgba"} {
		e, err := svc.GetRegistryEntry(ctx, system)
		if err != nil {
			t.Fatal(err)
		}
		var ids []string
		for _, c := range e.Cores {
			ids = append(ids, c.CoreID)
		}
		if strings.Join(ids, ",") != want || e.CoreID == "" {
			t.Errorf("%s: cores %v default %q, want %s", system, ids, e.CoreID, want)
		}
	}
	// The package is cached once: all systems use the same version.
	a, _ := svc.GetRegistryEntry(ctx, "gb")
	b, _ := svc.GetRegistryEntry(ctx, "gba")
	var va, vb string
	for _, c := range a.Cores {
		if c.CoreID == "mgba" {
			va = c.Version
		}
	}
	for _, c := range b.Cores {
		vb = c.Version
	}
	if va == "" || va != vb {
		t.Errorf("versions %q %q", va, vb)
	}
	// Importing again changes nothing.
	sum, err = svc.ImportCores(ctx, dir)
	if err != nil || sum.Installed != 0 || sum.Updated != 0 || sum.Unchanged != 5 {
		t.Fatalf("%+v %v", sum, err)
	}
}

func TestSharedCoreUpdateAndRemove(t *testing.T) {
	bb := hubtest.NewBuildbot(t)
	bb.AddCore(t, hubtest.BuildbotCore{ID: "mgba", SystemID: "game_boy_advance", DisplayName: "mGBA", License: "MPL-2.0", Extensions: "gba|gb|gbc", Date: "2026-10-01"})
	svc, _ := hubtest.New(t, func(o *hub.Options) { bb.Apply(o) })
	refresh(t, svc)
	hits := func() int { return bb.ZipHits("linux-x64", "mgba") }
	version := func(system string) string {
		t.Helper()
		e, err := svc.GetRegistryEntry(ctx, system)
		if err != nil {
			t.Fatal(err)
		}
		for _, c := range e.Cores {
			if c.CoreID == "mgba" {
				return c.Version
			}
		}
		return ""
	}
	for _, sys := range []string{"gb", "gbc", "gba"} {
		if _, err := svc.InstallCore(ctx, sys, "mgba"); err != nil {
			t.Fatal(sys, err)
		}
	}
	v1 := version("gb")
	if v1 == "" || version("gbc") != v1 || version("gba") != v1 {
		t.Fatalf("install reuses one build: %q %q %q", v1, version("gbc"), version("gba"))
	}
	if n := hits(); n != 1 {
		t.Fatalf("downloads: %d", n)
	}
	// A newer build: the automatic update (via one system) moves all three and downloads once.
	bb.AddCore(t, hubtest.BuildbotCore{ID: "mgba", SystemID: "game_boy_advance", DisplayName: "mGBA", License: "MPL-2.0", Extensions: "gba|gb|gbc", Date: "2026-10-05"})
	refresh(t, svc)
	if _, err := svc.UpdateCore(ctx, "gbc", "mgba"); err != nil {
		t.Fatal(err)
	}
	v2 := version("gba")
	if v2 == v1 || version("gb") != v2 || version("gbc") != v2 {
		t.Fatalf("update moves all: %q %q %q (was %s)", version("gb"), version("gbc"), version("gba"), v1)
	}
	if n := hits(); n != 2 {
		t.Fatalf("downloads after update: %d", n)
	}
	if _, err := svc.GetCorePackage(ctx, "mgba", v1, "linux-x64"); err == nil {
		t.Error("old version still cached")
	}
	// Removing from one system keeps the files for the others; removing the last drops the package.
	if err := svc.RemoveCore(ctx, "gb", "mgba"); err != nil {
		t.Fatal(err)
	}
	p, err := svc.GetCorePackage(ctx, "mgba", v2, "linux-x64")
	if err != nil || len(p.Files) == 0 || !p.Files[0].Available {
		t.Fatalf("%+v %v", p, err)
	}
	if err := svc.RemoveCore(ctx, "gbc", "mgba"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetCorePackage(ctx, "mgba", v2, "linux-x64"); err != nil {
		t.Fatal("gba still uses it:", err)
	}
	if err := svc.RemoveCore(ctx, "gba", "mgba"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetCorePackage(ctx, "mgba", v2, "linux-x64"); err == nil {
		t.Error("package kept after the last system removed it")
	}
}

func TestAutoUpdateOfSharedCoreDownloadsOnce(t *testing.T) {
	bb := hubtest.NewBuildbot(t)
	bb.AddCore(t, hubtest.BuildbotCore{ID: "mgba", SystemID: "game_boy_advance", DisplayName: "mGBA", License: "MPL-2.0", Extensions: "gba|gb|gbc", Date: "2026-10-01"})
	svc, _ := hubtest.New(t, func(o *hub.Options) { bb.Apply(o) })
	refresh(t, svc)
	for _, sys := range []string{"gb", "gbc", "gba"} {
		if _, err := svc.InstallCore(ctx, sys, "mgba"); err != nil {
			t.Fatal(sys, err)
		}
	}
	bb.AddCore(t, hubtest.BuildbotCore{ID: "mgba", SystemID: "game_boy_advance", DisplayName: "mGBA", License: "MPL-2.0", Extensions: "gba|gb|gbc", Date: "2026-10-05"})
	before := bb.ZipHits("linux-x64", "mgba")
	cctx, cancel := context.WithCancel(ctx)
	done := make(chan hub.CoreSyncReport, 1)
	go svc.RunCoreSync(cctx, time.Hour, func(r hub.CoreSyncReport, _ error) {
		select {
		case done <- r:
		default:
		}
	})
	rep := <-done
	cancel()
	if !rep.AutoUpdateRan || len(rep.UpdateErrors) != 0 {
		t.Fatalf("%+v", rep)
	}
	if n := bb.ZipHits("linux-x64", "mgba") - before; n != 1 {
		t.Fatalf("downloads: %d", n)
	}
	for _, sys := range []string{"gb", "gbc", "gba"} {
		e, _ := svc.GetRegistryEntry(ctx, sys)
		if len(e.Cores) != 1 || e.Cores[0].Version != "2026.10.05" || e.Cores[0].UpdateAvailable {
			t.Errorf("%s: %+v", sys, e.Cores)
		}
	}
}
