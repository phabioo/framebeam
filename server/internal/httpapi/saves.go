package httpapi

import (
	"context"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
)

func mustUUID(s string) uuid.UUID {
	id, err := uuid.Parse(s)
	if err != nil {
		return uuid.Nil
	}
	return id
}

func toAPICheckpoint(c hub.SaveCheckpoint) api.SaveCheckpoint {
	return api.SaveCheckpoint{Revision: c.Revision, Sha256: c.SHA256, Size: c.Size, DeviceId: mustUUID(c.DeviceID),
		DeviceName: c.DeviceName, CreatedAt: c.CreatedAt, Reason: api.SaveSyncReason(c.Reason)}
}

func toAPIConflict(c hub.SaveConflict) api.SaveConflict {
	out := api.SaveConflict{Id: c.ID, GameId: mustUUID(c.GameID), Slot: c.Slot, Status: api.SaveConflictStatus(c.Status),
		Hub: api.SaveConflictHubSide{Revision: c.Hub.Revision, Sha256: c.Hub.SHA256, DeviceId: mustUUID(c.Hub.DeviceID),
			DeviceName: c.Hub.DeviceName, CreatedAt: c.Hub.CreatedAt},
		Secured: api.SaveConflictSecuredUpload{Version: c.Secured.Version, Sha256: c.Secured.SHA256, BaseRevision: c.Secured.BaseRevision,
			DeviceId: mustUUID(c.Secured.DeviceID), DeviceName: c.Secured.DeviceName, CreatedAt: c.Secured.CreatedAt},
		CreatedAt: c.CreatedAt, ResolvedAt: c.ResolvedAt}
	if c.ResolvedBy != "" {
		by := c.ResolvedBy
		out.ResolvedBy = &by
	}
	return out
}

func toAPISlot(s hub.SaveSlot) api.SaveSlot {
	out := api.SaveSlot{GameId: mustUUID(s.GameID), Slot: s.Slot, Current: toAPICheckpoint(s.Current),
		OpenConflicts: make([]api.SaveConflict, 0, len(s.OpenConflicts))}
	for _, c := range s.OpenConflicts {
		out.OpenConflicts = append(out.OpenConflicts, toAPIConflict(c))
	}
	return out
}

func (s *Server) ListSaves(ctx context.Context, _ api.ListSavesRequestObject) (api.ListSavesResponseObject, error) {
	rows, err := s.svc.ListSaveSlots(ctx, principal(ctx).User.ID)
	if err != nil {
		return nil, err
	}
	out := api.ListSaves200JSONResponse{Saves: make([]api.SaveSlotSummary, 0, len(rows))}
	for _, r := range rows {
		out.Saves = append(out.Saves, api.SaveSlotSummary{GameId: mustUUID(r.GameID), Slot: r.Slot,
			Current: toAPICheckpoint(r.Current), OpenConflictCount: r.OpenConflictCount})
	}
	return out, nil
}

func (s *Server) GetSaveSlot(ctx context.Context, req api.GetSaveSlotRequestObject) (api.GetSaveSlotResponseObject, error) {
	sl, err := s.svc.GetSaveSlot(ctx, principal(ctx).User.ID, req.GameId.String(), req.Slot)
	if err != nil {
		return nil, err
	}
	return api.GetSaveSlot200JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) PutSave(ctx context.Context, req api.PutSaveRequestObject) (api.PutSaveResponseObject, error) {
	p := principal(ctx)
	res, err := s.svc.PutSave(ctx, hub.PutSaveInput{UserID: p.User.ID, DeviceID: p.Device.ID, GameID: req.GameId.String(),
		Slot: req.Slot, BaseRevision: req.Params.XFrameBeamBaseRevision, SHA256: req.Params.XFrameBeamContentSHA256,
		Reason: string(req.Params.XFrameBeamSyncReason), Body: req.Body})
	if err != nil {
		return nil, err
	}
	if res.Conflict != nil {
		var e api.SaveConflictError
		e.Error.Code, e.Error.Message = api.ErrorCodeSaveConflict, "Base revision is stale; upload secured as conflict"
		e.Conflict = toAPIConflict(*res.Conflict)
		return api.PutSave409JSONResponse(e), nil
	}
	return api.PutSave200JSONResponse(toAPISlot(res.Slot)), nil
}

