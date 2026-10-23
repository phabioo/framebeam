package httpapi

import (
	"context"
	"time"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Admin API for users, invites, pairing requests and devices (0.18, `admin_v1`). Every handler calls the same hub
// functions as the app; admin tokens only (401 without a token, 403 for users).

// pairingCodeTTL is the lifetime of the pairing code of a new user or a new code (like the app).
const pairingCodeTTL = 24 * time.Hour

func adminUserBody(u hub.User, devices int, lastSeen *time.Time) api.AdminUser {
	return api.AdminUser{Id: u.ID, Username: u.Username, DisplayName: u.DisplayName, Role: api.AdminUserRole(u.Role),
		Disabled: u.Disabled(), CreatedAt: u.CreatedAt, Devices: devices, LastSeenAt: lastSeen}
}

// lastSeenByUser is the newest connection of any device, per user.
func (s *Server) lastSeenByUser(ctx context.Context) (map[string]*time.Time, error) {
	devs, err := s.svc.ListDevices(ctx)
	if err != nil {
		return nil, err
	}
	out := map[string]*time.Time{}
	for _, d := range devs {
		if d.LastSeenAt != nil && (out[d.UserID] == nil || d.LastSeenAt.After(*out[d.UserID])) {
			out[d.UserID] = d.LastSeenAt
		}
	}
	return out, nil
}

// adminUserByID returns one user in the list form (ErrNotFound when unknown).
func (s *Server) adminUserByID(ctx context.Context, id string) (api.AdminUser, error) {
	rows, err := s.svc.ListUserRows(ctx)
	if err != nil {
		return api.AdminUser{}, err
	}
	seen, err := s.lastSeenByUser(ctx)
	if err != nil {
		return api.AdminUser{}, err
	}
	for _, r := range rows {
		if r.ID == id {
			return adminUserBody(r.User, r.Devices, seen[r.ID]), nil
		}
	}
	return api.AdminUser{}, hub.ErrNotFound
}

func (s *Server) ListAdminUsers(ctx context.Context, _ api.ListAdminUsersRequestObject) (api.ListAdminUsersResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	rows, err := s.svc.ListUserRows(ctx)
	if err != nil {
		return nil, err
	}
	seen, err := s.lastSeenByUser(ctx)
	if err != nil {
		return nil, err
	}
	out := api.AdminUserList{Users: make([]api.AdminUser, 0, len(rows))}
	for _, r := range rows {
		out.Users = append(out.Users, adminUserBody(r.User, r.Devices, seen[r.ID]))
	}
	return api.ListAdminUsers200JSONResponse(out), nil
}

func (s *Server) CreateAdminUser(ctx context.Context, req api.CreateAdminUserRequestObject) (api.CreateAdminUserResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	u, inv, code, err := s.svc.CreateUserWithCode(ctx, principal(ctx).User.ID, req.Body.DisplayName, pairingCodeTTL)
	if err != nil {
		return nil, err
	}
	return api.CreateAdminUser201JSONResponse(api.AdminUserCreated{User: adminUserBody(u, 0, nil),
		PairingCode: api.AdminCode{Code: code, ExpiresAt: inv.ExpiresAt}}), nil
}

func (s *Server) UpdateAdminUser(ctx context.Context, req api.UpdateAdminUserRequestObject) (api.UpdateAdminUserResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.RenameUser(ctx, req.UserId, req.Body.DisplayName); err != nil {
		return nil, err
	}
	u, err := s.adminUserByID(ctx, req.UserId)
	if err != nil {
		return nil, err
	}
	return api.UpdateAdminUser200JSONResponse(u), nil
}

func (s *Server) CreateAdminUserCode(ctx context.Context, req api.CreateAdminUserCodeRequestObject) (api.CreateAdminUserCodeResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	inv, code, err := s.svc.CreateCodeForUser(ctx, principal(ctx).User.ID, req.UserId, pairingCodeTTL)
	if err != nil {
		return nil, err
	}
	return api.CreateAdminUserCode201JSONResponse(api.AdminCode{Code: code, ExpiresAt: inv.ExpiresAt}), nil
}

func (s *Server) SetAdminUserRole(ctx context.Context, req api.SetAdminUserRoleRequestObject) (api.SetAdminUserRoleResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	role, password := hub.Role(req.Body.Role), ""
	if req.Body.Password != nil {
		password = *req.Body.Password
	}
	if role != hub.RoleUser && role != hub.RoleAdmin {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.SetUserRole(ctx, principal(ctx).User.ID, req.UserId, role, password); err != nil {
		return nil, err
	}
	u, err := s.adminUserByID(ctx, req.UserId)
	if err != nil {
		return nil, err
	}
	return api.SetAdminUserRole200JSONResponse(u), nil
}

func (s *Server) DisableAdminUser(ctx context.Context, req api.DisableAdminUserRequestObject) (api.DisableAdminUserResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.requireConfirmed(ctx); err != nil {
		return nil, err
	}
	if req.UserId == principal(ctx).User.ID {
		return nil, &hub.Error{Code: hub.CodeForbidden, Message: "You cannot disable yourself"}
	}
	if err := s.svc.DisableUser(ctx, req.UserId); err != nil {
		return nil, err
	}
	u, err := s.adminUserByID(ctx, req.UserId)
	if err != nil {
		return nil, err
	}
	return api.DisableAdminUser200JSONResponse(u), nil
}

func (s *Server) EnableAdminUser(ctx context.Context, req api.EnableAdminUserRequestObject) (api.EnableAdminUserResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.EnableUser(ctx, req.UserId); err != nil {
		return nil, err
	}
	u, err := s.adminUserByID(ctx, req.UserId)
	if err != nil {
		return nil, err
	}
	return api.EnableAdminUser200JSONResponse(u), nil
}

func (s *Server) DeleteAdminUser(ctx context.Context, req api.DeleteAdminUserRequestObject) (api.DeleteAdminUserResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.requireConfirmed(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DeleteUser(ctx, req.UserId, principal(ctx).User.ID); err != nil {
		return nil, err
	}
	return api.DeleteAdminUser204Response{}, nil
}

// ---- Invites ----

func adminInviteBody(i hub.Invite) api.AdminInvite {
	return api.AdminInvite{Id: i.ID, Status: api.AdminInviteStatus(i.Status), AuthorizeDevice: i.AuthorizeDevice, CreatedAt: i.CreatedAt,
		ExpiresAt: i.ExpiresAt, RedeemedAt: i.RedeemedAt, RevokedAt: i.RevokedAt, RedeemedBy: strPtr(i.RedeemedBy), ForUser: strPtr(i.ForUser)}
}

func (s *Server) ListAdminInvites(ctx context.Context, req api.ListAdminInvitesRequestObject) (api.ListAdminInvitesResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	limit := 60
	if req.Params.Limit != nil {
		limit = min(max(*req.Params.Limit, 1), 200)
	}
	list, err := s.svc.ListInvites(ctx, limit)
	if err != nil {
		return nil, err
	}
	out := api.AdminInviteList{Invites: make([]api.AdminInvite, 0, len(list))}
	for _, i := range list {
		out.Invites = append(out.Invites, adminInviteBody(i))
	}
	return api.ListAdminInvites200JSONResponse(out), nil
}

func (s *Server) CreateAdminInvite(ctx context.Context, req api.CreateAdminInviteRequestObject) (api.CreateAdminInviteResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	ttl, authorize := hub.DefaultInviteTTL, false
	if b := req.Body; b != nil {
		if b.Expiry != nil {
			d, ok := hub.ParseInviteTTL(string(*b.Expiry))
			if !ok {
				return nil, hub.ErrBadRequest
			}
			ttl = d
		}
		authorize = b.AuthorizeDevice != nil && *b.AuthorizeDevice
	}
	inv, code, err := s.svc.CreateInvite(ctx, principal(ctx).User.ID, ttl, authorize)
	if err != nil {
		return nil, err
	}
	return api.CreateAdminInvite201JSONResponse(api.AdminInviteCreated{Invite: adminInviteBody(inv),
		Code: api.AdminCode{Code: code, ExpiresAt: inv.ExpiresAt}}), nil
}

func (s *Server) RevokeAdminInvite(ctx context.Context, req api.RevokeAdminInviteRequestObject) (api.RevokeAdminInviteResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.RevokeInvite(ctx, req.InviteId); err != nil {
		return nil, err
	}
	return api.RevokeAdminInvite204Response{}, nil
}

// ---- Pairing requests and devices ----

func (s *Server) ListAdminPairingRequests(ctx context.Context, req api.ListAdminPairingRequestsRequestObject) (api.ListAdminPairingRequestsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	status := api.AdminPairingRequestStatus("pending")
	if p := req.Params.Status; p != nil {
		status = api.AdminPairingRequestStatus(*p)
	}
	var list []hub.PairingRequest
	var err error
	switch status {
	case "pending":
		list, err = s.svc.ListPendingRequests(ctx)
	case "denied":
		list, err = s.svc.ListDeniedRequests(ctx, 10)
	default:
		return nil, hub.ErrBadRequest
	}
	if err != nil {
		return nil, err
	}
	out := api.AdminPairingRequestList{Requests: make([]api.AdminPairingRequest, 0, len(list)), MinProtocolVersion: s.svc.Info().MinProtocolVersion}
	for _, q := range list {
		out.Requests = append(out.Requests, api.AdminPairingRequest{Id: q.ID, DeviceId: q.DeviceID, DeviceName: q.DeviceName, Platform: q.Platform,
			Arch: q.Arch, PlayerVersion: q.PlayerVersion, ProtocolVersion: q.ProtocolVersion, Status: status, UserId: strPtr(q.UserID),
			RemoteAddr: strPtr(q.RemoteAddr), CreatedAt: q.CreatedAt, ExpiresAt: q.ExpiresAt})
	}
	return api.ListAdminPairingRequests200JSONResponse(out), nil
}

func (s *Server) AllowAdminPairingRequest(ctx context.Context, req api.AllowAdminPairingRequestRequestObject) (api.AllowAdminPairingRequestResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.ApprovePairing(ctx, req.RequestId, req.Body.UserId); err != nil {
		return nil, err
	}
	return api.AllowAdminPairingRequest204Response{}, nil
}

func (s *Server) DenyAdminPairingRequest(ctx context.Context, req api.DenyAdminPairingRequestRequestObject) (api.DenyAdminPairingRequestResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DenyPairing(ctx, req.RequestId); err != nil {
		return nil, err
	}
	return api.DenyAdminPairingRequest204Response{}, nil
}

// deviceLookup holds what the device body needs besides the device itself.
type deviceLookup struct {
	names  map[string]string
	protos map[string]int
}

func (s *Server) deviceLookup(ctx context.Context) (deviceLookup, error) {
	users, err := s.svc.ListUsers(ctx)
	if err != nil {
		return deviceLookup{}, err
	}
	protos, err := s.svc.DeviceProtocols(ctx)
	if err != nil {
		return deviceLookup{}, err
	}
	l := deviceLookup{names: map[string]string{}, protos: protos}
	for _, u := range users {
		l.names[u.ID] = u.DisplayName
	}
	return l, nil
}

func (s *Server) adminDeviceBody(d hub.Device, l deviceLookup) api.AdminDevice {
	out := api.AdminDevice{Id: d.ID, UserId: d.UserID, UserName: l.names[d.UserID], Name: d.Name, Platform: d.Platform, Arch: d.Arch,
		PlayerVersion: d.PlayerVersion, Status: api.AdminDeviceStatus(d.Status), CreatedAt: d.CreatedAt, LastSeenAt: d.LastSeenAt, RevokedAt: d.RevokedAt}
	if p := l.protos[d.ID]; p > 0 {
		out.ProtocolVersion = &p
	}
	if d.Status != hub.DeviceRevoked {
		out.Online = s.svc.DeviceOnline(d.ID)
	}
	return out
}

func (s *Server) adminDeviceByID(ctx context.Context, id string) (api.AdminDevice, error) {
	d, err := s.svc.GetDevice(ctx, id)
	if err != nil {
		return api.AdminDevice{}, err
	}
	l, err := s.deviceLookup(ctx)
	if err != nil {
		return api.AdminDevice{}, err
	}
	return s.adminDeviceBody(d, l), nil
}

func (s *Server) ListAdminDevices(ctx context.Context, _ api.ListAdminDevicesRequestObject) (api.ListAdminDevicesResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	devs, err := s.svc.ListDevices(ctx)
	if err != nil {
		return nil, err
	}
	l, err := s.deviceLookup(ctx)
	if err != nil {
		return nil, err
	}
	out := api.AdminDeviceList{Devices: make([]api.AdminDevice, 0, len(devs)), MinProtocolVersion: s.svc.Info().MinProtocolVersion}
	for _, d := range devs {
		out.Devices = append(out.Devices, s.adminDeviceBody(d, l))
	}
	return api.ListAdminDevices200JSONResponse(out), nil
}

func (s *Server) RenameAdminDevice(ctx context.Context, req api.RenameAdminDeviceRequestObject) (api.RenameAdminDeviceResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := s.svc.RenameDevice(ctx, req.DeviceId, req.Body.Name); err != nil {
		return nil, err
	}
	d, err := s.adminDeviceByID(ctx, req.DeviceId)
	if err != nil {
		return nil, err
	}
	return api.RenameAdminDevice200JSONResponse(d), nil
}

func (s *Server) RevokeAdminDevice(ctx context.Context, req api.RevokeAdminDeviceRequestObject) (api.RevokeAdminDeviceResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.RevokeDevice(ctx, req.DeviceId); err != nil {
		return nil, err
	}
	d, err := s.adminDeviceByID(ctx, req.DeviceId)
	if err != nil {
		return nil, err
	}
	return api.RevokeAdminDevice200JSONResponse(d), nil
}

func (s *Server) DeleteAdminDevice(ctx context.Context, req api.DeleteAdminDeviceRequestObject) (api.DeleteAdminDeviceResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DeleteDevice(ctx, req.DeviceId); err != nil {
		return nil, err
	}
	return api.DeleteAdminDevice204Response{}, nil
}
