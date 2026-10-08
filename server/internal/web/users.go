package web

import (
	"errors"
	"fmt"
	"net"
	"net/http"
	"strconv"
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
	// NewLink is the shareable link for NewCode (<scheme>://<host>/invite#<code>); the code stays in the URL fragment.
	NewLink string
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
	if newCode != "" {
		b.NewLink = s.scheme() + "://" + s.hubAddress(r) + "/invite#" + newCode
	}
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

func (s *Server) scheme() string {
	if s.cfg.UseTLS {
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
	d := s.base(r, nil, "", "Join this Hub")
	fp, _ := s.certInfo()
	d.Body = inviteLandingBody{Address: s.hubAddress(r), Fingerprint: fp}
	s.render(w, http.StatusOK, "invite", "bare", d)
}
