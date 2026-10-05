package web

import (
	"errors"
	"net/http"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type settingsBody struct {
	HubName, Host, Listen   string
	TLS                     bool
	Fingerprint, CertSource string
	CertNotAfter            string
	MinPassword             int
}

func (s *Server) renderSettings(w http.ResponseWriter, r *http.Request, sess *session, status int, errMsg string) {
	d := s.base(r, sess, "settings", "Settings")
	b := settingsBody{HubName: s.svc.Info().Name, Host: r.Host, Listen: s.cfg.Listen, TLS: s.cfg.UseTLS,
		Fingerprint: s.cfg.CertFingerprint, CertSource: s.cfg.CertSource, MinPassword: hub.MinPasswordLen}
	if !s.cfg.CertNotAfter.IsZero() {
		b.CertNotAfter = s.cfg.CertNotAfter.Local().Format("02.01.2006")
	}
	d.Body, d.Error = b, errMsg
	s.render(w, status, "settings", "layout", d)
}

func (s *Server) settingsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderSettings(w, r, sess, http.StatusOK, "")
}

func (s *Server) settingsName(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetHubName(r.Context(), r.PostFormValue("name")); err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			s.renderSettings(w, r, sess, http.StatusBadRequest, he.Message+".")
			return
		}
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/settings?ok=name", http.StatusSeeOther)
}

func (s *Server) settingsPassword(w http.ResponseWriter, r *http.Request, sess *session) {
	if r.PostFormValue("new") != r.PostFormValue("new2") {
		s.renderSettings(w, r, sess, http.StatusBadRequest, "Die neuen Passwörter stimmen nicht überein.")
		return
	}
	if _, err := s.svc.VerifyPassword(r.Context(), sess.User.Username, r.PostFormValue("current")); err != nil {
		if errors.Is(err, hub.ErrInvalidCredentials) {
			s.renderSettings(w, r, sess, http.StatusBadRequest, "Das aktuelle Passwort ist falsch.")
			return
		}
		s.fail(w, r, err)
		return
	}
	if err := s.svc.ChangePassword(r.Context(), sess.User.ID, r.PostFormValue("new")); err != nil {
		var he *hub.Error
		if errors.As(err, &he) && he.Code == hub.CodeBadRequest {
			s.renderSettings(w, r, sess, http.StatusBadRequest, he.Message+".")
			return
		}
		s.fail(w, r, err)
		return
	}
	// ChangePassword beendet alle Web-Sessions; neue Sitzung für den aktuellen Admin.
	if err := s.startSession(w, r, sess.User.ID); err != nil {
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/settings?ok=password", http.StatusSeeOther)
}
