package httpapi

import (
	"context"
	"errors"
	"net/http"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
)

// UploadGame streams a raw ROM body to disk (same validation and de-duplication as the web upload).
func (s *Server) UploadGame(ctx context.Context, req api.UploadGameRequestObject) (api.UploadGameResponseObject, error) {
	p := principal(ctx)
	can, err := s.svc.CanUpload(ctx, p.User)
	if err != nil {
		return nil, err
	}
	if !can {
		return nil, hub.ErrUploadsDisabled
	}
	title := ""
	if req.Params.Title != nil {
		title = *req.Params.Title
	}
	g, err := s.svc.AddROM(ctx, req.Body, req.Params.Filename, title, "", p.User.ID)
	if err != nil {
		var mbe *http.MaxBytesError
		var he *hub.Error
		switch {
		case errors.As(err, &mbe):
			return nil, &hub.Error{Code: hub.CodePayloadTooLarge, Message: "ROM exceeds the upload limit"}
		case errors.As(err, &he) && he.Code == hub.CodeConflict && he.ExistingGameID != "":
			id, perr := uuid.Parse(he.ExistingGameID)
			if perr != nil {
				return nil, perr
			}
			var out api.UploadGame409JSONResponse
			out.Error.Code, out.Error.Message = api.ErrorCode(hub.CodeConflict), he.Message
			out.ExistingGameId = id
			return out, nil
		}
		return nil, err
	}
	ag, err := toAPIGame(g)
	if err != nil {
		return nil, err
	}
	return api.UploadGame201JSONResponse(ag), nil
}
