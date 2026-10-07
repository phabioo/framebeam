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

func TestCorePackageAPI(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	libBytes := bytes.Repeat([]byte("core!"), 1000)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", libBytes)
	src.AddPackage(t, "melonds_ds", "1.5.0", "linux-x64", []byte("newer"))
	s := newSessEnv(t, func(o *hub.Options) { src.Apply(o) })
	ctx := context.Background()
	d := s.device(s.anna.ID, "Anna PC") // any trusted device
	pkg := "/api/v1/cores/melonds_ds/packages/1.4.0/linux-x64"

	// Sync the source; the system selects 1.4.0, so only that version is cached.
	if _, err := s.svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	// 1.5.0 is known but not selected, so not cached.
	rec := s.do("GET", "/api/v1/cores/melonds_ds/packages/1.5.0/linux-x64", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	type pkgT struct {
		CoreID                     string `json:"core_id"`
		Version, Platform, License string
		Files                      []struct {
			Name, Role, Sha256 string
			Size               int64
			Available          bool
		}
	}
	p := decode[pkgT](t, rec)
	if len(p.Files) != 2 || p.Files[0].Role != "library" || p.Files[0].Available || p.Files[1].Available {
		t.Fatalf("%+v", p)
	}
	wantStatus(t, s.do("GET", "/api/v1/cores/melonds_ds/packages/1.5.0/linux-x64/files/melonds_ds.so", nil, opt{token: d.tok}), 404, "core_file_not_available")

	// Cached package.
	rec = s.do("GET", pkg, nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	p = decode[pkgT](t, rec)
	sum := sha256.Sum256(libBytes)
	if p.CoreID != "melonds_ds" || p.Platform != "linux-x64" || p.Files[0].Name != "melonds_ds.so" || p.Files[0].Sha256 != hex.EncodeToString(sum[:]) ||
		!p.Files[0].Available || p.Files[0].Size != int64(len(libBytes)) {
		t.Fatalf("%+v", p)
	}

	// File download: bytes, Content-Length, ETag, If-None-Match.
	fileURL := pkg + "/files/melonds_ds.so"
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

	// Errors.
	wantStatus(t, s.do("GET", "/api/v1/cores/nope/packages/1.4.0/linux-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", "/api/v1/cores/melonds_ds/packages/9.9.9/linux-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", "/api/v1/cores/melonds_ds/packages/1.4.0/windows-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", pkg+"/files/nothing.so", nil, opt{token: d.tok}), 404, "core_file_not_available")
	wantStatus(t, s.do("GET", pkg, nil, opt{}), 401, "unauthorized")
	wantStatus(t, s.do("GET", fileURL, nil, opt{}), 401, "unauthorized")
	wantStatus(t, s.do("GET", pkg+"/files/..%2Fx", nil, opt{token: d.tok, raw: true}), 404, "core_file_not_available")

	// listSystems: core_package_version = expected version, else the highest fully cached one.
	type sysList struct {
		Systems []struct {
			CorePackageVersion *string `json:"core_package_version"`
		}
	}
	got := decode[sysList](t, s.do("GET", "/api/v1/systems", nil, opt{token: d.tok}))
	if got.Systems[0].CorePackageVersion == nil || *got.Systems[0].CorePackageVersion != "1.4.0" {
		t.Fatalf("%+v", got)
	}
	if err := s.svc.SetExpectedCoreVersion(ctx, "nds", ""); err != nil {
		t.Fatal(err)
	}
	got = decode[sysList](t, s.do("GET", "/api/v1/systems", nil, opt{token: d.tok}))
	if got.Systems[0].CorePackageVersion == nil || *got.Systems[0].CorePackageVersion != "1.4.0" {
		t.Fatalf("any version should serve the highest cached: %+v", got)
	}
}

func TestCorePackageVersionNullWithoutPackages(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) {})
	d := s.device(s.anna.ID, "Anna PC")
	if err := s.svc.SetExpectedCoreVersion(context.Background(), "nds", ""); err != nil {
		t.Fatal(err)
	}
	rec := s.do("GET", "/api/v1/systems", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	if !strings.Contains(rec.Body.String(), `"core_package_version":null`) {
		t.Fatal(rec.Body.String())
	}
}