func (s *Server) DownloadSaveContent(ctx context.Context, req api.DownloadSaveContentRequestObject) (api.DownloadSaveContentResponseObject, error) {
	f, c, err := s.svc.OpenSaveContent(ctx, principal(ctx).User.ID, req.GameId.String(), req.Slot)
	if err != nil {
		return nil, err
	}
	return api.DownloadSaveContent200ApplicationoctetStreamResponse{Body: f, Headers: api.DownloadSaveContent200ResponseHeaders{
		ContentLength: ptr(c.Size), ETag: ptr(`"` + c.SHA256 + `"`), XFrameBeamSaveRevision: ptr(c.Revision)}}, nil
}

func (s *Server) ListSaveHistory(ctx context.Context, req api.ListSaveHistoryRequestObject) (api.ListSaveHistoryResponseObject, error) {
	vs, err := s.svc.ListSaveHistory(ctx, principal(ctx).User.ID, req.GameId.String(), req.Slot)
	if err != nil {
		return nil, err
	}
	out := api.ListSaveHistory200JSONResponse{Versions: make([]api.SaveHistoryVersion, 0, len(vs))}
	for _, v := range vs {
		out.Versions = append(out.Versions, api.SaveHistoryVersion{Version: v.Version, Revision: v.Revision, Sha256: v.SHA256,
			Size: v.Size, DeviceId: mustUUID(v.DeviceID), DeviceName: v.DeviceName, CreatedAt: v.CreatedAt,
			Reason: api.SaveHistoryReason(v.Reason), Label: v.Label})
	}
	return out, nil
}

func (s *Server) DownloadSaveHistoryContent(ctx context.Context, req api.DownloadSaveHistoryContentRequestObject) (api.DownloadSaveHistoryContentResponseObject, error) {
	f, v, err := s.svc.OpenSaveVersion(ctx, principal(ctx).User.ID, req.GameId.String(), req.Slot, req.Version)
	if err != nil {
		return nil, err
	}
	return api.DownloadSaveHistoryContent200ApplicationoctetStreamResponse{Body: f, Headers: api.DownloadSaveHistoryContent200ResponseHeaders{
		ContentLength: ptr(v.Size), ETag: ptr(`"` + v.SHA256 + `"`), XFrameBeamSaveRevision: ptr(v.Revision)}}, nil
}

func (s *Server) ResolveSaveConflict(ctx context.Context, req api.ResolveSaveConflictRequestObject) (api.ResolveSaveConflictResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	p := principal(ctx)
	sl, err := s.svc.ResolveSaveConflict(ctx, hub.ResolveInput{UserID: p.User.ID, GameID: req.GameId.String(), Slot: req.Slot,
		ConflictID: req.ConflictId, Resolution: string(req.Body.Resolution), ExpectedRevision: req.Body.ExpectedRevision,
		ResolvedBy: "device:" + p.Device.ID})
	if err != nil {
		return nil, err
	}
	return api.ResolveSaveConflict200JSONResponse(toAPISlot(sl)), nil
}

func toAPIVersion(v hub.SaveVersion) api.SaveHistoryVersion {
	return api.SaveHistoryVersion{Version: v.Version, Revision: v.Revision, Sha256: v.SHA256, Size: v.Size,
		DeviceId: mustUUID(v.DeviceID), DeviceName: v.DeviceName, CreatedAt: v.CreatedAt,
		Reason: api.SaveHistoryReason(v.Reason), Label: v.Label}
}

func (s *Server) RestoreSaveHistoryVersion(ctx context.Context, req api.RestoreSaveHistoryVersionRequestObject) (api.RestoreSaveHistoryVersionResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	p := principal(ctx)
	sl, err := s.svc.RestoreSaveVersion(ctx, hub.RestoreInput{UserID: p.User.ID, DeviceID: p.Device.ID, GameID: req.GameId.String(),
		Slot: req.Slot, Version: req.Version, ExpectedRevision: req.Body.ExpectedRevision})
	if err != nil {
		return nil, err
	}
	return api.RestoreSaveHistoryVersion200JSONResponse(toAPISlot(sl)), nil
}

func (s *Server) CreateSaveSnapshot(ctx context.Context, req api.CreateSaveSnapshotRequestObject) (api.CreateSaveSnapshotResponseObject, error) {
	var label *string
	if req.Body != nil {
		label = req.Body.Label
	}
	v, err := s.svc.CreateSaveSnapshot(ctx, principal(ctx).User.ID, req.GameId.String(), req.Slot, label)
	if err != nil {
		return nil, err
	}
	return api.CreateSaveSnapshot201JSONResponse(toAPIVersion(v)), nil
}
