package web

import (
	"errors"
	"net/http"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type pendingView struct {
	ID, Name, Platform, Arch, PlayerVersion string
	Ago                                     When
	Protocol                                int
	// Title is "Sam on Lena-PC" for a pre-assigned user, else the device name.
	Title string
	// AssignTo is the preselected user: the pre-assigned (invite) user, else the admin. Fixed: the user is pre-assigned.
	AssignTo string
	Fixed    bool
	TooOld   bool
	// MinProtocol is the Hub's minimum protocol version.
	MinProtocol int
}

type deviceView struct {
	ID, Name, User, Platform, Arch, PlayerVersion string
	LastSeen                                      When
	Online                                        bool
	Revoked                                       bool
	Denied                                        bool // a denied pairing request: the device never connected
	Status, StatusClass                           string
	Added                                         When
	Rename, Details                               bool // inline rename form / details row open
	Remote                                        string
	// TooOld: the last reported protocol version is below the Hub minimum; MinProtocol is that minimum.
	TooOld      bool
	MinProtocol int
}

type userOpt struct{ ID, DisplayName string }

type clientsBody struct {
	Pending []pendingView
	Devices []deviceView
	Users   []userOpt
	// Editing is true while an inline rename or details row is open: the region does not reload itself then.
	Editing bool
	// RenameErr is the refusal shown in the open rename form.
	RenameErr string
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
		assign, fixed := adminID, false
		if q.UserID != "" {
			assign, fixed = q.UserID, true
		}
		title := q.DeviceName
		if fixed && names[q.UserID] != "" {
			title = names[q.UserID] + " on " + q.DeviceName
		}
		b.Pending = append(b.Pending, pendingView{ID: q.ID, Name: q.DeviceName, Title: title, Platform: q.Platform, Arch: q.Arch,
			PlayerVersion: q.PlayerVersion, Protocol: q.ProtocolVersion, Ago: when(q.CreatedAt, now), AssignTo: assign, Fixed: fixed,
			TooOld: q.ProtocolVersion < minProto, MinProtocol: minProto})
	}
	qv := r.URL.Query()
	for _, d := range devs {
		v := deviceView{ID: d.ID, Name: d.Name, User: names[d.UserID], Platform: d.Platform, Arch: d.Arch,
			PlayerVersion: d.PlayerVersion, LastSeen: whenPtr(d.LastSeenAt, now), Revoked: d.Status == hub.DeviceRevoked,
			Added: when(d.CreatedAt, now), Status: "Trusted", StatusClass: "ok",
			TooOld: d.Status == hub.DeviceTrusted && protos[d.ID] > 0 && protos[d.ID] < minProto, MinProtocol: minProto,
			Rename: qv.Get("rename") == d.ID, Details: qv.Get("details") == d.ID}
		if v.Revoked {
			v.Status, v.StatusClass = "Revoked", ""
		} else {
			v.Online = s.svc.DeviceOnline(d.ID)
		}
		if v.LastSeen.Text == "never" {
			v.LastSeen.Text = "Never connected"
		}
		b.Devices = append(b.Devices, v)
	}
	for _, d := range b.Devices {
		b.Editing = b.Editing || d.Rename
	}
	denied, err := s.svc.ListDeniedRequests(ctx, 10)
	if err != nil {
		return clientsBody{}, err
	}
	for _, q := range denied {
		b.Devices = append(b.Devices, deviceView{ID: q.ID, Name: q.DeviceName, User: names[q.UserID], Platform: q.Platform, Arch: q.Arch,
			PlayerVersion: q.PlayerVersion, LastSeen: When{Text: "Never connected"}, Denied: true, Status: "Denied", Added: when(q.CreatedAt, now),
			Details: qv.Get("details") == q.ID, Remote: q.RemoteAddr})
	}
	return b, nil
}

// renderClients renders the page or (htmx, target #clients-body) only that region; flash/errMsg appear in the fragment.
func (s *Server) renderClients(w http.ResponseWriter, r *http.Request, sess *session, flash, errMsg string) {
	s.renderClientsStatus(w, r, sess, http.StatusOK, flash, errMsg, "")
}

