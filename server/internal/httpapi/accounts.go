package httpapi

import (
	"context"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Server accounts (0.18 package 4, feature accounts_v1): sign-in with a password, the own account, "Confirm it's
// you" and the own devices, plus the admin password reset and the password policy. Never log passwords.

// requireConfirmed is the "Confirm it's you" check of a dangerous step: 403 confirmation_required unless the
// calling device is confirmed. Call it after requireAdmin.
func (s *Server) requireConfirmed(ctx context.Context) error {
	p := principal(ctx)
	return s.svc.RequireConfirmed(ctx, p.User.ID, p.Device.ID)
}

func (s *Server) SignIn(ctx context.Context, req api.SignInRequestObject) (api.SignInResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	b := req.Body
	ip, _ := ctx.Value(keyRemoteIP).(string)
	in := hub.SignInInput{Username: b.Username, Password: b.Password, DeviceID: b.DeviceId.String(), DeviceName: b.DeviceName,
		Platform: b.Platform, Arch: b.Arch, AppVersion: b.AppVersion, ProtocolVersion: b.ProtocolVersion, RemoteAddr: ip}
	if b.InstallId != nil {
		in.InstallID = *b.InstallId
	}
	res, err := s.svc.SignIn(ctx, in)
	if err != nil {
		return nil, err
	}
	hid, err := uuid.Parse(res.HubID)
	if err != nil {
		return nil, err
	}
	return api.SignIn200JSONResponse{Status: api.PairingStatusStatusApproved, HubId: &hid, UserId: &res.UserID,
		DeviceCredential: &res.DeviceCredential}, nil
}

func toAPIAccount(a hub.Account) api.Account {
	return api.Account{UserId: a.UserID, Username: a.Username, DisplayName: a.DisplayName, Role: api.AccountRole(a.Role),
		HasPassword: a.HasPassword, PasswordRequired: a.PasswordRequired, MustChangePassword: a.MustChangePassword,
		ConfirmedUntil: a.ConfirmedUntil, ConfirmLockedUntil: a.ConfirmLockedUntil}
}

func (s *Server) account(ctx context.Context) (hub.Account, error) {
	p := principal(ctx)
	return s.svc.GetAccount(ctx, p.User.ID, p.Device.ID)
}

func (s *Server) GetAccount(ctx context.Context, _ api.GetAccountRequestObject) (api.GetAccountResponseObject, error) {
	a, err := s.account(ctx)
	if err != nil {
		return nil, err
	}
	return api.GetAccount200JSONResponse(toAPIAccount(a)), nil
}

func (s *Server) UpdateAccount(ctx context.Context, req api.UpdateAccountRequestObject) (api.UpdateAccountResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.RenameUser(ctx, principal(ctx).User.ID, req.Body.DisplayName); err != nil {
		return nil, err
	}
	a, err := s.account(ctx)
	if err != nil {
		return nil, err
	}
	return api.UpdateAccount200JSONResponse(toAPIAccount(a)), nil
}

func (s *Server) SetAccountPassword(ctx context.Context, req api.SetAccountPasswordRequestObject) (api.SetAccountPasswordResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	cur := ""
	if req.Body.CurrentPassword != nil {
		cur = *req.Body.CurrentPassword
	}
	p := principal(ctx)
	if err := s.svc.SetPassword(ctx, p.User.ID, p.Device.ID, cur, req.Body.NewPassword); err != nil {
		return nil, err
	}
	s.log.Info("password changed", "user", p.User.Username)
	return api.SetAccountPassword204Response{}, nil
}

func (s *Server) RemoveAccountPassword(ctx context.Context, req api.RemoveAccountPasswordRequestObject) (api.RemoveAccountPasswordResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	p := principal(ctx)
	if err := s.svc.RemovePassword(ctx, p.User.ID, p.Device.ID, req.Body.CurrentPassword); err != nil {
		return nil, err
	}
	s.log.Info("password removed", "user", p.User.Username)
	return api.RemoveAccountPassword204Response{}, nil
}

func (s *Server) ConfirmAccount(ctx context.Context, req api.ConfirmAccountRequestObject) (api.ConfirmAccountResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	secs := 0
	if req.Body.DurationSeconds != nil {
		secs = int(*req.Body.DurationSeconds)
		if secs == 0 { // an explicit 0 is not one of the allowed values
			return nil, hub.ErrBadRequest
		}
	}
	p := principal(ctx)
	until, err := s.svc.Confirm(ctx, p.User.ID, p.Device.ID, req.Body.Password, secs)
	if err != nil {
		return nil, err
	}
	return api.ConfirmAccount200JSONResponse{ConfirmedUntil: until}, nil
}

func (s *Server) EndAccountConfirmation(ctx context.Context, _ api.EndAccountConfirmationRequestObject) (api.EndAccountConfirmationResponseObject, error) {
	p := principal(ctx)
	if err := s.svc.EndConfirmation(ctx, p.User.ID, p.Device.ID); err != nil {
		return nil, err
	}
	return api.EndAccountConfirmation204Response{}, nil
}

func (s *Server) ListAccountDevices(ctx context.Context, _ api.ListAccountDevicesRequestObject) (api.ListAccountDevicesResponseObject, error) {
	p := principal(ctx)
	ds, err := s.svc.ListOwnDevices(ctx, p.User.ID, p.Device.ID)
	if err != nil {
		return nil, err
	}
	out := api.ListAccountDevices200JSONResponse{Devices: make([]api.AccountDevice, 0, len(ds))}
	for _, d := range ds {
		id, err := uuid.Parse(d.ID)
		if err != nil {
			return nil, err
		}
		ad := api.AccountDevice{DeviceId: id, Name: d.Name, Platform: d.Platform, Arch: d.Arch, AppVersion: d.PlayerVersion,
			CreatedAt: d.CreatedAt, LastSeenAt: d.LastSeenAt, SignInKind: api.AccountDeviceSignInKind(d.SignInKind),
			Status: api.AccountDeviceStatus(d.Status), Current: d.Current}
		if d.InstallID != "" {
			ad.InstallId = ptr(d.InstallID)
		}
		out.Devices = append(out.Devices, ad)
	}
	return out, nil
}

func (s *Server) SignOutAccountDevice(ctx context.Context, req api.SignOutAccountDeviceRequestObject) (api.SignOutAccountDeviceResponseObject, error) {
	if err := s.svc.SignOutDevice(ctx, principal(ctx).User.ID, req.DeviceId.String()); err != nil {
		return nil, err
	}
	return api.SignOutAccountDevice204Response{}, nil
}

func (s *Server) SignOutAllAccountDevices(ctx context.Context, req api.SignOutAllAccountDevicesRequestObject) (api.SignOutAllAccountDevicesResponseObject, error) {
	incl := req.Body != nil && req.Body.IncludeCurrent != nil && *req.Body.IncludeCurrent
	p := principal(ctx)
	n, err := s.svc.SignOutAllDevices(ctx, p.User.ID, p.Device.ID, incl)
	if err != nil {
		return nil, err
	}
	s.log.Info("devices signed out", "user", p.User.Username, "count", n)
	return api.SignOutAllAccountDevices200JSONResponse{SignedOut: n}, nil
}

func (s *Server) ResetAdminUserPassword(ctx context.Context, req api.ResetAdminUserPasswordRequestObject) (api.ResetAdminUserPasswordResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	p := principal(ctx)
	temp, err := s.svc.ResetPassword(ctx, p.User.ID, p.Device.ID, req.UserId)
	if err != nil {
		return nil, err
	}
	s.log.Warn("password reset", "user_id", req.UserId, "admin", p.User.Username) // the temporary password is never logged
	return api.ResetAdminUserPassword200JSONResponse{TemporaryPassword: temp, MustChangePassword: true}, nil
}

func (s *Server) GetAdminAccountSettings(ctx context.Context, _ api.GetAdminAccountSettingsRequestObject) (api.GetAdminAccountSettingsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	on, err := s.svc.RequirePasswords(ctx)
	if err != nil {
		return nil, err
	}
	return api.GetAdminAccountSettings200JSONResponse{RequirePasswords: on}, nil
}

func (s *Server) UpdateAdminAccountSettings(ctx context.Context, req api.UpdateAdminAccountSettingsRequestObject) (api.UpdateAdminAccountSettingsResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.requireConfirmed(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.SetRequirePasswords(ctx, req.Body.RequirePasswords); err != nil {
		return nil, err
	}
	policy := "optional for members"
	if req.Body.RequirePasswords {
		policy = "required for everyone"
	}
	s.log.Info("account policy changed", "policy", policy, "admin", principal(ctx).User.Username)
	return api.UpdateAdminAccountSettings200JSONResponse{RequirePasswords: req.Body.RequirePasswords}, nil
}
