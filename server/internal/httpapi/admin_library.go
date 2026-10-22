package httpapi

import (
	"context"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Admin API for the library (0.18, `admin_v1`): rescan of the import folder, rename and remove an entry. Uploading a ROM
// needs no admin variant: an admin may always use POST /games (the member upload API).

func adminScan(p hub.ScanProgress) api.AdminScan {
	out := api.AdminScan{Running: p.Running, Done: p.Done, Total: p.Total, Added: p.Added, Finished: p.Finished}
	if !p.StartedAt.IsZero() {
		out.StartedAt = ptr(p.StartedAt)
	}
	if !p.FinishedAt.IsZero() {
		out.FinishedAt = ptr(p.FinishedAt)
	}
	if p.LastError != "" {
		out.LastError = ptr(p.LastError)
	}
	if p.Finished {
		sum := api.AdminScanSummary{Added: p.Summary.Added, AlreadyHere: p.Summary.AlreadyHere, Unsupported: p.Summary.Unsupported,
			Failed: make([]struct {
				File   string `json:"file"`
				Reason string `json:"reason"`
			}, 0, len(p.Summary.Failed))}
		for _, f := range p.Summary.Failed {
			sum.Failed = append(sum.Failed, struct {
				File   string `json:"file"`
				Reason string `json:"reason"`
			}{File: f.File, Reason: f.Reason})
		}
		out.Summary = &sum
	}
	return out
}

func (s *Server) GetAdminLibraryRescan(ctx context.Context, _ api.GetAdminLibraryRescanRequestObject) (api.GetAdminLibraryRescanResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	return api.GetAdminLibraryRescan200JSONResponse(adminScan(s.svc.ScanProgress())), nil
}

// StartAdminLibraryRescan starts a scan of the import folder like the Library page. A scan that already runs is
// not started twice; the answer is the state either way.
func (s *Server) StartAdminLibraryRescan(ctx context.Context, _ api.StartAdminLibraryRescanRequestObject) (api.StartAdminLibraryRescanResponseObject, error) {
	h, err := s.adminHost(ctx)
	if err != nil {
		return nil, err
	}
	dir, maxBytes := h.ImportFolder()
	if _, err := s.svc.StartRescan(ctx, dir, principal(ctx).User.ID, maxBytes); err != nil {
		return nil, err
	}
	return api.StartAdminLibraryRescan202JSONResponse(adminScan(s.svc.ScanProgress())), nil
}

func (s *Server) RenameAdminGame(ctx context.Context, req api.RenameAdminGameRequestObject) (api.RenameAdminGameResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	g, err := s.svc.RenameGame(ctx, req.GameId.String(), req.Body.Title)
	if err != nil {
		return nil, err
	}
	out, err := toAPIGame(g)
	if err != nil {
		return nil, err
	}
	return api.RenameAdminGame200JSONResponse(out), nil
}

func (s *Server) DeleteAdminGame(ctx context.Context, req api.DeleteAdminGameRequestObject) (api.DeleteAdminGameResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DeleteGame(ctx, req.GameId.String()); err != nil {
		return nil, err
	}
	return api.DeleteAdminGame204Response{}, nil
}
