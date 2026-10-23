package httpapi

import (
	"context"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Admin API for systems, cores and firmware (0.18, `admin_v1`). The handlers call the same hub functions as the
// Systems & Cores screen of the app.

func nz[T any](s []T) []T {
	if s == nil {
		return []T{}
	}
	return s
}

func (s *Server) adminCoreSource(ctx context.Context) (api.AdminCoreSource, error) {
	src, err := s.svc.CoreSource(ctx)
	if err != nil {
		return api.AdminCoreSource{}, err
	}
	return api.AdminCoreSource{BuildbotUrl: src.BuildbotURL, InfoUrl: src.InfoURL, LastCheck: src.LastCheck, LastSuccess: src.LastSuccess,
		LastError: strPtr(src.LastError), AutoUpdate: src.AutoUpdate, AutoUpdateError: strPtr(src.AutoUpdateError), Cores: src.Cores}, nil
}

func (s *Server) adminSystemBody(ctx context.Context, e hub.SystemEntry, globalAuto bool, devices hub.DeviceReports) (api.AdminSystem, error) {
	sc, err := s.svc.SystemCores(ctx, e.ID)
	if err != nil {
		return api.AdminSystem{}, err
	}
	auto := e.AutoUpdate
	if auto == hub.SystemAutoDefault {
		auto = "default"
	}
	out := api.AdminSystem{Id: e.ID, DisplayName: e.Name, Extensions: nz(e.Extensions), LibretroIds: nz(e.LibretroIDs), DefaultCoreId: strPtr(e.CoreID),
		Platforms: nz(e.Platforms), Provisioning: e.Provisioning, InputProfile: e.InputProfile, DisplayProfile: e.DisplayProfile,
		FirmwareMode: api.FirmwareMode(e.FirmwareMode), AutoUpdate: api.AdminSystemAutoUpdate(auto),
		AutoUpdateEffective: hub.SystemAutoEffective(globalAuto, e.AutoUpdate),
		Cores:               make([]api.AdminInstalledCore, 0, len(sc.Installed)), AvailableCores: make([]api.AdminAvailableCore, 0, len(sc.Available)),
		Firmware: make([]api.AdminFirmwareFile, 0, len(e.Firmware)), Clients: []api.AdminSystemClient{}}
	for _, c := range sc.Installed {
		mode := api.AdminInstalledCoreAutoUpdate("auto")
		if c.AutoUpdate == hub.CoreAutoOff {
			mode = "off"
		}
		out.Cores = append(out.Cores, api.AdminInstalledCore{CoreId: c.CoreID, DisplayName: c.DisplayName, Version: c.Version, License: c.License,
			Origin: coreOrigin(c.Origin), BuildDate: strPtr(c.BuildDate), RequiredHwApi: strPtr(c.RequiredHWAPI), Platforms: nz(c.Platforms),
			Default: c.Default, Experimental: c.Experimental, UpdateAvailable: c.UpdateAvailable, UpdateBuildDate: strPtr(c.UpdateDate),
			AutoUpdate: mode, AutoUpdateEffective: c.AutoUpdateEffective})
	}
	for _, c := range sc.Available {
		out.AvailableCores = append(out.AvailableCores, api.AdminAvailableCore{CoreId: c.CoreID, DisplayName: c.DisplayName, License: c.License,
			RequiredHwApi: strPtr(c.RequiredHWAPI), BuildDate: strPtr(c.BuildDate), Platforms: nz(c.Platforms), Extensions: nz(c.Extensions),
			Notes: nz(c.Notes), Experimental: c.Experimental, NonCommercial: c.NonCommercial})
	}
	for _, f := range e.Firmware {
		af := api.AdminFirmwareFile{Id: f.ID, DisplayName: f.DisplayName, Sizes: nz(f.Sizes), Required: f.Required, Present: f.Present,
			PinnedSha256: strPtr(f.Pinned), State: api.AdminFirmwareFileState(f.State)}
		if f.Present {
			size, sha, at := f.Size, f.SHA256, f.ProvidedAt
			af.Size, af.Sha256, af.ProvidedAt = &size, &sha, &at
		}
		out.Firmware = append(out.Firmware, af)
	}
	for _, c := range devices.ClientReports(e) {
		out.Clients = append(out.Clients, api.AdminSystemClient{DeviceId: c.DeviceID, DeviceName: c.DeviceName, Platform: c.Platform, Arch: c.Arch,
			PlayerVersion: c.PlayerVersion, CoreVersion: strPtr(c.CoreVersion), Status: api.AdminSystemClientStatus(c.Status), Expected: strPtr(c.Expected)})
	}
	return out, nil
}

// adminSystemByID is the updated system that every mutation answers with.
func (s *Server) adminSystemByID(ctx context.Context, id string) (api.AdminSystem, error) {
	e, err := s.svc.GetRegistryEntry(ctx, id)
	if err != nil {
		return api.AdminSystem{}, err
	}
	global, err := s.svc.CoreAutoUpdate(ctx)
	if err != nil {
		return api.AdminSystem{}, err
	}
	devices, err := s.svc.LoadDeviceReports(ctx)
	if err != nil {
		return api.AdminSystem{}, err
	}
	return s.adminSystemBody(ctx, e, global, devices)
}

func (s *Server) ListAdminSystems(ctx context.Context, _ api.ListAdminSystemsRequestObject) (api.ListAdminSystemsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	reg, err := s.svc.ListRegistry(ctx)
	if err != nil {
		return nil, err
	}
	global, err := s.svc.CoreAutoUpdate(ctx)
	if err != nil {
		return nil, err
	}
	devices, err := s.svc.LoadDeviceReports(ctx) // once for all systems
	if err != nil {
		return nil, err
	}
	out := api.AdminSystems{Systems: make([]api.AdminSystem, 0, len(reg))}
	for _, e := range reg {
		sys, err := s.adminSystemBody(ctx, e, global, devices)
		if err != nil {
			return nil, err
		}
		out.Systems = append(out.Systems, sys)
	}
	if out.CoreSource, err = s.adminCoreSource(ctx); err != nil {
		return nil, err
	}
	return api.ListAdminSystems200JSONResponse(out), nil
}

func (s *Server) UpdateAdminSystem(ctx context.Context, req api.UpdateAdminSystemRequestObject) (api.UpdateAdminSystemResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	if _, err := s.svc.GetRegistryEntry(ctx, req.SystemId); err != nil {
		return nil, err
	}
	// Check every field before one is applied.
	mode := ""
	if b.AutoUpdate != nil {
		switch m := string(*b.AutoUpdate); m {
		case "default":
		case hub.SystemAutoOn, hub.SystemAutoOff:
			mode = m
		default:
			return nil, &hub.Error{Code: hub.CodeBadRequest, Message: "Unknown automatic update mode"}
		}
	}
	if b.FirmwareMode != nil && *b.FirmwareMode != api.FirmwareMode(hub.FirmwareBuiltin) && *b.FirmwareMode != api.FirmwareMode(hub.FirmwareNative) {
		return nil, &hub.Error{Code: hub.CodeBadRequest, Message: "Unknown firmware mode"}
	}
	if b.AutoUpdate != nil {
		if err := s.svc.SetSystemCoreAutoUpdate(ctx, req.SystemId, mode); err != nil {
			return nil, err
		}
	}
	if b.DefaultCoreId != nil {
		if err := s.svc.SetDefaultCore(ctx, req.SystemId, *b.DefaultCoreId); err != nil {
			return nil, err
		}
	}
	if b.FirmwareMode != nil {
		if err := s.svc.SetFirmwareMode(ctx, req.SystemId, hub.FirmwareMode(*b.FirmwareMode)); err != nil {
			return nil, err
		}
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.UpdateAdminSystem200JSONResponse(sys), nil
}

func (s *Server) UpdateAdminSystemCore(ctx context.Context, req api.UpdateAdminSystemCoreRequestObject) (api.UpdateAdminSystemCoreResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	var pinned bool
	switch req.Body.AutoUpdate {
	case "auto":
	case "off":
		pinned = true
	default:
		return nil, &hub.Error{Code: hub.CodeBadRequest, Message: "Unknown automatic update mode"}
	}
	if err := s.svc.SetCoreAutoUpdatePin(ctx, req.SystemId, req.CoreId, pinned); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.UpdateAdminSystemCore200JSONResponse(sys), nil
}

func (s *Server) InstallAdminCore(ctx context.Context, req api.InstallAdminCoreRequestObject) (api.InstallAdminCoreResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if _, err := s.svc.InstallCore(ctx, req.SystemId, req.CoreId); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.InstallAdminCore200JSONResponse(sys), nil
}

func (s *Server) UpgradeAdminCore(ctx context.Context, req api.UpgradeAdminCoreRequestObject) (api.UpgradeAdminCoreResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if _, err := s.svc.UpdateCore(ctx, req.SystemId, req.CoreId); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.UpgradeAdminCore200JSONResponse(sys), nil
}

func (s *Server) RemoveAdminCore(ctx context.Context, req api.RemoveAdminCoreRequestObject) (api.RemoveAdminCoreResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.RemoveCore(ctx, req.SystemId, req.CoreId); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.RemoveAdminCore200JSONResponse(sys), nil
}

func (s *Server) SyncAdminCores(ctx context.Context, _ api.SyncAdminCoresRequestObject) (api.SyncAdminCoresResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	s.svc.TriggerCoreSync()
	return api.SyncAdminCores202Response{}, nil
}

func (s *Server) UpdateAdminCoreSettings(ctx context.Context, req api.UpdateAdminCoreSettingsRequestObject) (api.UpdateAdminCoreSettingsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.SetCoreAutoUpdate(ctx, req.Body.AutoUpdate); err != nil {
		return nil, err
	}
	src, err := s.adminCoreSource(ctx)
	if err != nil {
		return nil, err
	}
	return api.UpdateAdminCoreSettings200JSONResponse(src), nil
}

// ---- Firmware ----

// GetAdminFirmwareFile streams a provided firmware file; the bytes are never logged.
func (s *Server) GetAdminFirmwareFile(ctx context.Context, req api.GetAdminFirmwareFileRequestObject) (api.GetAdminFirmwareFileResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	f, def, err := s.svc.OpenFirmware(ctx, req.SystemId, req.FileId)
	if err != nil {
		return nil, err
	}
	return api.GetAdminFirmwareFile200ApplicationoctetStreamResponse{Body: f, ContentLength: def.Size}, nil
}

// PutAdminFirmwareFile takes the raw body (bounded by the 1 MiB request cap; the largest firmware image is 512 KiB).
func (s *Server) PutAdminFirmwareFile(ctx context.Context, req api.PutAdminFirmwareFileRequestObject) (api.PutAdminFirmwareFileResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if _, err := s.svc.ProvideFirmware(ctx, req.SystemId, req.FileId, req.Body); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.PutAdminFirmwareFile200JSONResponse(sys), nil
}

func (s *Server) PinAdminFirmwareFile(ctx context.Context, req api.PinAdminFirmwareFileRequestObject) (api.PinAdminFirmwareFileResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.SetFirmwarePin(ctx, req.SystemId, req.FileId, req.Body.Sha256); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.PinAdminFirmwareFile200JSONResponse(sys), nil
}

func (s *Server) RemoveAdminFirmwareFile(ctx context.Context, req api.RemoveAdminFirmwareFileRequestObject) (api.RemoveAdminFirmwareFileResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.RemoveFirmware(ctx, req.SystemId, req.FileId); err != nil {
		return nil, err
	}
	sys, err := s.adminSystemByID(ctx, req.SystemId)
	if err != nil {
		return nil, err
	}
	return api.RemoveAdminFirmwareFile200JSONResponse(sys), nil
}
