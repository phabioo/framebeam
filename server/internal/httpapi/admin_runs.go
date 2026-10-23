package httpapi

import (
	"context"
	"encoding/json"
	"net/http"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Admin API for runs (0.18, `admin_v1`): every run including archived ones, status changes as the server admin, invitations,
// the event log export and removal. The plugin content (boards) is read-only.

func adminRunBody(r hub.Run) (api.AdminRun, error) {
	ar, err := toAPIRun(r)
	return api.AdminRun{Archived: r.Archived, Run: ar}, err
}

// adminRunDetail loads the run again and returns it with its boards.
func (s *Server) adminRunDetail(ctx context.Context, runID string) (api.AdminRunDetail, error) {
	r, err := s.svc.GetRunAdmin(ctx, runID)
	if err != nil {
		return api.AdminRunDetail{}, err
	}
	body, err := adminRunBody(r)
	if err != nil {
		return api.AdminRunDetail{}, err
	}
	boards, err := s.svc.RunBoards(ctx, r.ID)
	if err != nil {
		return api.AdminRunDetail{}, err
	}
	out := api.AdminRunDetail{Archived: body.Archived, Run: body.Run, Boards: []api.AdminRunBoard{}}
	if len(boards) > 0 {
		b, err := json.Marshal(boards)
		if err == nil {
			err = json.Unmarshal(b, &out.Boards)
		}
		if err != nil {
			return api.AdminRunDetail{}, err
		}
	}
	return out, nil
}

func (s *Server) ListAdminRuns(ctx context.Context, _ api.ListAdminRunsRequestObject) (api.ListAdminRunsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	runs, err := s.svc.ListRunsAdmin(ctx, true)
	if err != nil {
		return nil, err
	}
	out := api.ListAdminRuns200JSONResponse{Runs: make([]api.AdminRun, 0, len(runs))}
	for _, r := range runs {
		b, err := adminRunBody(r)
		if err != nil {
			return nil, err
		}
		out.Runs = append(out.Runs, b)
	}
	return out, nil
}

func (s *Server) GetAdminRun(ctx context.Context, req api.GetAdminRunRequestObject) (api.GetAdminRunResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	d, err := s.adminRunDetail(ctx, req.RunId.String())
	if err != nil {
		return nil, err
	}
	return api.GetAdminRun200JSONResponse(d), nil
}

// adminRunStatus changes the status as the server admin and answers with the run.
func (s *Server) adminRunStatus(ctx context.Context, runID, status string) (api.AdminRunDetail, error) {
	if err := requireAdmin(ctx); err != nil {
		return api.AdminRunDetail{}, err
	}
	if _, err := s.svc.SetRunStatusAdmin(ctx, principal(ctx).User.ID, runID, status); err != nil {
		return api.AdminRunDetail{}, err
	}
	return s.adminRunDetail(ctx, runID)
}

func (s *Server) PauseAdminRun(ctx context.Context, req api.PauseAdminRunRequestObject) (api.PauseAdminRunResponseObject, error) {
	d, err := s.adminRunStatus(ctx, req.RunId.String(), hub.RunPaused)
	if err != nil {
		return nil, err
	}
	return api.PauseAdminRun200JSONResponse(d), nil
}

func (s *Server) ResumeAdminRun(ctx context.Context, req api.ResumeAdminRunRequestObject) (api.ResumeAdminRunResponseObject, error) {
	d, err := s.adminRunStatus(ctx, req.RunId.String(), hub.RunActive)
	if err != nil {
		return nil, err
	}
	return api.ResumeAdminRun200JSONResponse(d), nil
}

func (s *Server) FinishAdminRun(ctx context.Context, req api.FinishAdminRunRequestObject) (api.FinishAdminRunResponseObject, error) {
	d, err := s.adminRunStatus(ctx, req.RunId.String(), hub.RunFinished)
	if err != nil {
		return nil, err
	}
	return api.FinishAdminRun200JSONResponse(d), nil
}

func (s *Server) StartAdminRun(ctx context.Context, req api.StartAdminRunRequestObject) (api.StartAdminRunResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	without := req.Body != nil && req.Body.WithoutMissing != nil && *req.Body.WithoutMissing
	id := req.RunId.String()
	if _, err := s.svc.StartRunAdmin(ctx, principal(ctx).User.ID, id, without); err != nil {
		return nil, err
	}
	d, err := s.adminRunDetail(ctx, id)
	if err != nil {
		return nil, err
	}
	return api.StartAdminRun200JSONResponse(d), nil
}

func (s *Server) archiveAdminRun(ctx context.Context, runID string, archived bool) (api.AdminRunDetail, error) {
	if err := requireAdmin(ctx); err != nil {
		return api.AdminRunDetail{}, err
	}
	if err := s.svc.ArchiveRun(ctx, runID, archived); err != nil {
		return api.AdminRunDetail{}, err
	}
	return s.adminRunDetail(ctx, runID)
}

func (s *Server) ArchiveAdminRun(ctx context.Context, req api.ArchiveAdminRunRequestObject) (api.ArchiveAdminRunResponseObject, error) {
	d, err := s.archiveAdminRun(ctx, req.RunId.String(), true)
	if err != nil {
		return nil, err
	}
	return api.ArchiveAdminRun200JSONResponse(d), nil
}

func (s *Server) UnarchiveAdminRun(ctx context.Context, req api.UnarchiveAdminRunRequestObject) (api.UnarchiveAdminRunResponseObject, error) {
	d, err := s.archiveAdminRun(ctx, req.RunId.String(), false)
	if err != nil {
		return nil, err
	}
	return api.UnarchiveAdminRun200JSONResponse(d), nil
}

func (s *Server) ResendAdminRunInvites(ctx context.Context, req api.ResendAdminRunInvitesRequestObject) (api.ResendAdminRunInvitesResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	id := req.RunId.String()
	if err := s.svc.ResendRunInvites(ctx, principal(ctx).User.ID, id); err != nil {
		return nil, err
	}
	d, err := s.adminRunDetail(ctx, id)
	if err != nil {
		return nil, err
	}
	return api.ResendAdminRunInvites200JSONResponse(d), nil
}

func (s *Server) ResendAdminRunInvite(ctx context.Context, req api.ResendAdminRunInviteRequestObject) (api.ResendAdminRunInviteResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.ResendRunInvite(ctx, principal(ctx).User.ID, req.RunId.String(), req.UserId); err != nil {
		return nil, err
	}
	return api.ResendAdminRunInvite204Response{}, nil
}

func (s *Server) RemoveAdminRunInvite(ctx context.Context, req api.RemoveAdminRunInviteRequestObject) (api.RemoveAdminRunInviteResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.RemoveRunInvite(ctx, principal(ctx).User.ID, req.RunId.String(), req.UserId); err != nil {
		return nil, err
	}
	return api.RemoveAdminRunInvite204Response{}, nil
}

func (s *Server) DeleteAdminRun(ctx context.Context, req api.DeleteAdminRunRequestObject) (api.DeleteAdminRunResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DeleteRun(ctx, req.RunId.String()); err != nil {
		return nil, err
	}
	return api.DeleteAdminRun204Response{}, nil
}

// runExportResponse streams the event log like the app export: the page size is bounded, the document is not built in
// memory. A failure after the first byte ends the document with an "error" member (see hub.RunExport.WriteTo) and
// is logged.
type runExportResponse struct {
	ctx context.Context
	x   *hub.RunExport
	log func(error)
}

func (e runExportResponse) VisitExportAdminRunResponse(w http.ResponseWriter) error {
	h := w.Header()
	h.Set("Content-Type", "application/json")
	h.Set("Content-Disposition", `attachment; filename="`+e.x.FileName()+`"`)
	w.WriteHeader(http.StatusOK)
	if err := e.x.WriteTo(e.ctx, w); err != nil {
		e.log(err)
	}
	return nil
}

func (s *Server) ExportAdminRun(ctx context.Context, req api.ExportAdminRunRequestObject) (api.ExportAdminRunResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	x, err := s.svc.OpenRunExport(ctx, req.RunId.String())
	if err != nil {
		return nil, err
	}
	return runExportResponse{ctx: ctx, x: x, log: func(err error) { s.log.Error("run export incomplete", "run_id", x.Run().ID, "err", err) }}, nil
}
