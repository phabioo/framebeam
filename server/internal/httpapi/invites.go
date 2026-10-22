package httpapi

import (
	"context"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// RedeemInvite redeems an onboarding invite (no auth, rate limited).
func (s *Server) RedeemInvite(ctx context.Context, req api.RedeemInviteRequestObject) (api.RedeemInviteResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	ip, _ := ctx.Value(keyRemoteIP).(string)
	in := hub.RedeemInput{Code: b.Code, DisplayName: b.DisplayName, DeviceID: b.DeviceId.String(),
		DeviceName: b.DeviceName, Platform: b.Platform, Arch: b.Arch, PlayerVersion: b.PlayerVersion,
		ProtocolVersion: b.ProtocolVersion, RemoteAddr: ip}
	if b.Password != nil {
		in.Password = *b.Password
	}
	if b.InstallId != nil {
		in.InstallID = *b.InstallId
	}
	res, err := s.svc.RedeemInvite(ctx, in)
	if err != nil {
		return nil, err
	}
	if res.Approved {
		hid, err := uuid.Parse(res.HubID)
		if err != nil {
			return nil, err
		}
		return api.RedeemInvite200JSONResponse{Status: api.InviteRedeemApprovedStatusApproved, HubId: hid, UserId: res.UserID,
			DeviceCredential: res.Credential}, nil
	}
	id, err := uuid.Parse(res.RequestID)
	if err != nil {
		return nil, err
	}
	return api.RedeemInvite202JSONResponse{RequestId: id, PollToken: res.PollToken,
		Status: api.PairingRequestAcceptedStatus("pending"), ExpiresIn: int(res.ExpiresIn.Seconds())}, nil
}
