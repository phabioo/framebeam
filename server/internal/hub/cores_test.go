package hub_test

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

// bbEnv is a Hub on a fake buildbot with a few dummy cores.
func bbEnv(t *testing.T, mod func(*hub.Options)) (*hub.Service, *hubtest.Buildbot) {
	t.Helper()
	bb := hubtest.NewBuildbot(t)
	bb.AddCore(t, hubtest.BuildbotCore{ID: "desmume", SystemID: "nds", DisplayName: "Nintendo - DS (DeSmuME)", License: "GPLv2", Date: "2026-10-09"})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "noods", SystemID: "nds", DisplayName: "Nintendo - DS (NooDS)", License: "GPLv3",
		RequiredHWAPI: "OpenGL Core >= 3.2", Date: "2026-10-08", Platforms: []string{"linux-x64"}})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "azahar", SystemID: "3ds", DisplayName: "Nintendo - 3DS (Azahar)", Date: "2026-10-09"})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "nc-core", SystemID: "nds", DisplayName: "NC", License: "Non-commercial", Date: "2026-10-01"})
	svc, _ := hubtest.New(t, func(o *hub.Options) {
		bb.Apply(o)
		if mod != nil {
			mod(o)
		}
	})
	return svc, bb
}

func refresh(t *testing.T, svc *hub.Service) {
	t.Helper()
	if _, err := svc.RefreshCatalog(context.Background()); err != nil {
		t.Fatal(err)
	}
}

func installedIDs(t *testing.T, svc *hub.Service) (def string, ids []string) {
	t.Helper()
	e, err := svc.GetRegistryEntry(ctx, "nds")
	if err != nil {
		t.Fatal(err)
	}
	for _, c := range e.Cores {
		ids = append(ids, c.CoreID)
	}
	return e.CoreID, ids
}

func cacheFiles(t *testing.T, svc *hub.Service) []string {
	t.Helper()
	var out []string
	filepath.Walk(filepath.Join(svc.DataDir(), "cores"), func(p string, fi os.FileInfo, err error) error {
		if err == nil && !fi.IsDir() {
			out = append(out, p)
		}
		return nil
	})
	return out
}

func TestCatalogRefreshAndAvailableCores(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	if st, _ := svc.CoreSource(ctx); st.LastCheck != nil || st.Cores != 0 {
		t.Fatalf("%+v", st)
	}
	if sc, _ := svc.SystemCores(ctx, "nds"); len(sc.Available) != 0 {
		t.Fatalf("catalog before the first refresh: %+v", sc.Available)
	}
	rep, err := svc.RefreshCatalog(ctx)
	if err != nil || rep.Cores != 4 {
		t.Fatalf("%+v %v", rep, err)
	}
	st, _ := svc.CoreSource(ctx)
	if st.LastCheck == nil || st.LastSuccess == nil || st.LastError != "" || st.Cores != 4 || !strings.HasSuffix(st.BuildbotURL, "/nightly") {
		t.Fatalf("%+v", st)
	}
	sc, err := svc.SystemCores(ctx, "nds")
	if err != nil || len(sc.Installed) != 0 {
		t.Fatalf("%+v %v", sc, err)
	}
	// 3ds cores are not offered for nds; profiled cores come first.
	var ids []string
	for _, a := range sc.Available {
		ids = append(ids, a.CoreID)
	}
	if strings.Join(ids, ",") != "desmume,nc-core,noods" {
		t.Fatalf("available %v", ids)
	}
	d := sc.Available[0]
	if d.DisplayName != "Nintendo - DS (DeSmuME)" || d.License != "GPLv2" || d.Experimental || d.BuildDate != "2026-10-09" ||
		strings.Join(d.Platforms, ",") != "windows-x64,linux-x64" || strings.Join(d.Extensions, ",") != "nds,bin" ||
		len(d.Notes) != 2 || d.Notes[0] != "BIOS for desmume" || d.RequiredHWAPI != "" {
		t.Fatalf("%+v", d)
	}
	if nc := sc.Available[1]; !nc.NonCommercial || !nc.Experimental {
		t.Fatalf("%+v", nc)
	}
	if n := sc.Available[2]; !n.Experimental || n.RequiredHWAPI != "OpenGL Core >= 3.2" || strings.Join(n.Platforms, ",") != "linux-x64" || n.BuildDate != "2026-10-08" {
		t.Fatalf("%+v", n)
	}
	// The catalog is cached on disk: a new service on the same data dir knows it without a refresh.
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "core-catalog.json")); err != nil {
		t.Fatal(err)
	}
	// A failing source keeps the last catalog and records the error.
	bb.Down = true
	if _, err := svc.RefreshCatalog(ctx); err == nil {
		t.Fatal("refresh succeeded against a down source")
	}
	st, _ = svc.CoreSource(ctx)
	if !strings.Contains(st.LastError, "503") || st.Cores != 4 {
		t.Fatalf("%+v", st)
	}
	if sc, _ := svc.SystemCores(ctx, "nds"); len(sc.Available) != 3 {
		t.Fatal("catalog lost after a failed refresh")
	}
	bb.Down = false
	refresh(t, svc)
	if st, _ = svc.CoreSource(ctx); st.LastError != "" {
		t.Fatalf("%+v", st)
	}
}

