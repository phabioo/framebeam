// Package httpapi implements the HTTP API of the FrameBeam Hub (OpenAPI spec in protocol/openapi)
// on top of the service layer internal/hub.
package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"log/slog"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
)

const maxBodyBytes = 1 << 20

type ctxKey int

const (
	keyBearer ctxKey = iota
	keyPrincipal
	keyRemoteIP
)

var errNotImplemented = errors.New("httpapi: not implemented")

// Server implements api.StrictServerInterface.
type Server struct {
	svc *hub.Service
	log *slog.Logger
}

// Register attaches the API routes (including the info endpoint and ROM download) to mux. The web interface
// registers its routes separately (e.g. "/").
func Register(mux *http.ServeMux, svc *hub.Service, log *slog.Logger) {
	if log == nil {
		log = slog.Default()
	}
	s := &Server{svc: svc, log: log}
	strict := api.NewStrictHandlerWithOptions(s, []api.StrictMiddlewareFunc{s.authMiddleware}, api.StrictHTTPServerOptions{
		RequestErrorHandlerFunc: func(w http.ResponseWriter, _ *http.Request, _ error) {
			writeError(w, http.StatusBadRequest, hub.CodeBadRequest, "Invalid request")
		},
		ResponseErrorHandlerFunc: func(w http.ResponseWriter, r *http.Request, err error) { s.writeErr(w, r, err) },
	})
	api.HandlerWithOptions(strict, api.StdHTTPServerOptions{
		BaseRouter: skipRomMux{mux},
		Middlewares: []api.MiddlewareFunc{func(next http.Handler) http.Handler {
			return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.Header().Set("Cache-Control", "no-store")
				limit := int64(maxBodyBytes)
				if r.Method == http.MethodPut && strings.Contains(r.URL.Path, "/saves/") {
					limit = hub.MaxSaveBytes + 1 // the service answers 413 payload_too_large
				}
				if r.Method == http.MethodPost && r.URL.Path == "/api/v1/games" {
					limit = hub.MaxROMBytes // streamed to disk, never buffered
				}
				r.Body = http.MaxBytesReader(w, r.Body, limit)
				next.ServeHTTP(w, r)
			})
		}},
		ErrorHandlerFunc: func(w http.ResponseWriter, r *http.Request, err error) {
			var ip *api.InvalidParamFormatError
			if errors.As(err, &ip) { // e.g. no UUID in the path: does not exist
				writeError(w, http.StatusNotFound, hub.CodeNotFound, "Not found")
				return
			}
			writeError(w, http.StatusBadRequest, hub.CodeBadRequest, "Invalid request")
		},
	})
	mux.HandleFunc("GET /api/v1/roms/{sha256}", s.downloadROM)
	mux.HandleFunc("GET /api/v1/systems/{system_id}/firmware/{file_id}", s.downloadFirmware)
	mux.HandleFunc("GET /api/v1/ws", s.serveWS)
}

// skipRomMux skips the generated routes for the ROM download and the WSS upgrade; both are registered
// directly on the mux.
type skipRomMux struct{ *http.ServeMux }

func (m skipRomMux) HandleFunc(pattern string, h func(http.ResponseWriter, *http.Request)) {
	if strings.Contains(pattern, "/api/v1/roms/") || strings.Contains(pattern, "/firmware/") || strings.HasSuffix(pattern, " /api/v1/ws") {
		return
	}
	m.ServeMux.HandleFunc(pattern, h)
}

func bearer(r *http.Request) string {
	h := r.Header.Get("Authorization")
	if len(h) > 7 && strings.EqualFold(h[:7], "bearer ") {
		return strings.TrimSpace(h[7:])
	}
	return ""
}

func remoteIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func (s *Server) authMiddleware(next api.StrictHandlerFunc, op string) api.StrictHandlerFunc {
	return func(ctx context.Context, w http.ResponseWriter, r *http.Request, req any) (any, error) {
		tok := bearer(r)
		ctx = context.WithValue(ctx, keyBearer, tok)
		ctx = context.WithValue(ctx, keyRemoteIP, remoteIP(r))
		ctx = hub.WithRequestHost(ctx, r.Host)
		switch op {
		case "UploadGame", "ListSystems", "RevokeSelf", "PostHandshake", "ListGames", "GetGame", "ConnectWebSocket",
			"ListSaves", "GetSaveSlot", "PutSave", "DownloadSaveContent", "ListSaveHistory", "DownloadSaveHistoryContent", "ResolveSaveConflict",
			"RestoreSaveHistoryVersion", "CreateSaveSnapshot", "DeleteSaveSnapshot", "UploadSaveFile", "GetCoresIndex", "GetCoresIndexSignature",
			"ListUsers", "GetCorePackage", "GetCorePackageFile", "ListSessions", "PublishSession", "GetSession", "UpdateSession", "EndSession", "InviteSessionUser",
			"WithdrawSessionInvite", "DeclineSession", "JoinSession", "RemoveSessionViewer":
			p, err := s.svc.Authenticate(ctx, tok)
			if err != nil {
				return nil, err
			}
			ctx = context.WithValue(ctx, keyPrincipal, p)
		}
		return next(ctx, w, r, req)
	}
}

