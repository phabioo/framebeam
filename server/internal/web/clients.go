package web

import (
	"errors"
	"net/http"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type pendingView struct {
	ID, Name, Platform, Arch, PlayerVersion, Ago string
	Protocol                                     int
	// AssignTo is the preselected user: the pre-assigned (invite) user, else the admin.
	AssignTo string
	TooOld   bool
	// MinProtocol is the Hub's minimum protocol version.
	MinProtocol int
}

type deviceView struct {
	ID, Name, User, Platform, Arch, PlayerVersion, LastSeen string
	Revoked                                                 bool
	// TooOld: the last reported protocol version is below the Hub minimum; MinProtocol is that minimum.
	TooOld      bool
	MinProtocol int
}

type userOpt struct{ ID, DisplayName string }

type clientsBody struct {
	Pending []pendingView
	Devices []deviceView
	Users   []userOpt
}

func (s *Server) clientsData(r *http.Request) (clientsBody, error) {
	ctx := r.Context()
	reqs, err := s.svc.ListPendingRequests(ctx)
	if err != nil {
		return clientsBody{}, err
	}
	devs, err := s.svc.ListDevices(ctx)
	if err != nil {
		return clientsBody{}, err
	}
	users, err := s.svc.ListUsers(ctx)
	if err != nil {
		return clientsBody{}, err
	}
	protos, err := s.svc.DeviceProtocols(ctx)
	if err != nil {
		return clientsBody{}, err
	}
	minProto := s.svc.Info().MinProtocolVersion
	now := s.svc.Now()
	var b clientsBody
	names := map[string]string{}
	adminID := ""
	for _, u := range users {
		names[u.ID] = u.DisplayName
		if u.Role == hub.RoleAdmin && adminID == "" {
			adminID = u.ID
		}
		if !u.Disabled() {
			b.Users = append(b.Users, userOpt{u.ID, u.DisplayName})
		}
	}
	for _, q := range reqs {
		assign := adminID
		if q.UserID != "" {
			assign = q.UserID
		}
		b.Pending = append(b.Pending, pendingView{ID: q.ID, Name: q.DeviceName, Platform: q.Platform, Arch: q.Arch,
			PlayerVersion: q.PlayerVersion, Protocol: q.ProtocolVersion, Ago: ago(q.CreatedAt, now), AssignTo: assign,
			TooOld: q.ProtocolVersion < minProto, MinProtocol: minProto})
	}
	for _, d := range devs {
		b.Devices = append(b.Devices, deviceView{ID: d.ID, Name: d.Name, User: names[d.UserID], Platform: d.Platform, Arch: d.Arch,
			PlayerVersion: d.PlayerVersion, LastSeen: lastSeen(d.LastSeenAt, now), Revoked: d.Status == hub.DeviceRevoked,
			TooOld: d.Status == hub.DeviceTrusted && protos[d.ID] > 0 && protos[d.ID] < minProto, MinProtocol: minProto})
	}
	return b, nil
}

// renderClients renders the page or (htmx, target #clients-body) only that region; flash/errMsg appear in the fragment.
func (s *Server) renderClients(w http.ResponseWriter, r *http.Request, sess *session, flash, errMsg string) {
	body, err := s.clientsData(r)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "clients", "Clients")
	d.Body = body
	if flash != "" {
		d.Flash = flash
	}
	d.Error = errMsg
	if isHX(r, "clients-body") {
		d.Fragment = true
		s.render(w, http.StatusOK, "clients", "clients-body", d)
		return
	}
	s.render(w, http.StatusOK, "clients", "layout", d)
}

func (s *Server) clientsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderClients(w, r, sess, "", "")
}

func decideMsg(err error) string {
	switch {
	case errors.Is(err, hub.ErrNotFound), errors.Is(err, hub.ErrPairingExpired), errors.Is(err, hub.ErrConflict):
		return "The request is no longer open (expired or already handled)."
	case errors.Is(err, hub.ErrBadRequest):
		return "Please select a valid user."
	}
	return ""
}

func (s *Server) clientAllow(w http.ResponseWriter, r *http.Request, sess *session) {
	err := s.svc.ApprovePairing(r.Context(), r.PathValue("id"), r.PostFormValue("user_id"))
	if err != nil {
		if m := decideMsg(err); m != "" {
			s.renderClients(w, r, sess, "", m)
			return
		}
		s.fail(w, r, err)
		return
	}
	s.renderClients(w, r, sess, "Device allowed. The player receives its credentials on its next poll.", "")
}

func (s *Server) clientDeny(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.DenyPairing(r.Context(), r.PathValue("id")); err != nil {
		if m := decideMsg(err); m != "" {
			s.renderClients(w, r, sess, "", m)
			return
		}
		s.fail(w, r, err)
		return
	}
	s.renderClients(w, r, sess, "Request denied.", "")
}

func (s *Server) clientRevoke(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.RevokeDevice(r.Context(), r.PathValue("id")); err != nil {
		if errors.Is(err, hub.ErrNotFound) {
			s.renderClients(w, r, sess, "", "Device not found.")
			return
		}
		s.fail(w, r, err)
		return
	}
	s.renderClients(w, r, sess, "Access revoked.", "")
}
