package httpapi

import (
	"context"
	"net/http"

	"github.com/phabioo/framebeam/server/internal/api"
)

// Temporary phase 5 stubs: the Hub agent replaces these with real handlers.

type notImplemented struct{}

func (notImplemented) write(w http.ResponseWriter) error {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusNotImplemented)
	_, err := w.Write([]byte(`{"error":{"code":"internal","message":"Not implemented"}}`))
	return err
}

func (n notImplemented) VisitRedeemInviteResponse(w http.ResponseWriter) error    { return n.write(w) }
func (n notImplemented) VisitUploadGameResponse(w http.ResponseWriter) error      { return n.write(w) }
func (n notImplemented) VisitListSystemsResponse(w http.ResponseWriter) error     { return n.write(w) }
func (n notImplemented) VisitGetFirmwareFileResponse(w http.ResponseWriter) error { return n.write(w) }

func (s *Server) RedeemInvite(context.Context, api.RedeemInviteRequestObject) (api.RedeemInviteResponseObject, error) {
	return notImplemented{}, nil
}

func (s *Server) UploadGame(context.Context, api.UploadGameRequestObject) (api.UploadGameResponseObject, error) {
	return notImplemented{}, nil
}

func (s *Server) ListSystems(context.Context, api.ListSystemsRequestObject) (api.ListSystemsResponseObject, error) {
	return notImplemented{}, nil
}

func (s *Server) GetFirmwareFile(context.Context, api.GetFirmwareFileRequestObject) (api.GetFirmwareFileResponseObject, error) {
	return notImplemented{}, nil
}
