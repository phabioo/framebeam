package httpapi

import (
	"context"
	"errors"
	"net/http"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/api"
	"github.com/phabioo/framebeam/server/internal/hub"
)

// RedeemInvite redeems an onboarding invite (no auth, rate limited).
func (s *Server) RedeemInvite(ctx context.Context, req api.RedeemInviteRequestObject) (api.RedeemInviteResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	ip, _ := ctx.Value(keyRemoteIP).(string)
	res, err := s.svc.RedeemInvite(ctx, hub.RedeemInput{Code: b.Code, DisplayName: b.DisplayName, DeviceID: b.DeviceId.String(),
		DeviceName: b.DeviceName, Platform: b.Platform, Arch: b.Arch, PlayerVersion: b.PlayerVersion,
		ProtocolVersion: b.ProtocolVersion, RemoteAddr: ip})
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

// ListSystems returns the registry with firmware status (no firmware bytes).
func (s *Server) ListSystems(ctx context.Context, _ api.ListSystemsRequestObject) (api.ListSystemsResponseObject, error) {
	reg, err := s.svc.ListRegistry(ctx)
	if err != nil {
		return nil, err
	}
	out := api.ListSystems200JSONResponse{Systems: make([]api.SystemInfo, 0, len(reg))}
	for _, e := range reg {
		si := api.SystemInfo{Id: e.ID, DisplayName: e.Name, PreferredCoreId: e.CoreID, FirmwareMode: api.FirmwareMode(e.FirmwareMode),
			Firmware: make([]api.FirmwareFile, 0, len(e.Firmware))}
		if e.ExpectedCoreVersion != "" {
			v := e.ExpectedCoreVersion
			si.ExpectedCoreVersion = &v
		}
		for _, f := range e.Firmware {
			af := api.FirmwareFile{Id: f.ID, DisplayName: f.DisplayName, Required: f.Required, Present: f.Present}
			if f.Present {
				sz, sha := f.Size, f.SHA256
				af.Size, af.Sha256 = &sz, &sha
			}
			si.Firmware = append(si.Firmware, af)
		}
		out.Systems = append(out.Systems, si)
	}
	return out, nil
}
