package httpapi

import (
	"context"

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
		si := api.SystemInfo{Id: e.ID, DisplayName: e.Name, PreferredCoreId: e.CoreID, FirmwareMode: api.FirmwareMode(e.FirmwareMode),
			Firmware: make([]api.FirmwareFile, 0, len(e.Firmware))}
		if e.ExpectedCoreVersion != "" {
			v := e.ExpectedCoreVersion
			si.ExpectedCoreVersion = &v
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
