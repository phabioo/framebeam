package httpapi

import (
	"context"
	"net"
	"net/http"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
)

// Local setup endpoints (0.9, feature local_setup_v1): for the Player on the same PC as the Hub.

// Option configures Register.
type Option func(*Server)

// WithRestart sets the function that restarts the server in-process; the local settings call it after a change
// of a value that is read at startup.
func WithRestart(f func()) Option { return func(s *Server) { s.restart = f } }

var errNotLoopback = &hub.Error{Code: hub.CodeForbidden, Message: "Only available from the machine the FrameBeam Hub runs on"}

// isLoopbackRemote reports whether the TCP peer address is a loopback address. Only RemoteAddr counts; headers such
// as X-Forwarded-For are never trusted.
func isLoopbackRemote(r *http.Request) bool {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func isLocalOp(op string) bool {
	switch op {
	case "GetLocalStatus", "LocalSetup", "LocalPair", "UpdateLocalSettings":
		return true
	}
	return false
}

func localStatusBody(st hub.LocalStatus) api.LocalStatus {
	return api.LocalStatus{AdminExists: st.AdminExists, NetworkSharing: st.NetworkSharing, ImportDir: st.ImportDir, HubVersion: st.HubVersion}
}

func (s *Server) GetLocalStatus(ctx context.Context, _ api.GetLocalStatusRequestObject) (api.GetLocalStatusResponseObject, error) {
	st, err := s.svc.LocalStatus(ctx)
	if err != nil {
		return nil, err
	}
	return api.GetLocalStatus200JSONResponse(localStatusBody(st)), nil
}

func localInput(ctx context.Context, b *api.LocalPairRequest) hub.LocalInput {
	ip, _ := ctx.Value(keyRemoteIP).(string)
	return hub.LocalInput{Username: b.Username, Password: b.Password, Device: hub.PairingInput{DeviceID: b.DeviceId.String(),
		DeviceName: b.DeviceName, Platform: b.Platform, Arch: b.Arch, PlayerVersion: b.PlayerVersion, ProtocolVersion: b.ProtocolVersion, RemoteAddr: ip}}
}

func (s *Server) localApproved(res hub.PairingResult, u hub.User, in hub.LocalInput, how string) (api.PairingStatus, error) {
	hid, err := uuid.Parse(res.HubID)
	if err != nil {
		return api.PairingStatus{}, err
	}
	// Same trace as an approved pairing; never the credential.
	s.log.Info("device paired locally without approval", "how", how, "user", u.Username, "device_id", in.Device.DeviceID, "device_name", in.Device.DeviceName)
	uid, cred := res.UserID, res.DeviceCredential
	return api.PairingStatus{Status: api.PairingStatusStatusApproved, HubId: &hid, UserId: &uid, DeviceCredential: &cred}, nil
}

func (s *Server) LocalSetup(ctx context.Context, req api.LocalSetupRequestObject) (api.LocalSetupResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	in := localInput(ctx, req.Body)
	res, u, err := s.svc.LocalSetup(ctx, in)
	if err != nil {
		return nil, err
	}
	out, err := s.localApproved(res, u, in, "setup")
	return api.LocalSetup200JSONResponse(out), err
}

func (s *Server) LocalPair(ctx context.Context, req api.LocalPairRequestObject) (api.LocalPairResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	in := localInput(ctx, req.Body)
	res, u, err := s.svc.LocalPair(ctx, in)
	if err != nil {
		return nil, err
	}
	out, err := s.localApproved(res, u, in, "pair")
	return api.LocalPair200JSONResponse(out), err
}

func (s *Server) UpdateLocalSettings(ctx context.Context, req api.UpdateLocalSettingsRequestObject) (api.UpdateLocalSettingsResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if principal(ctx).User.Role != hub.RoleAdmin {
		return nil, hub.ErrForbidden
	}
	st, changed, err := s.svc.UpdateLocalSettings(ctx, req.Body.NetworkSharing, req.Body.ImportDir)
	if err != nil {
		return nil, err
	}
	if changed {
		s.log.Info("local settings changed", "network_sharing", st.NetworkSharing, "import_dir", st.ImportDir, "admin", principal(ctx).User.Username)
		if s.restart != nil {
			s.restart()
		}
	}
	return api.UpdateLocalSettings200JSONResponse(localStatusBody(st)), nil
}
