package web

import (
	"context"
	"net/url"
	"time"

	"github.com/phabioo/uzume/server/internal/adminhost"
	"github.com/phabioo/uzume/server/internal/config"
)

// The server implements adminhost.Host for the admin REST API: the API calls the same functions as the Settings
// pages (netSave, netReset, prepareRestart, the certificate renewal).
var _ adminhost.Host = (*Server)(nil)

// NetState implements adminhost.Host.
func (s *Server) NetState(ctx context.Context) (adminhost.NetState, error) {
	desired, stored, err := s.netDesired(ctx)
	if err != nil {
		return adminhost.NetState{}, err
	}
	st := adminhost.NetState{Base: s.cfg.Net.Base, Running: s.cfg.Net.Running, Desired: desired, Stored: stored,
		CanRestart: s.cfg.Net.RequestRestart != nil}
	st.Pending, st.Issues = s.netPending(desired)
	if s.cfg.TURN != nil {
		ts := s.cfg.TURN.Status()
		st.TURN = &ts
		st.TURNForwards = turnForwards(s.cfg.Net.Running.Listen, ts)
	}
	return st, nil
}

// SaveNet implements adminhost.Host.
func (s *Server) SaveNet(ctx context.Context, key string, form url.Values) error {
	return s.netSave(ctx, key, form)
}

// ResetNet implements adminhost.Host.
func (s *Server) ResetNet(ctx context.Context, key string) error { return s.netReset(ctx, key) }

// PrepareRestart implements adminhost.Host.
func (s *Server) PrepareRestart(ctx context.Context, admin string) (adminhost.Restart, func(), error) {
	rb, restart, err := s.prepareRestart(ctx)
	if err == nil {
		s.log.Info("hub restart requested from the admin API", "user", admin)
	}
	return rb, restart, err
}

// Security implements adminhost.Host.
func (s *Server) Security() adminhost.Security {
	fp, notAfter := s.certInfo()
	return adminhost.Security{TLS: s.cfg.UseTLS, TrustedProxy: s.cfg.TrustedProxy, Fingerprint: fp, CertSource: s.cfg.CertSource,
		NotAfter: notAfter, CanRenew: s.cfg.RenewCert != nil && s.cfg.CertSource == "Self-generated"}
}

// RenewCert implements adminhost.Host.
func (s *Server) RenewCert(admin string) (string, time.Time, error) {
	if s.cfg.RenewCert == nil || s.cfg.CertSource != "Self-generated" {
		return "", time.Time{}, adminhost.ErrUnsupported
	}
	fp, notAfter, err := s.cfg.RenewCert()
	if err != nil {
		s.log.Error("renew tls certificate", "err", err)
		return "", time.Time{}, err
	}
	s.log.Warn("TLS certificate renewed; apps must confirm the new fingerprint", "new_sha256_fingerprint", fp, "admin", admin)
	return fp, notAfter, nil
}

// RunningConfig implements adminhost.Host.
func (s *Server) RunningConfig() config.Config { return s.cfg.Net.Running }

// ImportFolder implements adminhost.Host.
func (s *Server) ImportFolder() (string, int64) { return s.cfg.ImportDir, s.cfg.MaxUploadBytes }
