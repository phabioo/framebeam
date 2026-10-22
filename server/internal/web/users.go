package web

import (
	"errors"
	"fmt"
	"net"
	"net/http"
	"strconv"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

type userView struct {
	ID, Name, Initial, Role string
	Created                 When
	Devices                 int
	Disabled, Admin, Self   bool
	Rename                  bool // the inline rename form is open
	Status, StatusClass     string
}

type inviteView struct {
	ID, Status, Label, Meta, Title string
	Authorize                      bool
	At                             When
}

type ttlOpt struct {
	Value, Label string
	Default      bool
}

// newCodeView is a code that was just created and is shown exactly once.
type newCodeView struct {
	Title, Code, Meta, Link, Sub string
}

type usersBody struct {
	Users   []userView
	Active  []inviteView
	History []inviteView
	TTLs    []ttlOpt
	// NewCode is the code of the invite created just now; it is shown exactly once.
	NewCode, NewMeta string
	// NewLink is the shareable link for NewCode (<scheme>://<host>/invite#<code>); the code stays in the URL fragment.
	NewLink string
	// UserCode is the one-time pairing code of a user that was just created or got a new code.
	UserCode *newCodeView
	// Create opens the "Create user" form; CreateName and CreateErr are its value and refusal.
	Create     bool
	CreateName string
	CreateErr  string
	// Editing: an inline form is open, the table does not reload itself under it.
	Editing   bool
	RenameErr string
}

var ttlOptions = []ttlOpt{{"1h", "1 hour", true}, {"24h", "24 hours", false}, {"7d", "7 days", false}}

// until formats "in 42 min" / "in 3 h" / "in 5 days".
func until(t, now time.Time) string {
	d := t.Sub(now)
	switch {
	case d < time.Minute:
		return "in less than a minute"
	case d < time.Hour:
		return fmt.Sprintf("in %d min", int(d.Minutes()))
	case d < 48*time.Hour:
		return fmt.Sprintf("in %d h", int(d.Round(time.Hour).Hours()))
	}
	return fmt.Sprintf("in %d days", int(d.Round(24*time.Hour).Hours()/24))
}

// usersOpts selects what renderUsers opens on top of the list.
type usersOpts struct {
	Status     int
	UserCode   *newCodeView
	NewCode    string
	NewMeta    string
	Create     bool
	CreateName string
	CreateErr  string
	RenameErr  string
	Err        string // page-level refusal
}

func (s *Server) renderUsers(w http.ResponseWriter, r *http.Request, sess *session, status int, newCode, newMeta string) {
	s.renderUsersOpts(w, r, sess, usersOpts{Status: status, NewCode: newCode, NewMeta: newMeta})
}

func (s *Server) renderUsersOpts(w http.ResponseWriter, r *http.Request, sess *session, o usersOpts) {
	ctx := r.Context()
	rows, err := s.svc.ListUserRows(ctx)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	invites, err := s.svc.ListInvites(ctx, 60)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	now := s.svc.Now()
	b := usersBody{TTLs: ttlOptions, NewCode: o.NewCode, NewMeta: o.NewMeta, UserCode: o.UserCode, Create: o.Create || r.URL.Query().Get("create") != "",
		CreateName: o.CreateName, CreateErr: o.CreateErr, RenameErr: o.RenameErr}
	if o.NewCode != "" {
		b.NewLink = s.scheme(r) + "://" + s.hubAddress(r) + "/invite#" + o.NewCode
	}
	if b.UserCode != nil && b.UserCode.Code != "" {
		b.UserCode.Link = s.scheme(r) + "://" + s.hubAddress(r) + "/invite#" + b.UserCode.Code
	}
	renaming := r.URL.Query().Get("rename")
	for _, u := range rows {
		v := userView{ID: u.ID, Name: u.DisplayName, Initial: initial(u.DisplayName), Role: roleLabel(u.Role),
			Created: when(u.CreatedAt, now), Devices: u.Devices, Disabled: u.Disabled(), Admin: u.Role == hub.RoleAdmin, Self: u.ID == sess.User.ID,
			Status: "Active", StatusClass: "ok", Rename: renaming == u.ID || o.RenameErr != "" && renaming == u.ID}
		if v.Disabled {
			v.Status, v.StatusClass = "Disabled", ""
		}
		b.Editing = b.Editing || v.Rename
		b.Users = append(b.Users, v)
	}
	b.Editing = b.Editing || b.Create
	history := 0
	for _, i := range invites {
		v := inviteView{ID: i.ID, Status: string(i.Status), Authorize: i.AuthorizeDevice}
		switch i.Status {
		case hub.InviteActive:
			v.Label, v.Meta = "Active", "expires "+until(i.ExpiresAt, now)
			v.Title = "Invite"
			v.At = when(i.CreatedAt, now)
			if i.ForUser != "" {
				v.Title = "Pairing code for " + i.ForUser
			}
			b.Active = append(b.Active, v)
			continue
		case hub.InviteRedeemed:
			v.Label, v.Meta = "redeemed", "Redeemed"
			if i.RedeemedBy != "" {
				v.Meta = "Redeemed by " + i.RedeemedBy
			}
			if i.RedeemedAt != nil {
				v.At = when(*i.RedeemedAt, now)
			}
		case hub.InviteRevoked:
			v.Label, v.Meta = "revoked", "Revoked"
			if i.RevokedAt != nil {
				v.At = when(*i.RevokedAt, now)
			}
		default:
			v.Label, v.Meta = "expired", "Expired"
			v.At = when(i.ExpiresAt, now)
		}
		if history++; history <= 10 {
			b.History = append(b.History, v)
		}
	}
	d := s.base(r, sess, "users", "Users")
	d.InlineFlash = true
	d.Body = b
	if o.Err != "" {
		d.Error = o.Err
	}
	st := o.Status
	if st == 0 {
		st = http.StatusOK
	}
	switch {
	case isHX(r, "users-table"):
		d.Fragment = true
		s.render(w, st, "users", "users-table", d)
	case isHX(r, "users-invites"):
		d.Fragment = true
		s.render(w, st, "users", "users-invites", d)
	default:
		s.render(w, st, "users", "layout", d)
	}
}

func (s *Server) usersGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderUsers(w, r, sess, http.StatusOK, "", "")
}