func principal(ctx context.Context) hub.Principal {
	p, _ := ctx.Value(keyPrincipal).(hub.Principal)
	return p
}

func httpStatus(c hub.Code) int {
	switch c {
	case hub.CodeBadRequest:
		return http.StatusBadRequest
	case hub.CodeUnauthorized, hub.CodeDeviceRevoked, hub.CodeInvalidCredentials, hub.CodeUserDisabled:
		return http.StatusUnauthorized
	case hub.CodeForbidden, hub.CodeUploadsDisabled:
		return http.StatusForbidden
	case hub.CodeNotFound, hub.CodeInviteInvalid, hub.CodeCorePackageNotFound, hub.CodeCoreFileNotAvailable:
		return http.StatusNotFound
	case hub.CodeSessionForbidden:
		return http.StatusForbidden
	case hub.CodeSessionNotFound:
		return http.StatusNotFound
	case hub.CodeSessionEnded:
		return http.StatusGone
	case hub.CodeConflict, hub.CodePairingExpired, hub.CodeSaveConflict, hub.CodeSaveConflictStale,
		hub.CodeSessionFull, hub.CodeCapabilityMissing, hub.CodeDisplayNameTaken,
		hub.CodeSaveNotSnapshot, hub.CodeSaveSlotExists:
		return http.StatusConflict
	case hub.CodePayloadTooLarge:
		return http.StatusRequestEntityTooLarge
	case hub.CodeRateLimited:
		return http.StatusTooManyRequests
	}
	return http.StatusInternalServerError
}

func writeError(w http.ResponseWriter, status int, code hub.Code, msg string) {
	if status == http.StatusUnauthorized {
		w.Header().Set("WWW-Authenticate", "Bearer")
	}
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	var e api.Error
	e.Error.Code, e.Error.Message = api.ErrorCode(code), msg
	json.NewEncoder(w).Encode(e)
}

func (s *Server) writeErr(w http.ResponseWriter, r *http.Request, err error) {
	var he *hub.Error
	switch {
	case errors.As(err, &he):
		writeError(w, httpStatus(he.Code), he.Code, he.Message)
	case errors.Is(err, errNotImplemented):
		writeError(w, http.StatusNotImplemented, hub.CodeInternal, "Not implemented yet")
	default:
		var mbe *http.MaxBytesError
		if errors.As(err, &mbe) {
			writeError(w, http.StatusBadRequest, hub.CodeBadRequest, "Request too large")
			return
		}
		s.log.Error("internal error", "method", r.Method, "path", r.URL.Path, "err", err)
		writeError(w, http.StatusInternalServerError, hub.CodeInternal, "Internal error")
	}
}

// ---- StrictServerInterface ----

func (s *Server) GetHubInfo(_ context.Context, _ api.GetHubInfoRequestObject) (api.GetHubInfoResponseObject, error) {
	i := s.svc.Info()
	id, err := uuid.Parse(i.HubID)
	if err != nil {
		return nil, err
	}
	return api.GetHubInfo200JSONResponse{HubId: id, Name: i.Name, HubVersion: i.HubVersion,
		ProtocolVersion: i.ProtocolVersion, MinProtocolVersion: i.MinProtocolVersion, ApiBase: "/api/v1"}, nil
}

func (s *Server) CreatePairingRequest(ctx context.Context, req api.CreatePairingRequestRequestObject) (api.CreatePairingRequestResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	ip, _ := ctx.Value(keyRemoteIP).(string)
	c, err := s.svc.CreatePairingRequest(ctx, hub.PairingInput{DeviceID: b.DeviceId.String(), DeviceName: b.DeviceName,
		Platform: b.Platform, Arch: b.Arch, PlayerVersion: b.PlayerVersion, ProtocolVersion: b.ProtocolVersion, RemoteAddr: ip})
	if err != nil {
		return nil, err
	}
	id, err := uuid.Parse(c.RequestID)
	if err != nil {
		return nil, err
	}
	return api.CreatePairingRequest202JSONResponse{RequestId: id, PollToken: c.PollToken,
		Status: api.PairingRequestAcceptedStatus("pending"), ExpiresIn: int(c.ExpiresIn.Seconds())}, nil
}

