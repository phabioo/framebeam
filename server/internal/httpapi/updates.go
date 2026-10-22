package httpapi

import (
	"context"
	"errors"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// Admin update rollback API (ADR 0029 D4): /api/v1/admin/updates. Every call needs the token of an admin's device.

func apiPause(p *updates.Pause) *api.AdminUpdatePause {
	if p == nil {
		return nil
	}
	o := &api.AdminUpdatePause{From: p.From, To: p.To, At: p.At}
	if p.By != "" {
		o.By = ptr(p.By)
	}
	if p.Backup != "" {
		o.Backup = ptr(p.Backup)
	}
	if p.RestoredDB != "" {
		o.RestoredDb = ptr(p.RestoredDB)
	}
	if !p.LostSince.IsZero() {
		o.LostSince = ptr(p.LostSince)
	}
	return o
}

func (s *Server) GetAdminUpdateVersions(ctx context.Context, _ api.GetAdminUpdateVersionsRequestObject) (api.GetAdminUpdateVersionsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	info, err := s.svc.RollbackVersions(ctx)
	if err != nil {
		if errors.Is(err, hub.ErrUpdatesOff) {
			return nil, &hub.Error{Code: hub.CodeBadRequest, Message: "Updates are off for this Hub"}
		}
		return nil, err
	}
	out := api.AdminUpdateVersions{Current: info.Current, CurrentSchema: info.CurrentSchema, CanRollback: info.CanRollback, InContainer: info.InContainer,
		Paused: apiPause(info.Paused), BackupFolder: info.BackupFolder, ConnectedPlayers: info.ConnectedPlayers, Versions: []api.AdminUpdateVersion{}}
	if info.Unavailable != "" {
		out.Unavailable = ptr(info.Unavailable)
	}
	if info.Skipped != "" {
		out.Skipped = ptr(info.Skipped)
		out.SkippedAvailable = ptr(info.SkippedAvailable != nil)
	}
	for _, v := range info.Versions {
		o := api.AdminUpdateVersion{Version: v.Version, Channel: api.AdminUpdateVersionChannel(v.Channel), PublishedAt: v.PublishedAt,
			SchemaVersion: v.SchemaVersion, Data: api.AdminUpdateVersionData(v.Action)}
		if v.NotesURL != "" {
			o.NotesUrl = ptr(v.NotesURL)
		}
		if !v.FirstUsed.IsZero() {
			o.FirstUsed, o.LastUsed = ptr(v.FirstUsed), ptr(v.LastUsed)
		}
		if v.Reason != "" {
			o.Reason = ptr(v.Reason)
		}
		if v.DBBackup != "" {
			o.DbBackup, o.LostSince = ptr(v.DBBackup), ptr(v.LostSince)
		}
		if v.ManualCommand != "" {
			o.ManualCommand = ptr(v.ManualCommand)
		}
		out.Versions = append(out.Versions, o)
	}
	return api.GetAdminUpdateVersions200JSONResponse(out), nil
}

func (s *Server) RollbackAdminUpdate(ctx context.Context, req api.RollbackAdminUpdateRequestObject) (api.RollbackAdminUpdateResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	who := principal(ctx).User.Username
	res, err := s.svc.Rollback(ctx, hub.RollbackRequest{Version: req.Body.Version, By: who, ConfirmDataLoss: req.Body.ConfirmDataLoss != nil && *req.Body.ConfirmDataLoss})
	switch {
	case err == nil:
	case errors.Is(err, hub.ErrRollbackConfirm):
		return nil, &hub.Error{Code: hub.CodeConflict, Message: err.Error()}
	case errors.Is(err, hub.ErrInContainer), errors.Is(err, hub.ErrNotPackaged), errors.Is(err, hub.ErrUpdatesOff):
		return nil, &hub.Error{Code: hub.CodeBadRequest, Message: err.Error()}
	default:
		return nil, err
	}
	s.log.Warn("update rollback started", "to", req.Body.Version, "data", string(res.Plan.Action), "admin", who)
	return api.RollbackAdminUpdate202JSONResponse{Version: res.Staged.Version, Data: api.AdminRollbackResultData(res.Plan.Action),
		SafetyBackup: res.Safety, SafetyFolder: res.SafetyDir}, nil
}

func (s *Server) ResumeAdminUpdates(ctx context.Context, _ api.ResumeAdminUpdatesRequestObject) (api.ResumeAdminUpdatesResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.ResumeUpdates(ctx); err != nil {
		return nil, err
	}
	s.log.Info("automatic updates resumed", "admin", principal(ctx).User.Username)
	return api.ResumeAdminUpdates204Response{}, nil
}
