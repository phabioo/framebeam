package web

import (
	"errors"
	"fmt"
	"net/http"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type userView struct {
	ID, Name, Initial, Role, Created string
	Devices                          int
	Disabled, Admin                  bool
}

type inviteView struct {
	ID, Status, Label, Meta string
	Authorize               bool
}

type ttlOpt struct {
	Value, Label string
	Default      bool
}

type usersBody struct {
	Users   []userView
	Active  []inviteView
	History []inviteView
	TTLs    []ttlOpt
	// NewCode is the code of the invite created just now; it is shown exactly once.
	NewCode, NewMeta string
}

var ttlOptions = []ttlOpt{{"15m", "15 minutes", false}, {"1h", "1 hour", true}, {"24h", "24 hours", false}}

func ttlFromValue(v string) (time.Duration, bool) {
	switch v {
	case "15m":
		return 15 * time.Minute, true
	case "1h":
		return time.Hour, true
	case "24h":
		return 24 * time.Hour, true
	}
	return 0, false
}

// until formats "in 42 min" / "in 3 h".
func until(t, now time.Time) string {
	d := t.Sub(now)
	switch {
	case d < time.Minute:
		return "in less than a minute"
	case d < time.Hour:
		return fmt.Sprintf("in %d min", int(d.Minutes()))
	}
	return fmt.Sprintf("in %d h", int(d.Round(time.Hour).Hours()))
}

func (s *Server) renderUsers(w http.ResponseWriter, r *http.Request, sess *session, status int, newCode, newMeta string) {
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
	b := usersBody{TTLs: ttlOptions, NewCode: newCode, NewMeta: newMeta}
	for _, u := range rows {
		b.Users = append(b.Users, userView{ID: u.ID, Name: u.DisplayName, Initial: initial(u.DisplayName), Role: roleLabel(u.Role),
			Created: u.CreatedAt.Local().Format("02.01.2006"), Devices: u.Devices, Disabled: u.Disabled(), Admin: u.Role == hub.RoleAdmin})
	}
	history := 0
	for _, i := range invites {
		v := inviteView{ID: i.ID, Status: string(i.Status), Authorize: i.AuthorizeDevice}
		switch i.Status {
		case hub.InviteActive:
			v.Label, v.Meta = "Active", "expires "+until(i.ExpiresAt, now)
			b.Active = append(b.Active, v)
			continue
		case hub.InviteRedeemed:
			v.Label = "redeemed"
			v.Meta = "redeemed by " + i.RedeemedBy
			if i.RedeemedAt != nil {
				v.Meta += " · " + i.RedeemedAt.Local().Format("02.01.")
			}
		case hub.InviteRevoked:
			v.Label, v.Meta = "revoked", "revoked"
			if i.RevokedAt != nil {
				v.Meta += " · " + i.RevokedAt.Local().Format("02.01.")
			}
		default:
			v.Label, v.Meta = "expired", "expired · "+i.ExpiresAt.Local().Format("02.01.")
		}
		if history++; history <= 10 {
			b.History = append(b.History, v)
		}
	}
	d := s.base(r, sess, "users", "Users")
	d.Body = b
	switch {
	case isHX(r, "users-table"):
		d.Fragment = true
		s.render(w, status, "users", "users-table", d)
	case isHX(r, "users-invites"):
		d.Fragment = true
		s.render(w, status, "users", "users-invites", d)
	default:
		s.render(w, status, "users", "layout", d)
	}
}

func (s *Server) usersGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderUsers(w, r, sess, http.StatusOK, "", "")
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
	ttl, ok := ttlFromValue(r.PostFormValue("expiry"))
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

type confirmBody struct {
	Heading, Intro, Note, Action, Button, Cancel string
	Removes                                      []string
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