func TestInstallCoreHappyPath(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	refresh(t, svc)
	ic, err := svc.InstallCore(ctx, "nds", "desmume")
	if err != nil {
		t.Fatal(err)
	}
	if ic.Version != "2026.10.09" || ic.BuildDate != "2026-10-09" || ic.Origin != "libretro-buildbot" || ic.License != "GPLv2" || !ic.Default ||
		ic.Experimental || strings.Join(ic.Platforms, ",") != "linux-x64,windows-x64" || ic.UpdateAvailable {
		t.Fatalf("%+v", ic)
	}
	// Both platforms are stored, exactly one library each, with the SHA-256 of the extracted file.
	for plat, name := range map[string]string{"linux-x64": "desmume_libretro.so", "windows-x64": "desmume_libretro.dll"} {
		p, err := svc.GetCorePackage(ctx, "desmume", "2026.10.09", plat)
		if err != nil {
			t.Fatal(plat, err)
		}
		lib := bytes.Repeat([]byte("desmume!"), 40)
		sum := sha256.Sum256(lib)
		if len(p.Files) != 1 || p.Files[0].Name != name || p.Files[0].Role != "library" || !p.Files[0].Available || p.Files[0].SHA256 != hex.EncodeToString(sum[:]) ||
			p.Files[0].Size != int64(len(lib)) || p.License != "GPLv2" || p.Origin != "libretro-buildbot" {
			t.Fatalf("%s: %+v", plat, p)
		}
		if p.SourceURL != bb.ZipURL(plat, "desmume") || !strings.HasPrefix(p.SourceRef, "2026-10-09 ") || len(p.SourceRef) != len("2026-10-09 ")+8 {
			t.Fatalf("%s: source %q %q", plat, p.SourceURL, p.SourceRef)
		}
		f, def, err := svc.OpenCoreFile(ctx, "desmume", "2026.10.09", plat, name)
		if err != nil {
			t.Fatal(err)
		}
		got := make([]byte, len(lib)+1)
		n, _ := f.Read(got)
		f.Close()
		if !bytes.Equal(got[:n], lib) || def.SHA256 != p.Files[0].SHA256 {
			t.Fatalf("%s: served file differs", plat)
		}
	}
	// The registry shows the default core and its served version; nothing is left in the staging directory.
	e, _ := svc.GetRegistryEntry(ctx, "nds")
	if e.CoreID != "desmume" || e.CoreName != "Nintendo - DS (DeSmuME)" || e.ExpectedCoreVersion != "2026.10.09" || len(e.Cores) != 1 {
		t.Fatalf("%+v", e)
	}
	if left, _ := os.ReadDir(filepath.Join(svc.DataDir(), "tmp")); len(left) != 0 {
		t.Fatalf("staging left behind: %v", left)
	}
	// Errors: twice, wrong system, unknown core, bad id.
	if _, err := svc.InstallCore(ctx, "nds", "desmume"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
	if _, err := svc.InstallCore(ctx, "nds", "azahar"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal("3ds core installed for nds:", err)
	}
	if _, err := svc.InstallCore(ctx, "nds", "nothing"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
	if _, err := svc.InstallCore(ctx, "nds", "../x"); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
	if _, err := svc.InstallCore(ctx, "snes", "desmume"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	// A core that has a build for one platform only is stored for that platform.
	if _, err := svc.InstallCore(ctx, "nds", "noods"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetCorePackage(ctx, "noods", "2026.10.08", "windows-x64"); !errors.Is(err, hub.ErrCorePackageNotFound) {
		t.Fatal(err)
	}
	if def, ids := installedIDs(t, svc); def != "desmume" || strings.Join(ids, ",") != "desmume,noods" {
		t.Fatalf("%s %v", def, ids)
	}
}

func TestInstallCoreRejectsBadDownloadsAndLeavesNothing(t *testing.T) {
	dummy := bytes.Repeat([]byte{7}, 100)
	cases := map[string]struct {
		zip  map[string][]byte
		want string
	}{
		"traversal":     {map[string][]byte{"../desmume_libretro.so": dummy}, "unsafe path"},
		"subdir":        {map[string][]byte{"cores/desmume_libretro.so": dummy}, "unsafe path"},
		"two libraries": {map[string][]byte{"desmume_libretro.so": dummy, "other_libretro.so": dummy}, "more than one library"},
		"no library":    {map[string][]byte{"readme.txt": dummy}, "no library"},
		"wrong name":    {map[string][]byte{"other_libretro.so": dummy}, "expected"},
		"oversize":      {map[string][]byte{"desmume_libretro.so": bytes.Repeat([]byte{1}, 5000)}, "larger than the limit"},
	}
	for name, c := range cases {
		t.Run(name, func(t *testing.T) {
			svc, bb := bbEnv(t, func(o *hub.Options) { o.CoreMaxLibraryBytes = 1000 })
			bb.SetZip("linux-x64", "desmume", "2026-10-09", hubtest.MakeZip(t, c.zip))
			refresh(t, svc)
			_, err := svc.InstallCore(ctx, "nds", "desmume")
			if !errors.Is(err, hub.ErrBadRequest) || !strings.Contains(err.Error(), c.want) {
				t.Fatalf("err %v, want %q", err, c.want)
			}
			if def, ids := installedIDs(t, svc); def != "" || len(ids) != 0 {
				t.Fatalf("installed %s %v", def, ids)
			}
			if files := cacheFiles(t, svc); len(files) != 0 {
				t.Fatalf("cache files left: %v", files)
			}
			if left, _ := os.ReadDir(filepath.Join(svc.DataDir(), "tmp")); len(left) != 0 {
				t.Fatalf("staging left behind: %v", left)
			}
		})
	}
}

func TestInstallCoreCRCMismatchAndHTTPStatusAndZipCap(t *testing.T) {
	svc, bb := bbEnv(t, func(o *hub.Options) { o.CoreMaxZipBytes = 1 << 20 })
	bb.SetCRC("windows-x64", "desmume", "deadbeef")
	refresh(t, svc)
	if _, err := svc.InstallCore(ctx, "nds", "desmume"); err == nil || !strings.Contains(err.Error(), "CRC32 mismatch") {
		t.Fatalf("err %v", err)
	}
	if _, ids := installedIDs(t, svc); len(ids) != 0 || len(cacheFiles(t, svc)) != 0 {
		t.Fatalf("%v %v", ids, cacheFiles(t, svc))
	}
	bb.SetCRC("windows-x64", "desmume", "00000000")
	// Oversize zip: larger than the cap.
	big := bytes.Repeat([]byte{9}, 2<<20) // not even a zip: the size cap applies before anything is read
	bb.SetZip("linux-x64", "desmume", "2026-10-09", big)
	bb.SetZip("windows-x64", "desmume", "2026-10-09", big)
	refresh(t, svc)
	if _, err := svc.InstallCore(ctx, "nds", "desmume"); err == nil || !strings.Contains(err.Error(), "larger than the limit") {
		t.Fatalf("err %v", err)
	}
	bb.Down = true // the catalog stays; downloads fail with HTTP 503
	if _, err := svc.InstallCore(ctx, "nds", "noods"); err == nil || !strings.Contains(err.Error(), "HTTP 503") {
		t.Fatalf("err %v", err)
	}
	if _, ids := installedIDs(t, svc); len(ids) != 0 {
		t.Fatal(ids)
	}
}

func TestUpdateCoreKeepsOldBuildUntilNewIsCached(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	refresh(t, svc)
	if _, err := svc.InstallCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.UpdateCore(ctx, "nds", "desmume"); !errors.Is(err, hub.ErrBadRequest) || !strings.Contains(err.Error(), "up to date") {
		t.Fatalf("update without a newer build: %v", err)
	}
	if _, err := svc.UpdateCore(ctx, "nds", "noods"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	// A newer upstream build appears; nothing changes until the admin updates.
	newLib := bytes.Repeat([]byte("newer"), 50)
	for _, p := range []string{"linux-x64", "windows-x64"} {
		suffix := map[string]string{"linux-x64": ".so", "windows-x64": ".dll"}[p]
		bb.SetZip(p, "desmume", "2026-10-12", hubtest.MakeZip(t, map[string][]byte{"desmume_libretro" + suffix: newLib}))
	}
	refresh(t, svc)
	sc, _ := svc.SystemCores(ctx, "nds")
	if len(sc.Installed) != 1 || !sc.Installed[0].UpdateAvailable || sc.Installed[0].UpdateDate != "2026-10-12" || sc.Installed[0].Version != "2026.10.09" {
		t.Fatalf("%+v", sc.Installed)
	}
	// The new build fails to download (bad CRC): the old build stays installed, served and cached.
	bb.SetCRC("windows-x64", "desmume", "deadbeef")
	refresh(t, svc)
	if _, err := svc.UpdateCore(ctx, "nds", "desmume"); err == nil {
		t.Fatal("update with a bad CRC succeeded")
	}
	e, _ := svc.GetRegistryEntry(ctx, "nds")
	if e.ExpectedCoreVersion != "2026.10.09" {
		t.Fatalf("%+v", e)
	}
	if _, f, err := svc.OpenCoreFile(ctx, "desmume", "2026.10.09", "linux-x64", "desmume_libretro.so"); err != nil {
		t.Fatal(err)
	} else {
		_ = f
	}
	// Fixed upstream: the update installs the new version and drops the old one.
	bb.SetZip("windows-x64", "desmume", "2026-10-12", hubtest.MakeZip(t, map[string][]byte{"desmume_libretro.dll": newLib}))
	refresh(t, svc)
	ic, err := svc.UpdateCore(ctx, "nds", "desmume")
	if err != nil {
		t.Fatal(err)
	}
	if ic.Version != "2026.10.12" || ic.BuildDate != "2026-10-12" || ic.UpdateAvailable || !ic.Default {
		t.Fatalf("%+v", ic)
	}
	if _, err := svc.GetCorePackage(ctx, "desmume", "2026.10.09", "linux-x64"); !errors.Is(err, hub.ErrCorePackageNotFound) {
		t.Fatal("old version still known:", err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "cores", "desmume", "2026.10.09")); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("old cache files remain:", err)
	}
	if e, _ = svc.GetRegistryEntry(ctx, "nds"); e.ExpectedCoreVersion != "2026.10.12" {
		t.Fatalf("%+v", e)
	}
	if len(cacheFiles(t, svc)) != 2 {
		t.Fatalf("cache: %v", cacheFiles(t, svc))
	}
}

func TestRemoveCoreReassignsDefault(t *testing.T) {
	svc, _ := bbEnv(t, nil)
	refresh(t, svc)
	for _, id := range []string{"noods", "desmume"} { // experimental first
		if _, err := svc.InstallCore(ctx, "nds", id); err != nil {
			t.Fatal(err)
		}
	}
	// The first installed core became the default; the admin can change it.
	if def, _ := installedIDs(t, svc); def != "noods" {
		t.Fatal(def)
	}
	if err := svc.SetDefaultCore(ctx, "nds", "nc-core"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	if err := svc.SetDefaultCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.InstallCore(ctx, "nds", "nc-core"); err != nil {
		t.Fatal(err)
	}
	// Removing a non-default core keeps the default.
	if err := svc.RemoveCore(ctx, "nds", "nc-core"); err != nil {
		t.Fatal(err)
	}
	if def, ids := installedIDs(t, svc); def != "desmume" || strings.Join(ids, ",") != "desmume,noods" {
		t.Fatalf("%s %v", def, ids)
	}
	// Removing the default reassigns it, and the packages and cache files are gone.
	if err := svc.RemoveCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	if def, ids := installedIDs(t, svc); def != "noods" || len(ids) != 1 {
		t.Fatalf("%s %v", def, ids)
	}
	if _, err := svc.GetCorePackage(ctx, "desmume", "2026.10.09", "linux-x64"); !errors.Is(err, hub.ErrCorePackageNotFound) {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(svc.DataDir(), "cores", "desmume")); !errors.Is(err, os.ErrNotExist) {
		t.Fatal(err)
	}
	if e, _ := svc.GetRegistryEntry(ctx, "nds"); e.ExpectedCoreVersion != "2026.10.08" {
		t.Fatalf("%+v", e)
	}
	if err := svc.RemoveCore(ctx, "nds", "noods"); err != nil {
		t.Fatal(err)
	}
	if def, ids := installedIDs(t, svc); def != "" || len(ids) != 0 || len(cacheFiles(t, svc)) != 0 {
		t.Fatalf("%q %v %v", def, ids, cacheFiles(t, svc))
	}
	if err := svc.RemoveCore(ctx, "nds", "noods"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatal(err)
	}
	// A profiled core is preferred as the new default.
	for _, id := range []string{"noods", "desmume", "nc-core"} {
		if _, err := svc.InstallCore(ctx, "nds", id); err != nil {
			t.Fatal(err)
		}
	}
	if err := svc.RemoveCore(ctx, "nds", "noods"); err != nil {
		t.Fatal(err)
	}
	if def, _ := installedIDs(t, svc); def != "desmume" {
		t.Fatal(def)
	}
}

func TestImportCores(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	dir := t.TempDir()
	bb.WriteImportDir(t, dir)
	// No catalog refresh: the info comes from <dir>/info.zip.
	sum, err := svc.ImportCores(ctx, dir)
	if err != nil {
		t.Fatal(err)
	}
	// azahar is a 3ds core: no supported system.
	if sum.Installed != 3 || sum.Updated != 0 || sum.Unchanged != 0 || len(sum.Problems) != 1 || !strings.Contains(sum.Problems[0], "azahar") {
		t.Fatalf("%+v", sum)
	}
	if def, ids := installedIDs(t, svc); def != "desmume" || strings.Join(ids, ",") != "desmume,noods,nc-core" && strings.Join(ids, ",") != "desmume,nc-core,noods" {
		t.Fatalf("%s %v", def, ids)
	}
	p, err := svc.GetCorePackage(ctx, "desmume", "2026.10.09", "windows-x64")
	if err != nil || p.Origin != "libretro-buildbot" || !strings.HasPrefix(p.SourceRef, "2026-10-09 ") || !p.Files[0].Available {
		t.Fatalf("%+v %v", p, err)
	}
	// Importing the same zips again changes nothing but the unchanged count.
	sum, err = svc.ImportCores(ctx, dir)
	if err != nil || sum.Unchanged != 3 || sum.Installed != 0 || sum.Updated != 0 {
		t.Fatalf("%+v %v", sum, err)
	}
	if e, _ := svc.GetRegistryEntry(ctx, "nds"); e.ExpectedCoreVersion != "2026.10.09" {
		t.Fatalf("%+v", e)
	}
}

func TestImportCoresSameDayNewBuildGetsSuffix(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	dir := t.TempDir()
	bb.WriteImportDir(t, dir)
	if _, err := svc.ImportCores(ctx, dir); err != nil {
		t.Fatal(err)
	}
	// A different build of the same day (other content, CRC32 not listed this time).
	for _, p := range []string{"linux-x64", "windows-x64"} {
		suffix := map[string]string{"linux-x64": ".so", "windows-x64": ".dll"}[p]
		z := hubtest.MakeZip(t, map[string][]byte{"desmume_libretro" + suffix: []byte("second build of the day")})
		if err := os.WriteFile(filepath.Join(dir, p, "desmume_libretro"+suffix+".zip"), z, 0o644); err != nil {
			t.Fatal(err)
		}
		idx := filepath.Join(dir, p, ".index-extended")
		b, _ := os.ReadFile(idx)
		var keep []string
		for _, l := range strings.Split(string(b), "\n") {
			if !strings.Contains(l, "desmume_") {
				keep = append(keep, l)
			}
		}
		keep = append(keep, "2026-10-09 "+"00000000 desmume_libretro"+suffix+".zip") // wrong CRC32 is rejected below
		os.WriteFile(idx, []byte(strings.Join(keep, "\n")), 0o644)
	}
	sum, err := svc.ImportCores(ctx, dir)
	if err == nil && len(sum.Problems) == 0 {
		t.Fatalf("a wrong CRC32 was accepted: %+v", sum)
	}
	if !strings.Contains(strings.Join(sum.Problems, ";"), "CRC32 mismatch") {
		t.Fatalf("%+v", sum)
	}
	if e, _ := svc.GetRegistryEntry(ctx, "nds"); e.ExpectedCoreVersion != "2026.10.09" {
		t.Fatalf("a rejected import changed the installed build: %+v", e)
	}
	// Without the .index-extended the file date is the build date, no CRC32 check: same day => version suffix.
	for _, p := range []string{"linux-x64", "windows-x64"} {
		os.Remove(filepath.Join(dir, p, ".index-extended"))
		suffix := map[string]string{"linux-x64": ".so", "windows-x64": ".dll"}[p]
		day := time.Date(2026, 10, 9, 12, 0, 0, 0, time.UTC)
		os.Chtimes(filepath.Join(dir, p, "desmume_libretro"+suffix+".zip"), day, day)
	}
	sum, err = svc.ImportCores(ctx, dir)
	if err != nil || sum.Updated < 1 {
		t.Fatalf("%+v %v", sum, err)
	}
	e, _ := svc.GetRegistryEntry(ctx, "nds")
	if e.ExpectedCoreVersion != "2026.10.09.2" {
		t.Fatalf("%+v", e)
	}
	if _, err := svc.GetCorePackage(ctx, "desmume", "2026.10.09", "linux-x64"); !errors.Is(err, hub.ErrCorePackageNotFound) {
		t.Fatal("the replaced build is still known")
	}
}