// hubMessage returns the user-facing message of a refused admin action.
func hubMessage(err error) (string, bool) {
	var he *hub.Error
	if errors.As(err, &he) {
		switch he.Code {
		case hub.CodeBadRequest, hub.CodeForbidden, hub.CodeDisplayNameTaken:
			return he.Message + ".", true
		}
	}
	return "", false
}

// userCreate creates a regular user together with a one-time pairing code (design 3n-2 "Create user"). The code is
// shown once in the response.
func (s *Server) userCreate(w http.ResponseWriter, r *http.Request, sess *session) {
	name := r.PostFormValue("name")
	u, inv, code, err := s.svc.CreateUserWithCode(r.Context(), sess.User.ID, name, 24*time.Hour)
	if err != nil {
		if m, ok := hubMessage(err); ok {
			s.renderUsersOpts(w, r, sess, usersOpts{Status: http.StatusBadRequest, Create: true, CreateName: name, CreateErr: m})
			return
		}
		s.fail(w, r, err)
		return
	}
	s.renderUsersOpts(w, r, sess, usersOpts{UserCode: &newCodeView{Title: "Pairing code for " + u.DisplayName, Code: code,
		Meta: "expires " + until(inv.ExpiresAt, s.svc.Now()), Sub: "Hand this code over personally. The first device that enters it is paired with " + u.DisplayName + " directly."}})
}

// userCodeNew gives an existing regular user a new one-time pairing code.
func (s *Server) userCodeNew(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	u, err := s.svc.GetUser(r.Context(), id)
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	inv, code, err := s.svc.CreateCodeForUser(r.Context(), sess.User.ID, id, 24*time.Hour)
	if err != nil {
		if m, ok := hubMessage(err); ok {
			s.renderUsersOpts(w, r, sess, usersOpts{Status: http.StatusBadRequest, Err: m})
			return
		}
		s.fail(w, r, err)
		return
	}
	s.renderUsersOpts(w, r, sess, usersOpts{UserCode: &newCodeView{Title: "Pairing code for " + u.DisplayName, Code: code,
		Meta: "expires " + until(inv.ExpiresAt, s.svc.Now()), Sub: "Hand this code over personally. The first device that enters it is paired with " + u.DisplayName + " directly."}})
}

