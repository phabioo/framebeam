package web

import (
	"errors"
	"net/http"
	"strings"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type setupBody struct {
	Local       bool
	MinPassword int
}

func (s *Server) setupGet(w http.ResponseWriter, r *http.Request) {
	if has, err := s.svc.HasAdmin(r.Context()); err != nil {
		s.fail(w, r, err)
		return
	} else if has {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return
	}
	d := s.base(r, nil, "", "Setup")
	d.CSRF = s.preCSRF(w, r)
	d.Body = setupBody{Local: isLoopback(r), MinPassword: hub.MinPasswordLen}
	s.render(w, http.StatusOK, "setup", "bare", d)
}

func (s *Server) setupPost(w http.ResponseWriter, r *http.Request) {
	if has, err := s.svc.HasAdmin(r.Context()); err != nil {
		s.fail(w, r, err)
		return
	} else if has {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return
	}
	if !isLoopback(r) {
		http.Error(w, "Setup is possible only from the machine the FrameBeam Hub runs on", http.StatusForbidden)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, maxFormBytes)
	if r.ParseForm() != nil || !s.checkPreCSRF(r) {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return
	}
	d := s.base(r, nil, "", "Setup")
	d.CSRF = s.preCSRF(w, r)
	d.Body = setupBody{Local: true, MinPassword: hub.MinPasswordLen}
	pw := r.PostFormValue("password")
	if pw != r.PostFormValue("password2") {
		d.Error = "The passwords do not match."
		s.render(w, http.StatusBadRequest, "setup", "bare", d)
		return
	}
	release, ok := s.acquireVerify(r.Context())
	if !ok {
		busy(w)
		return
	}
	u, err := s.svc.CreateAdmin(r.Context(), strings.TrimSpace(r.PostFormValue("username")), pw)
	release()
	if err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			d.Error = he.Message
			s.render(w, http.StatusBadRequest, "setup", "bare", d)
			return
		}
		s.fail(w, r, err)
		return
	}
	if err := s.startSession(w, r, u.ID); err != nil {
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/library", http.StatusSeeOther)
}

func (s *Server) loginGet(w http.ResponseWriter, r *http.Request) {
	if has, err := s.svc.HasAdmin(r.Context()); err != nil {
		s.fail(w, r, err)
		return
	} else if !has {
		http.Redirect(w, r, "/setup", http.StatusSeeOther)
		return
	}
	d := s.base(r, nil, "", "Sign in")
	d.CSRF = s.preCSRF(w, r)
	s.render(w, http.StatusOK, "login", "bare", d)
}

func (s *Server) loginPost(w http.ResponseWriter, r *http.Request) {
	if has, err := s.svc.HasAdmin(r.Context()); err != nil {
		s.fail(w, r, err)
		return
	} else if !has {
		http.Redirect(w, r, "/setup", http.StatusSeeOther)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, maxFormBytes)
	if r.ParseForm() != nil || !s.checkPreCSRF(r) {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return
	}
	ip := remoteIP(r)
	user := r.PostFormValue("username")
	d := s.base(r, nil, "", "Sign in")
	d.CSRF = s.preCSRF(w, r)
	// Reserve the attempt before the (expensive) verification; a success refunds it.
	if !s.login.take(ip, user) {
		w.Header().Set("Retry-After", "60")
		d.Error = "Too many failed attempts. Please try again in a minute."
		s.render(w, http.StatusTooManyRequests, "login", "bare", d)
		return
	}
	release, ok := s.acquireVerify(r.Context())
	if !ok {
		s.login.refund(ip, user)
		busy(w)
		return
	}
	u, err := s.svc.VerifyPassword(r.Context(), user, r.PostFormValue("password"))
	release()
	if err == nil && u.Role == hub.RoleAdmin {
		s.login.success(ip, user)
		if err := s.startSession(w, r, u.ID); err != nil {
			s.fail(w, r, err)
			return
		}
		http.Redirect(w, r, "/library", http.StatusSeeOther)
		return
	}
	if err != nil && !errors.Is(err, hub.ErrInvalidCredentials) {
		s.login.refund(ip, user)
		s.fail(w, r, err)
		return
	}
	d.Error = "Username or password is incorrect."
	s.render(w, http.StatusUnauthorized, "login", "bare", d)
}

func (s *Server) logout(w http.ResponseWriter, r *http.Request, _ *session) {
	if c, err := r.Cookie(s.cookieName(sessionCookie)); err == nil {
		s.svc.DeleteWebSession(r.Context(), c.Value)
	}
	s.clearSession(w)
	http.Redirect(w, r, "/login", http.StatusSeeOther)
}