func (s *Server) GetPairingRequest(ctx context.Context, req api.GetPairingRequestRequestObject) (api.GetPairingRequestResponseObject, error) {
	tok, _ := ctx.Value(keyBearer).(string)
	res, err := s.svc.PollPairing(ctx, req.RequestId.String(), tok)
	if err != nil {
		return nil, err
	}
	out := api.PairingStatus{Status: api.PairingStatusStatus(res.Status)}
	if res.Status == hub.PairingApproved {
		hid, err := uuid.Parse(res.HubID)
		if err != nil {
			return nil, err
		}
		out.HubId, out.UserId, out.DeviceCredential = &hid, &res.UserID, &res.DeviceCredential
	}
	return api.GetPairingRequest200JSONResponse(out), nil
}

func (s *Server) CreateAccessToken(ctx context.Context, req api.CreateAccessTokenRequestObject) (api.CreateAccessTokenResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	t, err := s.svc.IssueAccessToken(ctx, req.Body.DeviceId.String(), req.Body.DeviceCredential)
	if err != nil {
		return nil, err
	}
	return api.CreateAccessToken200JSONResponse{AccessToken: t.Token, TokenType: api.Bearer, ExpiresIn: int(t.ExpiresIn.Seconds())}, nil
}

func (s *Server) RevokeSelf(ctx context.Context, _ api.RevokeSelfRequestObject) (api.RevokeSelfResponseObject, error) {
	if err := s.svc.RevokeDevice(ctx, principal(ctx).Device.ID); err != nil {
		return nil, err
	}
	return api.RevokeSelf204Response{}, nil
}

func (s *Server) PostHandshake(ctx context.Context, req api.PostHandshakeRequestObject) (api.PostHandshakeResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	cores := make([]hub.CoreReport, 0, len(b.Cores))
	for _, c := range b.Cores {
		cores = append(cores, hub.CoreReport{ID: c.Id, Version: c.Version})
	}
	res, err := s.svc.Handshake(ctx, principal(ctx).Device.ID, hub.HandshakeInput{Platform: b.Platform, Arch: b.Arch,
		PlayerVersion: b.PlayerVersion, ProtocolVersion: b.ProtocolVersion, MinProtocolVersion: b.MinProtocolVersion,
		H264Encode: &b.Video.H264Encode, H264Decode: &b.Video.H264Decode, Cores: &cores})
	if err != nil {
		return nil, err
	}
	features := []string{hub.FeatureSavesV1, hub.FeatureSessionsV1, hub.FeatureUsersV1, hub.FeatureFirmwareV1, hub.FeatureCoresV1,
		hub.FeatureSavesV2, hub.FeatureCoresIndexV1, hub.FeatureSavesV3, hub.FeatureSavesV4}
	if s.svc.TURNEnabled() {
		features = append(features, hub.FeatureTURNV1)
	}
	if can, err := s.svc.CanUpload(ctx, principal(ctx).User); err != nil {
		return nil, err
	} else if can {
		features = append(features, hub.FeatureUploadsV1)
	}
	out := api.HandshakeResponse{HubVersion: res.Info.HubVersion, ProtocolVersion: res.Info.ProtocolVersion,
		MinProtocolVersion: res.Info.MinProtocolVersion, Compatible: res.Compatible, Problems: []api.HandshakeProblem{},
		Features: &features}
	if u := principal(ctx).User; u.ID != "" {
		out.User = &api.HandshakeUser{Id: u.ID, DisplayName: u.DisplayName, Role: api.HandshakeUserRole(u.Role)}
	}
	for _, p := range res.Problems {
		hp := api.HandshakeProblem{Code: api.HandshakeProblemCode(p.Code), Detail: p.Detail}
		if p.CoreID != "" {
			id := p.CoreID
			hp.CoreId = &id
		}
		out.Problems = append(out.Problems, hp)
	}
	return api.PostHandshake200JSONResponse(out), nil
}

func toAPIGame(g hub.Game) (api.Game, error) {
	id, err := uuid.Parse(g.ID)
	if err != nil {
		return api.Game{}, err
	}
	return api.Game{Id: id, Title: g.Title, System: g.System, UploadedBy: g.UploadedBy, AddedAt: g.AddedAt,
		Rom: api.RomInfo{Sha256: g.ROMSHA256, Size: g.ROMSize, Filename: g.Filename}}, nil
}

func (s *Server) ListGames(ctx context.Context, _ api.ListGamesRequestObject) (api.ListGamesResponseObject, error) {
	gs, err := s.svc.ListGames(ctx)
	if err != nil {
		return nil, err
	}
	out := api.ListGames200JSONResponse{Games: make([]api.Game, 0, len(gs))}
	for _, g := range gs {
		ag, err := toAPIGame(g)
		if err != nil {
			return nil, err
		}
		out.Games = append(out.Games, ag)
	}
	return out, nil
}