// userRename changes the display name from the inline form.
func (s *Server) userRename(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	switch err := s.svc.RenameUser(r.Context(), id, r.PostFormValue("name")); {
	case err == nil:
		http.Redirect(w, r, "/users?ok=userrenamed", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
	default:
		if m, ok := hubMessage(err); ok {
			q := r.URL.Query()
			q.Set("rename", id)
			r.URL.RawQuery = q.Encode()
			s.renderUsersOpts(w, r, sess, usersOpts{Status: http.StatusBadRequest, RenameErr: m})
			return
		}
		s.fail(w, r, err)
	}
}

// userRoleUser turns an admin into a regular user (Role pill menu). Making an admin needs a password: userAdminForm.
func (s *Server) userRoleUser(w http.ResponseWriter, r *http.Request, sess *session) {
	switch err := s.svc.SetUserRole(r.Context(), sess.User.ID, r.PathValue("id"), hub.RoleUser, ""); {
	case err == nil:
		http.Redirect(w, r, "/users?ok=userrole", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
	default:
		if _, ok := hubMessage(err); ok {
			http.Redirect(w, r, "/users?err=rolekept", http.StatusSeeOther)
			return
		}
		s.fail(w, r, err)
	}
}

// userAdminForm asks for the password of the new admin (the web interface signs in with username and password).
func (s *Server) userAdminForm(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderAdminForm(w, r, sess, "")
}

func (s *Server) renderAdminForm(w http.ResponseWriter, r *http.Request, sess *session, errMsg string) {
	u, err := s.svc.GetUser(r.Context(), r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	if u.Role == hub.RoleAdmin || u.Disabled() {
		http.Redirect(w, r, "/users?err=rolekept", http.StatusSeeOther)
		return
	}
	d := s.base(r, sess, "users", "Make admin")
	d.Error = errMsg
	d.Body = confirmBody{
		Heading: "Make " + u.DisplayName + " an admin?",
		Intro: "Admins sign in to this web interface with the username “" + u.Username + "” and a password, and can change everything on this server. " +
			"Choose a password of at least " + fmt.Sprint(hub.MinPasswordLen) + " characters and hand it over personally.",
		Action: "/users/" + u.ID + "/admin", Button: "Make admin", Cancel: "/users", Password: true, Safe: true, PasswordMin: hub.MinPasswordLen,
	}
	status := http.StatusOK
	if errMsg != "" {
		status = http.StatusBadRequest
	}
	s.render(w, status, "confirm", "layout", d)
}

func (s *Server) userAdmin(w http.ResponseWriter, r *http.Request, sess *session) {
	switch err := s.svc.SetUserRole(r.Context(), sess.User.ID, r.PathValue("id"), hub.RoleAdmin, r.PostFormValue("password")); {
	case err == nil:
		http.Redirect(w, r, "/users?ok=userrole", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
	default:
		if m, ok := hubMessage(err); ok {
			s.renderAdminForm(w, r, sess, m)
			return
		}
		s.fail(w, r, err)
	}
}

func (s *Server) userDisable(w http.ResponseWriter, r *http.Request, sess *session) {
	id := r.PathValue("id")
	if id == sess.User.ID {
		http.Redirect(w, r, "/users?err=admin", http.StatusSeeOther)
		return
	}
	s.userToggle(w, r, s.svc.DisableUser(r.Context(), id), "disabled")
}

func (s *Server) userEnable(w http.ResponseWriter, r *http.Request, sess *session) {
	s.userToggle(w, r, s.svc.EnableUser(r.Context(), r.PathValue("id")), "enabled")
}

func (s *Server) userToggle(w http.ResponseWriter, r *http.Request, err error, ok string) {
	switch {
	case err == nil:
		http.Redirect(w, r, "/users?ok="+ok, http.StatusSeeOther)
	case errors.Is(err, hub.ErrForbidden):
		http.Redirect(w, r, "/users?err=admin", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
	default:
		s.fail(w, r, err)
	}
}

// inviteCreate creates an invite; the code is only shown in this response (only its hash is stored).
func (s *Server) inviteCreate(w http.ResponseWriter, r *http.Request, sess *session) {
	ttl, ok := hub.ParseInviteTTL(r.PostFormValue("expiry"))
	if !ok {
		ttl = hub.DefaultInviteTTL
	}
	authorize := r.PostFormValue("authorize") == "1"
	inv, code, err := s.svc.CreateInvite(r.Context(), sess.User.ID, ttl, authorize)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	meta := "expires " + until(inv.ExpiresAt, s.svc.Now())
	s.renderUsers(w, r, sess, http.StatusOK, code, meta)
}

// inviteRevokeConfirm asks before an open invite is revoked.
func (s *Server) inviteRevokeConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	d := s.base(r, sess, "users", "Revoke invite")
	d.Body = confirmBody{
		Heading: "Revoke this invite?",
		Intro:   "The code stops working at once. Nobody who has not entered it yet can use it to join this server.",
		Action:  "/users/invites/" + r.PathValue("id") + "/revoke", Button: "Revoke invite", Cancel: "/users",
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) inviteRevoke(w http.ResponseWriter, r *http.Request, sess *session) {
	switch err := s.svc.RevokeInvite(r.Context(), r.PathValue("id")); {
	case err == nil:
		http.Redirect(w, r, "/users?ok=revoked", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/users?err=noinvite", http.StatusSeeOther)
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) scheme(r *http.Request) string {
	if s.secureReq(r) {
		return "https"
	}
	return "http"
}

// hubAddress is the host[:port] players should use: the configured public host (with its external port when
// known), else the host of the request.
func (s *Server) hubAddress(r *http.Request) string {
	if h := s.cfg.PublicHost; h != "" {
		if p := s.cfg.PublicPort; p > 0 {
			return net.JoinHostPort(h, strconv.Itoa(p))
		}
		return h
	}
	return r.Host
}

type inviteLandingBody struct {
	Address, Fingerprint string
}

// inviteLanding is the public page behind an invite link. The code lives only in the URL fragment, which
// browsers never send to the server; a static script shows it. The page carries no admin data.
func (s *Server) inviteLanding(w http.ResponseWriter, r *http.Request) {
	d := s.base(r, nil, "", "Join this server")
	fp, _ := s.certInfo()
	d.Body = inviteLandingBody{Address: s.hubAddress(r), Fingerprint: fp}
	s.render(w, http.StatusOK, "invite", "bare", d)
}

type confirmBody struct {
	Heading, Intro, Note, Action, Button, Cancel string
	Removes                                      []string
	// Password adds a new-password field; Safe makes the button a normal (not red) action.
	Password, Safe bool
	PasswordMin    int
}

// userDeleteConfirm is the no-JS confirmation step that names everything the deletion removes.
func (s *Server) userDeleteConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	rows, err := s.svc.ListUserRows(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	id := r.PathValue("id")
	for _, u := range rows {
		if u.ID != id {
			continue
		}
		if u.Role == hub.RoleAdmin || u.ID == sess.User.ID {
			http.Redirect(w, r, "/users?err=admin", http.StatusSeeOther)
			return
		}
		d := s.base(r, sess, "users", "Delete user")
		d.Body = confirmBody{
			Heading: "Delete “" + u.DisplayName + "”?",
			Intro:   "This permanently deletes the user and cannot be undone. The following is removed:",
			Removes: []string{
				"The account and sign-in of " + u.DisplayName,
				"All their devices and access tokens (" + fmt.Sprint(u.Devices) + " trusted now); running Sessions end",
				"All their saves and save history: these are deleted for good",
				"Invites they created",
			},
			Note:   "Games they uploaded stay in the library and are reassigned to you.",
			Action: "/users/" + u.ID + "/delete", Button: "Delete user", Cancel: "/users",
		}
		s.render(w, http.StatusOK, "confirm", "layout", d)
		return
	}
	http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
}

func (s *Server) userDelete(w http.ResponseWriter, r *http.Request, sess *session) {
	switch err := s.svc.DeleteUser(r.Context(), r.PathValue("id"), sess.User.ID); {
	case err == nil:
		http.Redirect(w, r, "/users?ok=userdeleted", http.StatusSeeOther)
	case errors.Is(err, hub.ErrForbidden):
		http.Redirect(w, r, "/users?err=admin", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/users?err=nouser", http.StatusSeeOther)
	default:
		s.fail(w, r, err)
	}
}
