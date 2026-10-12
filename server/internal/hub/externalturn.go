package hub

import (
	"context"
	"sync/atomic"
	"time"

	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// External TURN servers (own coturn on a VPS or a third-party provider) as relay providers next to the embedded
// TURN server and Cloudflare TURN. Two credential modes per server:
//
//   - ExternalTURNStatic: one fixed username and password, handed to every Player as is (no expiry).
//   - ExternalTURNSecret: shared secret of the TURN REST API (coturn use-auth-secret). The Hub issues time-limited
//     credentials per device (username "<expiry-unix>:<device-id>", password base64(HMAC-SHA1(secret, username)),
//     TTL like the embedded TURN) and re-sends them by ice_update before they expire.
//
// Passwords and secrets only exist in memory (the caller reads them from the environment or a file); they are never
// stored, logged or part of ExternalTURNStatus.

// Credential modes of an external TURN server.
const (
	ExternalTURNStatic = "static"
	ExternalTURNSecret = "secret"
)

// ExternalTURN is one configured external TURN server.
type ExternalTURN struct {
	Name string   // label for Settings, diagnostics and logs
	URLs []string // turn: and turns: URLs
	Mode string   // ExternalTURNStatic or ExternalTURNSecret
	// Username and Password are the static credentials (ExternalTURNStatic).
	Username, Password string
	// Secret is the shared secret (ExternalTURNSecret).
	Secret string
}

// ExternalTURNStatus is what Settings and diagnostics may show of an external TURN server: never a password or secret.
type ExternalTURNStatus struct {
	Name      string
	URLs      []string
	Mode      string
	SecretSet bool // the password (static) or shared secret (secret) is configured
}

// ExternalTURNOptions configure StartExternalTURN. Refresh has a default and is overridden by tests.
type ExternalTURNOptions struct {
	// Refresh is the interval of the ice_update push that renews shared-secret credentials (default half of the
	// credential lifetime).
	Refresh time.Duration
}

type externalState struct {
	p atomic.Pointer[[]ExternalTURN]
}

func (e *externalState) list() []ExternalTURN {
	if p := e.p.Load(); p != nil {
		return *p
	}
	return nil
}

// ExternalTURNStatus lists the active external TURN servers without secrets.
func (s *Service) ExternalTURNStatus() []ExternalTURNStatus {
	var out []ExternalTURNStatus
	for _, p := range s.ext.list() {
		out = append(out, ExternalTURNStatus{Name: p.Name, URLs: append([]string(nil), p.URLs...), Mode: p.Mode,
			SecretSet: p.Secret != "" || p.Password != ""})
	}
	return out
}

// StartExternalTURN activates the external TURN servers until ctx ends. Players get them by ice_update; in secret
// mode the credentials are renewed by a periodic ice_update push.
func (s *Service) StartExternalTURN(ctx context.Context, ps []ExternalTURN, o ExternalTURNOptions) {
	if len(ps) == 0 {
		return
	}
	cp := append([]ExternalTURN(nil), ps...)
	s.ext.p.Store(&cp)
	s.PushICEUpdate()
	refresh := o.Refresh
	if refresh <= 0 {
		refresh = turnsrv.CredentialTTL / 2
	}
	hasSecret := false
	for _, p := range cp {
		hasSecret = hasSecret || p.Mode == ExternalTURNSecret
	}
	go func() {
		defer func() {
			s.ext.p.Store(nil)
			s.PushICEUpdate()
		}()
		if !hasSecret {
			<-ctx.Done()
			return
		}
		t := time.NewTicker(refresh)
		defer t.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-t.C:
				s.PushICEUpdate()
			}
		}
	}()
}

// externalFor returns the credentials of every external TURN server for a device.
func (s *Service) externalFor(deviceID string) []turnsrv.Credentials {
	var out []turnsrv.Credentials
	for _, p := range s.ext.list() {
		urls := append([]string(nil), p.URLs...)
		switch p.Mode {
		case ExternalTURNSecret:
			u, c, exp := turnsrv.Issue([]byte(p.Secret), deviceID, s.now())
			out = append(out, turnsrv.Credentials{URLs: urls, Username: u, Credential: c, ExpiresAt: exp})
		case ExternalTURNStatic:
			out = append(out, turnsrv.Credentials{URLs: urls, Username: p.Username, Credential: p.Password})
		}
	}
	return out
}