func (s *Server) GetGame(ctx context.Context, req api.GetGameRequestObject) (api.GetGameResponseObject, error) {
	g, err := s.svc.GetGame(ctx, req.GameId.String())
	if err != nil {
		return nil, err
	}
	ag, err := toAPIGame(g)
	if err != nil {
		return nil, err
	}
	return api.GetGame200JSONResponse(ag), nil
}

// DownloadRom is not used by the strict handler (the route lives on the mux, see downloadROM).
func (s *Server) DownloadRom(context.Context, api.DownloadRomRequestObject) (api.DownloadRomResponseObject, error) {
	return nil, errNotImplemented
}

// ConnectWebSocket: documented only; the upgrade is served by serveWS (ws.go) directly on the mux.
func (s *Server) ConnectWebSocket(context.Context, api.ConnectWebSocketRequestObject) (api.ConnectWebSocketResponseObject, error) {
	return nil, errNotImplemented
}

// ---- Firmware download (directly on the mux: ETag = sha256, If-None-Match; never logged) ----

func (s *Server) downloadFirmware(w http.ResponseWriter, r *http.Request) {
	if _, err := s.svc.Authenticate(r.Context(), bearer(r)); err != nil {
		s.writeErr(w, r, err)
		return
	}
	f, def, err := s.svc.OpenFirmware(r.Context(), r.PathValue("system_id"), r.PathValue("file_id"))
	if err != nil {
		s.writeErr(w, r, err)
		return
	}
	defer f.Close()
	h := w.Header()
	h.Set("Cache-Control", "private, no-cache")
	h.Set("Content-Type", "application/octet-stream")
	h.Set("ETag", `"`+def.SHA256+`"`)
	http.ServeContent(&jsonRangeErrWriter{ResponseWriter: w}, r, "", time.Time{}, f)
}

// GetFirmwareFile is not used by the strict handler (the route lives on the mux, see downloadFirmware).
func (s *Server) GetFirmwareFile(context.Context, api.GetFirmwareFileRequestObject) (api.GetFirmwareFileResponseObject, error) {
	return nil, errNotImplemented
}

// ---- ROM download (directly on the mux: Range, ETag, If-None-Match via http.ServeContent) ----

func (s *Server) downloadROM(w http.ResponseWriter, r *http.Request) {
	if _, err := s.svc.Authenticate(r.Context(), bearer(r)); err != nil {
		s.writeErr(w, r, err)
		return
	}
	f, g, err := s.svc.OpenROM(r.Context(), r.PathValue("sha256"))
	if err != nil {
		s.writeErr(w, r, err)
		return
	}
	defer f.Close()
	h := w.Header()
	h.Set("Cache-Control", "private, max-age=0, must-revalidate")
	h.Set("Content-Type", "application/octet-stream")
	h.Set("ETag", `"`+g.ROMSHA256+`"`)
	http.ServeContent(&jsonRangeErrWriter{ResponseWriter: w}, r, g.Filename, g.AddedAt, f)
}

// jsonRangeErrWriter replaces the plain-text error body of http.ServeContent on 416 with the spec's error format.
type jsonRangeErrWriter struct {
	http.ResponseWriter
	errBody bool
}

func (w *jsonRangeErrWriter) WriteHeader(code int) {
	if code == http.StatusRequestedRangeNotSatisfiable {
		w.errBody = true
		w.Header().Set("Content-Type", "application/json")
		w.Header().Del("X-Content-Type-Options")
	}
	w.ResponseWriter.WriteHeader(code)
}

func (w *jsonRangeErrWriter) Write(p []byte) (int, error) {
	if w.errBody {
		w.errBody = false
		var e api.Error
		e.Error.Code, e.Error.Message = api.ErrorCode(hub.CodeBadRequest), "Range not satisfiable"
		if err := json.NewEncoder(w.ResponseWriter).Encode(e); err != nil {
			return 0, err
		}
		return len(p), nil
	}
	return w.ResponseWriter.Write(p)
}

// LogRequests logs method, path, status and duration (never headers, query or bodies).
func LogRequests(log *slog.Logger, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		sw := &statusWriter{ResponseWriter: w, status: 200}
		next.ServeHTTP(sw, r)
		log.Info("request", "method", r.Method, "path", r.URL.Path, "status", sw.status, "dur_ms", time.Since(start).Milliseconds())
	})
}

type statusWriter struct {
	http.ResponseWriter
	status int
}

func (w *statusWriter) WriteHeader(c int) { w.status = c; w.ResponseWriter.WriteHeader(c) }

func (w *statusWriter) Unwrap() http.ResponseWriter { return w.ResponseWriter }

// ptr returns a pointer to v (optional response header fields in generated code are pointers).
func ptr[T any](v T) *T { return &v }