// renderClientsErr renders the list with a refused inline rename (the form stays open with the reason).
func (s *Server) renderClientsErr(w http.ResponseWriter, r *http.Request, sess *session, status int, renameErr string) {
	s.renderClientsStatus(w, r, sess, status, "", "", renameErr)
}

func (s *Server) renderClientsStatus(w http.ResponseWriter, r *http.Request, sess *session, status int, flash, errMsg, renameErr string) {
	body, err := s.clientsData(r)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	body.RenameErr = renameErr
	d := s.base(r, sess, "clients", "Clients")
	d.Body = body
	if flash != "" {
		d.Flash = flash
	}
	d.Error = errMsg
	if isHX(r, "clients-body") {
		d.Fragment = true
		s.render(w, status, "clients", "clients-body", d)
		return
	}
	s.render(w, status, "clients", "layout", d)
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
			if r.Header.Get("HX-Request") != "true" {
				http.Redirect(w, r, "/clients?err=nodevice", http.StatusSeeOther)
				return
			}
			s.renderClients(w, r, sess, "", "Device not found.")
			return
		}
		s.fail(w, r, err)
		return
	}
	if r.Header.Get("HX-Request") != "true" {
		http.Redirect(w, r, "/clients?ok=devrevoked", http.StatusSeeOther)
		return
	}
	s.renderClients(w, r, sess, "Access revoked.", "")
}

// clientRevokeConfirm is the confirmation step of "Revoke access…" (design 3m-2 dialogs).
func (s *Server) clientRevokeConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	dev, err := s.svc.GetDevice(r.Context(), r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/clients?err=nodevice", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	owner := "The device"
	if u, err := s.svc.GetUser(r.Context(), dev.UserID); err == nil {
		owner = u.DisplayName + "’s " + dev.Name
	}
	d := s.base(r, sess, "clients", "Revoke access")
	d.Body = confirmBody{
		Heading: "Revoke access for " + dev.Name + "?",
		Intro:   owner + " loses access at once, also with tokens that are still valid. Saves stay on the Hub. The device can ask again later.",
		Action:  "/clients/devices/" + dev.ID + "/revoke", Button: "Revoke access", Cancel: "/clients",
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

// clientRename changes the device name from the inline form. A refused name keeps the form open with the reason.
func (s *Server) clientRename(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	err := s.svc.RenameDevice(r.Context(), id, r.PostFormValue("name"))
	var he *hub.Error
	switch {
	case err == nil:
		if r.Header.Get("HX-Request") != "true" {
			http.Redirect(w, r, "/clients?ok=devrenamed", http.StatusSeeOther)
			return
		}
		s.renderClients(w, r, sess, "Device renamed.", "")
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/clients?err=nodevice", http.StatusSeeOther)
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		q := r.URL.Query()
		q.Set("rename", id)
		r.URL.RawQuery = q.Encode()
		w.Header().Set("X-Rename-Error", "1")
		s.renderClientsErr(w, r, sess, http.StatusBadRequest, he.Message+".")
	default:
		s.fail(w, r, err)
	}
}

// clientDeleteConfirm is the no-JS confirmation step that names what the deletion removes.
func (s *Server) clientDeleteConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	dev, err := s.svc.GetDevice(r.Context(), r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/clients?err=nodevice", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	owner := ""
	if u, err := s.svc.GetUser(r.Context(), dev.UserID); err == nil {
		owner = " of " + u.DisplayName
	}
	d := s.base(r, sess, "clients", "Delete device")
	d.Body = confirmBody{
		Heading: "Delete “" + dev.Name + "”?",
		Intro:   "This permanently deletes the device" + owner + " and cannot be undone. The following is removed:",
		Removes: []string{
			"The device and all its access tokens; a running Session or connection ends immediately",
			"The player has to be paired and approved again to use this Hub",
		},
		Note:   "Saves stay untouched; in the save history this device is then shown as “Deleted device”.",
		Action: "/clients/devices/" + dev.ID + "/delete", Button: "Delete device", Cancel: "/clients",
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) clientDelete(w http.ResponseWriter, r *http.Request, sess *session) {
	switch err := s.svc.DeleteDevice(r.Context(), r.PathValue("id")); {
	case err == nil:
		http.Redirect(w, r, "/clients?ok=devdeleted", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/clients?err=nodevice", http.StatusSeeOther)
	default:
		s.fail(w, r, err)
	}
}
