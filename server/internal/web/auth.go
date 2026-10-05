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
	d := s.base(r, nil, "", "Einrichtung")
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
		http.Error(w, "Einrichtung nur von dem Rechner aus möglich, auf dem der FrameBeam Hub läuft", http.StatusForbidden)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, maxFormBytes)
	if r.ParseForm() != nil || !s.checkPreCSRF(r) {
		http.Error(w, "CSRF-Prüfung fehlgeschlagen", http.StatusForbidden)
		return
	}
	d := s.base(r, nil, "", "Einrichtung")
	d.CSRF = s.preCSRF(w, r)
	d.Body = setupBody{Local: true, MinPassword: hub.MinPasswordLen}
	pw := r.PostFormValue("password")
	if pw != r.PostFormValue("password2") {
		d.Error = "Die Passwörter stimmen nicht überein."
		s.render(w, http.StatusBadRequest, "setup", "bare", d)
		return
	}
	u, err := s.svc.CreateAdmin(r.Context(), strings.TrimSpace(r.PostFormValue("username")), pw)
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
	d := s.base(r, nil, "", "Anmelden")
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
		http.Error(w, "CSRF-Prüfung fehlgeschlagen", http.StatusForbidden)
		return
	}
	ip := remoteIP(r)
	d := s.base(r, nil, "", "Anmelden")
	d.CSRF = s.preCSRF(w, r)
	if s.login.blocked(ip) {
		w.Header().Set("Retry-After", "60")
		d.Error = "Zu viele Fehlversuche. Bitte in einer Minute erneut versuchen."
		s.render(w, http.StatusTooManyRequests, "login", "bare", d)
		return
	}
	u, err := s.svc.VerifyPassword(r.Context(), r.PostFormValue("username"), r.PostFormValue("password"))
	if err == nil && u.Role == hub.RoleAdmin {
		if err := s.startSession(w, r, u.ID); err != nil {
			s.fail(w, r, err)
			return
		}
		http.Redirect(w, r, "/library", http.StatusSeeOther)
		return
	}
	if err != nil && !errors.Is(err, hub.ErrInvalidCredentials) {
		s.fail(w, r, err)
		return
	}
	s.login.fail(ip)
	d.Error = "Benutzername oder Passwort falsch."
	s.render(w, http.StatusUnauthorized, "login", "bare", d)
}

func (s *Server) logout(w http.ResponseWriter, r *http.Request, _ *session) {
	if c, err := r.Cookie(sessionCookie); err == nil {
		s.svc.DeleteWebSession(r.Context(), c.Value)
	}
	s.clearSession(w)
	http.Redirect(w, r, "/login", http.StatusSeeOther)
}
