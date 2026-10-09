package httpapi

import (
	"context"
	"strings"
	"time"

	openapi_types "github.com/oapi-codegen/runtime/types"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
)

// ListSystems returns the registry with firmware status (no firmware bytes).
func (s *Server) ListSystems(ctx context.Context, _ api.ListSystemsRequestObject) (api.ListSystemsResponseObject, error) {
	reg, err := s.svc.ListRegistry(ctx)
	if err != nil {
		return nil, err
	}
	out := api.ListSystems200JSONResponse{Systems: make([]api.SystemInfo, 0, len(reg))}
	for _, e := range reg {
		cores := make([]api.SystemCore, 0, len(e.Cores))
		for _, c := range e.Cores {
			sc := api.SystemCore{CoreId: c.CoreID, DisplayName: c.DisplayName, Version: c.Version, License: c.License,
				Experimental: c.Experimental, Origin: coreOrigin(c.Origin)}
			if c.RequiredHWAPI != "" {
				v := c.RequiredHWAPI
				sc.RequiredHwApi = &v
			}
			if t, err := time.Parse("2006-01-02", c.BuildDate); err == nil {
				sc.BuildDate = &openapi_types.Date{Time: t}
			}
			cores = append(cores, sc)
		}
		// preferred_core_id, expected_core_version and core_package_version describe the default core (old Players).
		si := api.SystemInfo{Id: e.ID, DisplayName: e.Name, PreferredCoreId: e.CoreID, FirmwareMode: api.FirmwareMode(e.FirmwareMode),
			Firmware: make([]api.FirmwareFile, 0, len(e.Firmware)), Cores: &cores}
		if e.CoreID != "" {
			id := e.CoreID
			si.DefaultCoreId = &id
		}
		if e.ExpectedCoreVersion != "" {
			v := e.ExpectedCoreVersion
			si.ExpectedCoreVersion = &v
			si.CorePackageVersion = &v
		}
		for _, f := range e.Firmware {
			af := api.FirmwareFile{Id: f.ID, DisplayName: f.DisplayName, Required: f.Required}
			// A file with a hash mismatch (pinned hash differs) counts as not present for Players.
			if f.Present && f.State != hub.FirmwareMismatch {
				af.Present = true
				sz, sha := f.Size, f.SHA256
				af.Size, af.Sha256 = &sz, &sha
			}
			si.Firmware = append(si.Firmware, af)
		}
		out.Systems = append(out.Systems, si)
	}
	return out, nil
}

// GetCorePackage returns the manifest of a core package known to the Hub.
func (s *Server) GetCorePackage(ctx context.Context, req api.GetCorePackageRequestObject) (api.GetCorePackageResponseObject, error) {
	p, err := s.svc.GetCorePackage(ctx, req.CoreId, req.Version, string(req.Platform))
	if err != nil {
		return nil, err
	}
	out := api.GetCorePackage200JSONResponse{CoreId: p.CoreID, Version: p.Version, Platform: api.CorePlatform(p.Platform),
		License: p.License, SourceUrl: p.SourceURL, SourceRef: p.SourceRef, Origin: ptr(coreOrigin(p.Origin)), Files: make([]api.CorePackageFile, 0, len(p.Files))}
	for _, f := range p.Files {
		out.Files = append(out.Files, api.CorePackageFile{Name: f.Name, Role: api.CorePackageFileRole(f.Role), Size: f.Size,
			Sha256: f.SHA256, Available: f.Available})
	}
	return out, nil
}

// GetCorePackageFile streams a cached core file (ETag = "<sha256>", If-None-Match answers 304). The file is
// never loaded into memory.
func (s *Server) GetCorePackageFile(ctx context.Context, req api.GetCorePackageFileRequestObject) (api.GetCorePackageFileResponseObject, error) {
	f, def, err := s.svc.OpenCoreFile(ctx, req.CoreId, req.Version, string(req.Platform), req.Name)
	if err != nil {
		return nil, err
	}
	etag := `"` + def.SHA256 + `"`
	if req.Params.IfNoneMatch != nil && etagMatches(*req.Params.IfNoneMatch, etag) {
		f.Close()
		return api.GetCorePackageFile304Response{Headers: api.GetCorePackageFile304ResponseHeaders{ETag: ptr(etag)}}, nil
	}
	return api.GetCorePackageFile200ApplicationoctetStreamResponse{Body: f, ContentLength: def.Size,
		Headers: api.GetCorePackageFile200ResponseHeaders{ContentLength: ptr(def.Size), ETag: ptr(etag)}}, nil
}

// etagMatches implements the weak comparison of If-None-Match (list, "*", W/ prefix).
func etagMatches(header, etag string) bool {
	for _, p := range strings.Split(header, ",") {
		p = strings.TrimPrefix(strings.TrimSpace(p), "W/")
		if p == "*" || p == etag {
			return true
		}
	}
	return false
}

// coreOrigin maps a stored origin to the API enum: anything but the buildbot is a legacy FrameBeam package.
func coreOrigin(o string) api.CoreOrigin {
	if o == hub.OriginBuildbot {
		return api.LibretroBuildbot
	}
	return api.Framebeam
}
