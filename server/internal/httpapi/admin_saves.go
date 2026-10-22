package httpapi

import (
	"context"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Admin API for the saves of all users (0.18, `admin_v1`). The hub functions are the ones of the member API and of the
// web interface; the path names the owner. Changes are recorded as made by the web device of the admin
// (hub.WebDeviceID, like the web interface) and follow the same base revision rules: a stale revision is a 409, nothing is
// overwritten silently.

func (s *Server) ListAdminSaves(ctx context.Context, req api.ListAdminSavesRequestObject) (api.ListAdminSavesResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	user := ""
	if req.Params.UserId != nil {
		user = *req.Params.UserId
	}
	rows, err := s.svc.ListSaveSlots(ctx, user)
	if err != nil {
		return nil, err
	}
	out := api.AdminSaveList{Saves: make([]api.AdminSaveSummary, 0, len(rows))}
	for _, r := range rows {
		if req.Params.GameId != nil && r.GameID != req.Params.GameId.String() {
			continue
		}
		out.Saves = append(out.Saves, api.AdminSaveSummary{UserId: r.UserID, Username: r.Username, GameId: r.GameID,
			GameTitle: r.GameTitle, Slot: r.Slot, Current: toAPICheckpoint(r.Current), OpenConflictCount: r.OpenConflictCount})
	}
	return api.ListAdminSaves200JSONResponse(out), nil
}

func (s *Server) GetAdminSaveSlot(ctx context.Context, req api.GetAdminSaveSlotRequestObject) (api.GetAdminSaveSlotResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	sl, err := s.svc.GetSaveSlot(ctx, req.UserId, req.GameId.String(), req.Slot)
	if err != nil {
		return nil, err
	}
	return api.GetAdminSaveSlot200JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) ListAdminSaveHistory(ctx context.Context, req api.ListAdminSaveHistoryRequestObject) (api.ListAdminSaveHistoryResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	vs, err := s.svc.ListSaveHistory(ctx, req.UserId, req.GameId.String(), req.Slot)
	if err != nil {
		return nil, err
	}
	out := api.ListAdminSaveHistory200JSONResponse{Versions: make([]api.SaveHistoryVersion, 0, len(vs))}
	for _, v := range vs {
		out.Versions = append(out.Versions, toAPIVersion(v))
	}
	return out, nil
}

func (s *Server) DownloadAdminSaveContent(ctx context.Context, req api.DownloadAdminSaveContentRequestObject) (api.DownloadAdminSaveContentResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	f, c, err := s.svc.OpenSaveContent(ctx, req.UserId, req.GameId.String(), req.Slot)
	if err != nil {
		return nil, err
	}
	return api.DownloadAdminSaveContent200ApplicationoctetStreamResponse{Body: f, Headers: api.DownloadAdminSaveContent200ResponseHeaders{
		ContentLength: ptr(c.Size), ETag: ptr(`"` + c.SHA256 + `"`), XUzumeSaveRevision: ptr(c.Revision)}}, nil
}

func (s *Server) DownloadAdminSaveHistoryContent(ctx context.Context, req api.DownloadAdminSaveHistoryContentRequestObject) (api.DownloadAdminSaveHistoryContentResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	f, v, err := s.svc.OpenSaveVersion(ctx, req.UserId, req.GameId.String(), req.Slot, req.Version)
	if err != nil {
		return nil, err
	}
	return api.DownloadAdminSaveHistoryContent200ApplicationoctetStreamResponse{Body: f, Headers: api.DownloadAdminSaveHistoryContent200ResponseHeaders{
		ContentLength: ptr(v.Size), ETag: ptr(`"` + v.SHA256 + `"`), XUzumeSaveRevision: ptr(v.Revision)}}, nil
}

func (s *Server) ResolveAdminSaveConflict(ctx context.Context, req api.ResolveAdminSaveConflictRequestObject) (api.ResolveAdminSaveConflictResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	sl, err := s.svc.ResolveSaveConflict(ctx, hub.ResolveInput{UserID: req.UserId, GameID: req.GameId.String(), Slot: req.Slot,
		ConflictID: req.ConflictId, Resolution: string(req.Body.Resolution), ExpectedRevision: req.Body.ExpectedRevision,
		ResolvedBy: "user:" + principal(ctx).User.ID})
	if err != nil {
		return nil, err
	}
	return api.ResolveAdminSaveConflict200JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) RestoreAdminSaveVersion(ctx context.Context, req api.RestoreAdminSaveVersionRequestObject) (api.RestoreAdminSaveVersionResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.UserId != principal(ctx).User.ID {
		if err := s.requireConfirmed(ctx); err != nil {
			return nil, err
		}
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	sl, err := s.svc.RestoreSaveVersion(ctx, hub.RestoreInput{UserID: req.UserId, DeviceID: hub.WebDeviceID(principal(ctx).User.ID),
		GameID: req.GameId.String(), Slot: req.Slot, Version: req.Version, ExpectedRevision: req.Body.ExpectedRevision})
	if err != nil {
		return nil, err
	}
	return api.RestoreAdminSaveVersion200JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) CreateAdminSaveSnapshot(ctx context.Context, req api.CreateAdminSaveSnapshotRequestObject) (api.CreateAdminSaveSnapshotResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	var label *string
	if req.Body != nil {
		label = req.Body.Label
	}
	v, err := s.svc.CreateSaveSnapshot(ctx, req.UserId, req.GameId.String(), req.Slot, label)
	if err != nil {
		return nil, err
	}
	return api.CreateAdminSaveSnapshot201JSONResponse(toAPIVersion(v)), nil
}

func (s *Server) DeleteAdminSaveVersion(ctx context.Context, req api.DeleteAdminSaveVersionRequestObject) (api.DeleteAdminSaveVersionResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.UserId != principal(ctx).User.ID {
		if err := s.requireConfirmed(ctx); err != nil {
			return nil, err
		}
	}
	if err := s.svc.DeleteSaveVersion(ctx, req.UserId, req.GameId.String(), req.Slot, req.Version); err != nil {
		return nil, err
	}
	return api.DeleteAdminSaveVersion204Response{}, nil
}

func (s *Server) CreateAdminSaveSlot(ctx context.Context, req api.CreateAdminSaveSlotRequestObject) (api.CreateAdminSaveSlotResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	sl, err := s.svc.CreateSaveSlot(ctx, req.UserId, req.GameId.String(), req.Slot, req.Body.Name, hub.WebDeviceID(principal(ctx).User.ID))
	if err != nil {
		return nil, err
	}
	return api.CreateAdminSaveSlot201JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) RenameAdminSaveSlot(ctx context.Context, req api.RenameAdminSaveSlotRequestObject) (api.RenameAdminSaveSlotResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	sl, err := s.svc.RenameSaveSlot(ctx, hub.RenameInput{UserID: req.UserId, DeviceID: hub.WebDeviceID(principal(ctx).User.ID),
		GameID: req.GameId.String(), Slot: req.Slot, NewSlot: req.Body.Name})
	if err != nil {
		return nil, err
	}
	return api.RenameAdminSaveSlot200JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) DeleteAdminSaveSlot(ctx context.Context, req api.DeleteAdminSaveSlotRequestObject) (api.DeleteAdminSaveSlotResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.UserId != principal(ctx).User.ID {
		if err := s.requireConfirmed(ctx); err != nil {
			return nil, err
		}
	}
	if req.Params.ConfirmRevision < 1 {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.DeleteSaveSlot(ctx, hub.DeleteInput{UserID: req.UserId, DeviceID: hub.WebDeviceID(principal(ctx).User.ID),
		GameID: req.GameId.String(), Slot: req.Slot, ConfirmRevision: req.Params.ConfirmRevision}); err != nil {
		return nil, err
	}
	return api.DeleteAdminSaveSlot204Response{}, nil
}

func (s *Server) UploadAdminSaveFile(ctx context.Context, req api.UploadAdminSaveFileRequestObject) (api.UploadAdminSaveFileResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil || req.Params.XUzumeContentSHA256 == "" {
		return nil, hub.ErrBadRequest
	}
	rev := req.Params.XUzumeExpectedRevision
	sl, err := s.svc.UploadSaveFile(ctx, hub.UploadSaveInput{UserID: req.UserId, DeviceID: hub.WebDeviceID(principal(ctx).User.ID),
		GameID: req.GameId.String(), Slot: req.Slot, ExpectedRevision: &rev, SHA256: req.Params.XUzumeContentSHA256, Body: req.Body})
	if err != nil {
		return nil, err
	}
	return api.UploadAdminSaveFile200JSONResponse(toAPISlot(sl)), nil
}
