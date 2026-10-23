// Package serverhost implements adminhost.Host for the running server process: it holds the process-level facts
// (configuration the server started with, TURN relay, certificate, restart hook) that the admin REST API needs.
// It uses only the service layer internal/hub.
package serverhost

import (
	"context"
	"log/slog"
	"net"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/adminhost"
	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/diag"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/statusinfo"
	"github.com/phabioo/uzume/server/internal/turnsrv"
)

// DefaultMaxUploadBytes is the upload limit for ROMs (4 GiB).
const DefaultMaxUploadBytes = hub.MaxROMBytes

// NetConfig is what the network settings need to know about the running server (set by cmd/uzume-server).
type NetConfig struct {
	// Base is the configuration from server.env and flags, without the values saved through the admin API.
	Base config.Config
	// Running is the effective configuration the server started with (Base plus saved values, after fallbacks).
	Running config.Config
	// Issues are saved values that could not be applied at startup (the server then runs with a fallback).
	Issues []NetIssue
	// RequestRestart asks the process to shut down gracefully and re-execute itself (nil: not supported).
	RequestRestart func()
	// TestBind checks that the server can listen on an address (default config.TestBind).
	TestBind func(addr string) error
}

// NetIssue is a saved network value that could not be used at startup.
type NetIssue struct {
	Key     string // config.Net* key
	Value   string // the saved raw value that failed
	Message string // shown to the admin next to the setting
}

// NetSignature identifies the value of a setting for NetIssue.Value. The TURN setting is the combination of all
// TURN related values, so that an issue disappears as soon as any of them is changed.
func NetSignature(c *config.Config, key string) string {
	if key == config.NetTURN {
		return strings.Join([]string{c.NetValue(config.NetTURN), c.NetValue(config.NetPublicHost), c.NetValue(config.NetTURNPort),
			c.NetValue(config.NetRelayPorts), c.NetValue(config.NetRelayIP)}, "|")
	}
	return c.NetValue(key)
}

// turnKeys are the settings that make up the TURN configuration.
var turnKeys = map[string]bool{config.NetTURN: true, config.NetPublicHost: true, config.NetTURNPort: true,
	config.NetRelayPorts: true, config.NetRelayIP: true}

// Config describes operational data the service does not know.
type Config struct {
	// Listen is the configured listen address.
	Listen string
	// UseTLS: HTTPS is active.
	UseTLS bool
	// TrustedProxy lists the configured -trusted-proxy networks (read-only).
	TrustedProxy []string
	// CertFingerprint (SHA-256), CertSource ("Self-generated"/"Own cert/key"), CertNotAfter describe the certificate.
	CertFingerprint string
	CertSource      string
	CertNotAfter    time.Time
	// CertState, when set, returns the current fingerprint and expiry and overrides CertFingerprint/CertNotAfter
	// (the certificate can change at runtime after RenewCert).
	CertState func() (fingerprint string, notAfter time.Time)
	// RenewCert replaces the self-generated certificate without a restart and returns the new fingerprint and
	// expiry. nil when not supported (own certificate, plain HTTP).
	RenewCert func() (fingerprint string, notAfter time.Time, err error)
	// PublicHost and PublicPort (0: unknown) are the externally used address (-public-host).
	PublicHost string
	PublicPort int
	// ImportDir is the library import folder.
	ImportDir string
	// MaxUploadBytes: limit for ROM uploads (0 = DefaultMaxUploadBytes).
	MaxUploadBytes int64
	// TURN is the embedded TURN server (nil: off).
	TURN *turnsrv.Server
	// Net describes the network settings and the restart hook.
	Net NetConfig
	// Errors holds the recent WARN/ERROR log records for the status view (default diag.RecentErrors).
	Errors *diag.ErrorRing
	// Log holds the recent log lines (default diag.Recent).
	Log *diag.Ring
	// Disk replaces the disk reader of the status view (tests only).
	Disk func(dir string) (total, free uint64, ok bool)
}

// Server implements adminhost.Host.
type Server struct {
	svc *hub.Service
	cfg Config
	log *slog.Logger
	src *statusinfo.Source
}

var _ adminhost.Host = (*Server)(nil)

