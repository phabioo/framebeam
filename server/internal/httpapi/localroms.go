package httpapi

import (
	"context"
	"regexp"
	"strconv"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// RegisterGame registers a ROM file that exists only on the caller's device (feature local_roms_v1).
func (s *Server) RegisterGame(ctx context.Context, req api.RegisterGameRequestObject) (api.RegisterGameResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	in := hub.RegisterInput{SHA256: b.Sha256, Size: b.Size, Filename: b.Filename, GameVersion: b.GameVersion}
	if b.System != nil {
		in.System = *b.System
	}
	if b.Title != nil {
		in.Title = *b.Title
	}
	if b.GameCode != nil {
		in.GameCode = *b.GameCode
	}
	g, created, err := s.svc.RegisterROM(ctx, principal(ctx).User.ID, in)
	if err != nil {
		return nil, err
	}
	ag, err := toAPIGame(g)
	if err != nil {
		return nil, err
	}
	if created {
		return api.RegisterGame201JSONResponse(ag), nil
	}
	return api.RegisterGame200JSONResponse(ag), nil
}

// DeleteGameRegistration removes the caller's local registrations for a game.
func (s *Server) DeleteGameRegistration(ctx context.Context, req api.DeleteGameRegistrationRequestObject) (api.DeleteGameRegistrationResponseObject, error) {
	if err := s.svc.UnregisterROM(ctx, principal(ctx).User.ID, req.GameId.String()); err != nil {
		return nil, err
	}
	return api.DeleteGameRegistration204Response{}, nil
}

func (s *Server) requireUpload(ctx context.Context) (hub.Principal, error) {
	p := principal(ctx)
	can, err := s.svc.CanUpload(ctx, p.User)
	if err != nil {
		return p, err
	}
	if !can {
		return p, hub.ErrUploadsDisabled
	}
	return p, nil
}

// CreateUpload starts or resumes a resumable ROM upload.
func (s *Server) CreateUpload(ctx context.Context, req api.CreateUploadRequestObject) (api.CreateUploadResponseObject, error) {
	p, err := s.requireUpload(ctx)
	if err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	in := hub.UploadInput{Filename: b.Filename, Size: b.Size}
	if b.Sha256 != nil {
		in.SHA256 = *b.Sha256
	}
	if b.Title != nil {
		in.Title = *b.Title
	}
	if b.System != nil {
		in.System = *b.System
	}
	u, err := s.svc.CreateUpload(ctx, p.User.ID, in)
	if err != nil {
		return nil, err
	}
	id, err := uuid.Parse(u.ID)
	if err != nil {
		return nil, err
	}
	return api.CreateUpload201JSONResponse{UploadId: id, Offset: u.Offset, ChunkSize: hub.UploadChunkBytes}, nil
}

// GetUpload returns the state of an upload session of the caller.
func (s *Server) GetUpload(ctx context.Context, req api.GetUploadRequestObject) (api.GetUploadResponseObject, error) {
	if _, err := s.requireUpload(ctx); err != nil {
		return nil, err
	}
	u, err := s.svc.GetUpload(ctx, principal(ctx).User.ID, req.UploadId.String())
	if err != nil {
		return nil, err
	}
	return api.GetUpload200JSONResponse{UploadId: req.UploadId, Offset: u.Offset, Size: u.Size}, nil
}

var contentRangeRe = regexp.MustCompile(`^bytes ([0-9]{1,19})-([0-9]{1,19})/([0-9]{1,19})$`)

// PutUploadChunk stores one chunk; the chunk that completes the file finishes the upload.
func (s *Server) PutUploadChunk(ctx context.Context, req api.PutUploadChunkRequestObject) (api.PutUploadChunkResponseObject, error) {
	p, err := s.requireUpload(ctx)
	if err != nil {
		return nil, err
	}
	m := contentRangeRe.FindStringSubmatch(req.Params.ContentRange)
	if m == nil {
		return nil, hub.ErrBadRequest
	}
	a, e1 := strconv.ParseInt(m[1], 10, 64)
	b, e2 := strconv.ParseInt(m[2], 10, 64)
	total, e3 := strconv.ParseInt(m[3], 10, 64)
	if e1 != nil || e2 != nil || e3 != nil {
		return nil, hub.ErrBadRequest
	}
	u, g, err := s.svc.AppendUpload(ctx, p.User.ID, req.UploadId.String(), hub.UploadChunk{Start: a, End: b, Total: total}, req.Body)
	if err != nil {
		return nil, err
	}
	out := api.UploadProgress{Offset: u.Offset}
	if g != nil {
		ag, err := toAPIGame(*g)
		if err != nil {
			return nil, err
		}
		out.Game = &ag
	}
	return api.PutUploadChunk200JSONResponse(out), nil
}

// DeleteUpload aborts an upload session.
func (s *Server) DeleteUpload(ctx context.Context, req api.DeleteUploadRequestObject) (api.DeleteUploadResponseObject, error) {
	if _, err := s.requireUpload(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.AbortUpload(ctx, principal(ctx).User.ID, req.UploadId.String()); err != nil {
		return nil, err
	}
	return api.DeleteUpload204Response{}, nil
}
