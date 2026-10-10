package httpapi

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func newBuildbotEnv(t *testing.T) (*sessEnv, *hubtest.Buildbot) {
	bb := hubtest.NewBuildbot(t)
	bb.AddCore(t, hubtest.BuildbotCore{ID: "desmume", SystemID: "nds", DisplayName: "DeSmuME", License: "GPLv2", Lib: bytes.Repeat([]byte("core!"), 1000)})
	bb.AddCore(t, hubtest.BuildbotCore{ID: "noods", SystemID: "nds", DisplayName: "NooDS", License: "GPLv3", RequiredHWAPI: "OpenGL Core >= 3.2",
		Date: "2026-10-08", Platforms: []string{"linux-x64"}})
	s := newSessEnv(t, func(o *hub.Options) { bb.Apply(o) })
	if _, err := s.svc.RefreshCatalog(context.Background()); err != nil {
		t.Fatal(err)
	}
	return s, bb
}

func TestCorePackageAPI(t *testing.T) {
	s, _ := newBuildbotEnv(t)
	ctx := context.Background()
	d := s.device(s.anna.ID, "Anna PC") // any trusted device
	libBytes := bytes.Repeat([]byte("core!"), 1000)
	pkg := "/api/v1/cores/desmume/packages/2026.10.09/linux-x64"
	wantStatus(t, s.do("GET", pkg, nil, opt{token: d.tok}), 404, "core_package_not_found") // nothing installed yet
	if _, err := s.svc.InstallCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	type pkgT struct {
		CoreID                     string `json:"core_id"`
		Version, Platform, License string
		Origin, SourceRef          string
		Files                      []struct {
			Name, Role, Sha256 string
			Size               int64
			Available          bool
		}
	}
	rec := s.do("GET", pkg, nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	p := decode[pkgT](t, rec)
	sum := sha256.Sum256(libBytes)
	if p.CoreID != "desmume" || p.Platform != "linux-x64" || p.License != "GPLv2" || len(p.Files) != 1 || p.Files[0].Name != "desmume_libretro.so" ||
		p.Files[0].Role != "library" || p.Files[0].Sha256 != hex.EncodeToString(sum[:]) || !p.Files[0].Available || p.Files[0].Size != int64(len(libBytes)) {
		t.Fatalf("%+v", p)
	}
	if !strings.Contains(rec.Body.String(), `"origin":"libretro-buildbot"`) {
		t.Fatal(rec.Body.String())
	}

	// File download: bytes, Content-Length, ETag, If-None-Match.
	fileURL := pkg + "/files/desmume_libretro.so"
	rec = s.do("GET", fileURL, nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	etag := `"` + hex.EncodeToString(sum[:]) + `"`
	if !bytes.Equal(rec.Body.Bytes(), libBytes) || rec.Header().Get("ETag") != etag || rec.Header().Get("Content-Length") != "5000" ||
		rec.Header().Get("Content-Type") != "application/octet-stream" {
		t.Fatalf("headers %v len %d", rec.Header(), rec.Body.Len())
	}
	for _, inm := range []string{etag, `W/` + etag, `"x", ` + etag, "*"} {
		rec = s.do("GET", fileURL, nil, opt{token: d.tok, header: map[string]string{"If-None-Match": inm}})
		if rec.Code != 304 || rec.Body.Len() != 0 || rec.Header().Get("ETag") != etag {
			t.Fatalf("If-None-Match %q: %d %v", inm, rec.Code, rec.Header())
		}
	}
	rec = s.do("GET", fileURL, nil, opt{token: d.tok, header: map[string]string{"If-None-Match": `"other"`}})
	wantStatus(t, rec, 200, "")

	// Errors. Core ids with a dash are valid ids.
	wantStatus(t, s.do("GET", "/api/v1/cores/nope/packages/2026.10.09/linux-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", "/api/v1/cores/a-b/packages/2026.10.09/linux-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", "/api/v1/cores/desmume/packages/9.9.9/linux-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", pkg+"/files/nothing.so", nil, opt{token: d.tok}), 404, "core_file_not_available")
	wantStatus(t, s.do("GET", pkg, nil, opt{}), 401, "unauthorized")
	wantStatus(t, s.do("GET", fileURL, nil, opt{}), 401, "unauthorized")
	wantStatus(t, s.do("GET", pkg+"/files/..%2Fx", nil, opt{token: d.tok, raw: true}), 404, "core_file_not_available")
}

func TestSystemsListsInstalledCoresAndDefault(t *testing.T) {
	s, _ := newBuildbotEnv(t)
	ctx := context.Background()
	d := s.device(s.anna.ID, "Anna PC")
	type sysList struct {
		Systems []struct {
			ID                  string  `json:"id"`
			PreferredCoreID     string  `json:"preferred_core_id"`
			ExpectedCoreVersion *string `json:"expected_core_version"`
			CorePackageVersion  *string `json:"core_package_version"`
			DefaultCoreID       *string `json:"default_core_id"`
			Cores               *[]struct {
				CoreID        string  `json:"core_id"`
				DisplayName   string  `json:"display_name"`
				Version       string  `json:"version"`
				License       string  `json:"license"`
				Experimental  bool    `json:"experimental"`
				RequiredHwApi *string `json:"required_hw_api"`
				Origin        string  `json:"origin"`
				BuildDate     *string `json:"build_date"`
			} `json:"cores"`
		}
	}
	get := func() sysList { return decode[sysList](t, s.do("GET", "/api/v1/systems", nil, opt{token: d.tok})) }

	// Nothing installed: an empty list, no default, no version.
	rec := s.do("GET", "/api/v1/systems", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	if b := rec.Body.String(); !strings.Contains(b, `"core_package_version":null`) || !strings.Contains(b, `"default_core_id":null`) || !strings.Contains(b, `"cores":[]`) {
		t.Fatal(b)
	}
	for _, id := range []string{"noods", "desmume"} {
		if _, err := s.svc.InstallCore(ctx, "nds", id); err != nil {
			t.Fatal(err)
		}
	}
	got := get().Systems[1] // systems are ordered by id: 3ds, nds
	if got.DefaultCoreID == nil || *got.DefaultCoreID != "noods" || got.PreferredCoreID != "noods" || got.ExpectedCoreVersion == nil || *got.ExpectedCoreVersion != "2026.10.08" ||
		got.CorePackageVersion == nil || *got.CorePackageVersion != "2026.10.08" || got.Cores == nil || len(*got.Cores) != 2 {
		t.Fatalf("%+v", got)
	}
	cs := *got.Cores
	if cs[0].CoreID != "desmume" || cs[0].Experimental || cs[0].Version != "2026.10.09" || cs[0].License != "GPLv2" || cs[0].Origin != "libretro-buildbot" ||
		cs[0].RequiredHwApi != nil || cs[0].BuildDate == nil || *cs[0].BuildDate != "2026-10-09" || cs[0].DisplayName != "DeSmuME" {
		t.Fatalf("%+v", cs[0])
	}
	if cs[1].CoreID != "noods" || !cs[1].Experimental || cs[1].RequiredHwApi == nil || *cs[1].RequiredHwApi != "OpenGL Core >= 3.2" {
		t.Fatalf("%+v", cs[1])
	}
	// Changing the default changes what old Players see.
	if err := s.svc.SetDefaultCore(ctx, "nds", "desmume"); err != nil {
		t.Fatal(err)
	}
	got = get().Systems[1] // systems are ordered by id: 3ds, nds
	if *got.DefaultCoreID != "desmume" || got.PreferredCoreID != "desmume" || *got.CorePackageVersion != "2026.10.09" {
		t.Fatalf("%+v", got)
	}
}
