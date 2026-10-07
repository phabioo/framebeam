package web

import (
	"errors"
	"fmt"
	"net"
	"net/http"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/tlsutil"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

type settingsBody struct {
	HubName, Host, Listen   string
	TLS                     bool
	Fingerprint, CertSource string
	CertNotAfter            string
	CertExpiresSoon         bool // expired or expires within tlsutil.RenewBefore
	CertSelfGenerated       bool
	MinPassword             int
	Appearance              string
	AllowUploads            bool
	Updates                 updatesBody
	TURN                    turnBody
}

type turnBody struct {
	On                    bool
	PublicHost            string
	RelayIP, ResolvedAt   string
	Fixed                 bool
	Port, RelayMin        int
	RelayMax, Allocations int
	Forwards              []string
}

type updatesBody struct {
	hub.UpdateStatus
	LastCheckText string
	LastResultAt  string
	// Selectable channel option values: "" is the compiled default and only offered for development builds.
	OffOption bool
	Channel   string // select value: stable, beta or "" (development default)
	Err       bool   // the status could not be read
}

func (s *Server) renderSettings(w http.ResponseWriter, r *http.Request, sess *session, status int, errMsg string) {
	d := s.base(r, sess, "settings", "Settings")
	b := settingsBody{HubName: s.svc.Info().Name, Host: r.Host, Listen: s.cfg.Listen, TLS: s.cfg.UseTLS,
		Fingerprint: s.cfg.CertFingerprint, CertSource: s.cfg.CertSource, MinPassword: hub.MinPasswordLen}
	if !s.cfg.CertNotAfter.IsZero() {
		b.CertNotAfter = s.cfg.CertNotAfter.Local().Format("2006-01-02")
		b.CertExpiresSoon = tlsutil.ExpiresSoon(s.cfg.CertNotAfter, time.Now())
	}
	if s.cfg.TURN != nil {
		st := s.cfg.TURN.Status()
		b.TURN = turnBody{On: true, PublicHost: st.PublicHost, RelayIP: st.RelayIP, Fixed: st.Fixed, Port: st.Port,
			RelayMin: st.RelayMin, RelayMax: st.RelayMax, Allocations: st.Allocations, Forwards: turnForwards(s.cfg.Listen, st)}
		if !st.ResolvedAt.IsZero() {
			b.TURN.ResolvedAt = st.ResolvedAt.Local().Format("2006-01-02 15:04")
		}
	}
	b.CertSelfGenerated = s.cfg.CertSource == "Self-generated"
	b.Appearance, _ = s.svc.Appearance(r.Context())
	b.AllowUploads, _ = s.svc.AllowUserUploads(r.Context())
	if st, err := s.svc.UpdateStatus(r.Context()); err == nil {
		b.Updates = updatesBody{UpdateStatus: st, LastCheckText: "never", OffOption: st.Settings.CompiledChannel != "stable" && st.Settings.CompiledChannel != "beta"}
		if st.Settings.ChannelIsSet {
			b.Updates.Channel = st.Settings.Channel
		} else if !b.Updates.OffOption {
			b.Updates.Channel = st.Settings.Channel
		}
		if st.LastCheck != nil {
			b.Updates.LastCheckText = st.LastCheck.Local().Format("2006-01-02 15:04")
		}
		if st.LastResult != nil {
			b.Updates.LastResultAt = st.LastResult.Time.Local().Format("2006-01-02 15:04")
		}
	} else {
		b.Updates.Err = true
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
		s.renderSettings(w, r, sess, http.StatusBadRequest, "The new passwords do not match.")
		return
	}
	if _, err := s.svc.VerifyPassword(r.Context(), sess.User.Username, r.PostFormValue("current")); err != nil {
		if errors.Is(err, hub.ErrInvalidCredentials) {
			s.renderSettings(w, r, sess, http.StatusBadRequest, "The current password is incorrect.")
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
	// ChangePassword ends all web sessions; start a new session for the current admin.
	if err := s.startSession(w, r, sess.User.ID); err != nil {
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/settings?ok=password", http.StatusSeeOther)
}

func (s *Server) settingsAppearance(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetAppearance(r.Context(), r.PostFormValue("mode")); err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			s.renderSettings(w, r, sess, http.StatusBadRequest, he.Message+".")
			return
		}
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/settings?ok=appearance", http.StatusSeeOther)
}

func (s *Server) settingsUploads(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetAllowUserUploads(r.Context(), r.PostFormValue("enabled") == "1"); err != nil {
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/settings?ok=uploads", http.StatusSeeOther)
}

func (s *Server) settingsUpdates(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetUpdateSettings(r.Context(), r.PostFormValue("channel"), r.PostFormValue("auto") == "1"); err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			s.renderSettings(w, r, sess, http.StatusBadRequest, he.Message+".")
			return
		}
		s.fail(w, r, err)
		return
	}
	s.redirect(w, r, "/settings?ok=updates")
}

// settingsUpdatesCheck starts a check in the background ("Check now").
func (s *Server) settingsUpdatesCheck(w http.ResponseWriter, r *http.Request, _ *session) {
	s.svc.TriggerUpdateCheck()
	s.redirect(w, r, "/settings?ok=updatecheck")
}

// settingsUpdatesInstall stages the update and asks the root helper to install it. The browser asks for
// confirmation first (hx-confirm); an update that breaks recent Players is only installed with confirm_breaking=1.
func (s *Server) settingsUpdatesInstall(w http.ResponseWriter, r *http.Request, _ *session) {
	_, err := s.svc.InstallUpdate(r.Context(), r.PostFormValue("confirm_breaking") == "1")
	switch {
	case err == nil:
		s.redirect(w, r, "/settings?ok=updateinstall")
	case errors.Is(err, hub.ErrNotPackaged):
		s.redirect(w, r, "/settings?err=updatepackage")
	case errors.Is(err, hub.ErrNoUpdate), errors.Is(err, hub.ErrUpdatesOff):
		s.redirect(w, r, "/settings?err=updatenone")
	case errors.Is(err, hub.ErrUpdateBreaking):
		s.redirect(w, r, "/settings?err=updatebreaking")
	default:
		s.log.Error("install update", "err", err)
		s.redirect(w, r, "/settings?err=updatefailed")
	}
}

// turnForwards lists the router port forwards a public Hub needs (ADR 0012 D1).
func turnForwards(listen string, st turnsrv.Status) []string {
	hubPort := "8443"
	if _, p, err := net.SplitHostPort(listen); err == nil && p != "" {
		hubPort = p
	}
	return []string{
		"TCP " + hubPort + " (Hub, HTTPS/WSS)",
		fmt.Sprintf("UDP and TCP %d (STUN/TURN)", st.Port),
		fmt.Sprintf("UDP %d-%d (relay)", st.RelayMin, st.RelayMax),
	}
}
