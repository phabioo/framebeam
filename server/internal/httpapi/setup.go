package httpapi

import (
	"context"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Setup with the setup code (0.18 package 6, handshake feature setup_code_v1): the app on another PC sets up a server
// without a screen. No auth and no loopback rule: the setup code is the proof.

func (s *Server) SetupWithCode(ctx context.Context, req api.SetupWithCodeRequestObject) (api.SetupWithCodeResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	ip, _ := ctx.Value(keyRemoteIP).(string)
	in := hub.SetupCodeInput{Code: b.SetupCode, Username: b.Username, Password: b.Password,
		Device: hub.PairingInput{DeviceID: b.DeviceId.String(), DeviceName: b.DeviceName, Platform: b.Platform, Arch: b.Arch,
			PlayerVersion: b.PlayerVersion, ProtocolVersion: b.ProtocolVersion, RemoteAddr: ip}}
	if b.InstallId != nil {
		in.Device.InstallID = *b.InstallId
	}
	res, u, err := s.svc.SetupWithCode(ctx, in)
	if err != nil {
		return nil, err
	}
	hid, err := uuid.Parse(res.HubID)
	if err != nil {
		return nil, err
	}
	// Same trace as an approved pairing; never the code or the credential.
	s.log.Info("server set up with the setup code", "user", u.Username, "device_id", in.Device.DeviceID, "device_name", in.Device.DeviceName)
	uid, cred := res.UserID, res.DeviceCredential
	return api.SetupWithCode200JSONResponse(api.PairingStatus{Status: api.PairingStatusStatusApproved, HubId: &hid, UserId: &uid, DeviceCredential: &cred}), nil
}