// New creates the host.
func New(svc *hub.Service, cfg Config, log *slog.Logger) *Server {
	if log == nil {
		log = slog.Default()
	}
	if cfg.MaxUploadBytes <= 0 {
		cfg.MaxUploadBytes = DefaultMaxUploadBytes
	}
	if cfg.Errors == nil {
		cfg.Errors = diag.RecentErrors
	}
	if cfg.Log == nil {
		cfg.Log = diag.Recent
	}
	var turnIssues []string
	for _, is := range cfg.Net.Issues {
		if is.Key == config.NetTURN {
			turnIssues = append(turnIssues, is.Message)
		}
	}
	src := &statusinfo.Source{Svc: svc, TURN: cfg.TURN, PublicHost: cfg.PublicHost, PublicPort: cfg.PublicPort,
		ListenPort: func() int { return cfg.Net.Running.ListenPort() }, TURNIssues: turnIssues, Errors: cfg.Errors, Log: cfg.Log, Disk: cfg.Disk}
	return &Server{svc: svc, cfg: cfg, log: log, src: src}
}

// StatusSource is the health data source of the admin REST API.
func (s *Server) StatusSource() *statusinfo.Source { return s.src }

// netDesired returns the configuration as it is meant to be: server.env/flags plus the saved values.
func (s *Server) netDesired(ctx context.Context) (config.Config, map[string]string, error) {
	stored, err := s.svc.NetOverrides(ctx)
	if err != nil {
		return config.Config{}, nil, err
	}
	c := s.cfg.Net.Base
	c.ICEServers = config.CloneICE(s.cfg.Net.Base.ICEServers)
	c.ApplyNet(stored)
	return c, stored, nil
}

// netPending lists the labels of saved settings that are not applied yet (they need a restart), except those that
// failed at startup, and the messages of those failed ones.
func (s *Server) netPending(desired config.Config) (pending, issueMsgs []string) {
	run := s.cfg.Net.Running
	issues := map[string]NetIssue{}
	for _, is := range s.cfg.Net.Issues {
		if NetSignature(&desired, is.Key) == is.Value {
			issues[is.Key] = is
			issueMsgs = append(issueMsgs, is.Message)
		}
	}
	_, turnBroken := issues[config.NetTURN]
	for _, k := range config.NetKeys {
		if !config.NetNeedsRestart(k) {
			continue
		}
		if _, bad := issues[k]; bad || (turnBroken && turnKeys[k]) {
			continue
		}
		if desired.NetValue(k) != run.NetValue(k) {
			pending = append(pending, config.NetLabel(k))
		}
	}
	return pending, issueMsgs
}

// certInfo returns the current certificate fingerprint and expiry (live when the server can renew at runtime).
func (s *Server) certInfo() (string, time.Time) {
	if s.cfg.CertState != nil {
		return s.cfg.CertState()
	}
	return s.cfg.CertFingerprint, s.cfg.CertNotAfter
}

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

// PrepareRestart implements adminhost.Host. The address comes from the configuration (public host, else a concrete
// listen host), never from a request's Host header; without one the URL stays empty.
func (s *Server) PrepareRestart(ctx context.Context, admin string) (adminhost.Restart, func(), error) {
	if s.cfg.Net.RequestRestart == nil {
		return adminhost.Restart{}, nil, adminhost.ErrUnsupported
	}
	desired, _, err := s.netDesired(ctx)
	if err != nil {
		return adminhost.Restart{}, nil, err
	}
	scheme := "https"
	if !s.cfg.UseTLS {
		scheme = "http"
	}
	host := s.cfg.PublicHost
	if host == "" {
		if h, _, err := net.SplitHostPort(s.cfg.Listen); err == nil && h != "" {
			if ip := net.ParseIP(h); ip == nil || !ip.IsUnspecified() {
				host = h
			}
		}
	}
	port := desired.ListenPort()
	rb := adminhost.Restart{PortChanged: port != s.cfg.Net.Running.ListenPort()}
	if host != "" {
		rb.URL = scheme + "://" + net.JoinHostPort(host, strconv.Itoa(port))
	}
	s.log.Info("server restart requested from the admin API", "user", admin)
	return rb, s.cfg.Net.RequestRestart, nil
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
