package hub

import (
	"context"
	"encoding/json"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/turnsrv"
)

const settingExternalTURN = "external_turn"

// Sources of the external TURN list.
const (
	ExternalTURNFromFlags = "flags"
	ExternalTURNFromWeb   = "web"
)

// Limits of the web form.
const (
	maxExternalTURN     = 8
	maxExternalTURNURLs = 8
	maxExternalSecret   = 512
)

// External TURN servers (own coturn on a VPS or a third-party provider) as relay providers next to the embedded
// TURN server and Cloudflare TURN. Two credential modes per server:
//
//   - ExternalTURNStatic: one fixed username and password, handed to every app as is (no expiry).
//   - ExternalTURNSecret: shared secret of the TURN REST API (coturn use-auth-secret). The server issues time-limited
//     credentials per device (username "<expiry-unix>:<device-id>", password base64(HMAC-SHA1(secret, username)),
//     TTL like the embedded TURN) and re-sends them by ice_update before they expire.
//
// Two sources, the same precedence as the other server settings: the list saved in the web interface (Settings,
// Network) wins; without a saved list the servers from the flags/hub.env (-turn-external) apply. Secrets of
// flag-configured servers only exist in memory (read from the environment or a file); secrets saved in the web
// interface live in the settings table like the embedded TURN secret. Neither is ever logged or part of
// ExternalTURNStatus.

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
	Username  string // static mode only; not a secret, but kept out of diagnostics
	SecretSet bool   // the password (static) or shared secret (secret) is configured; without it the server is not used
}

// ExternalTURNOptions configure StartExternalTURN. Refresh has a default and is overridden by tests.
type ExternalTURNOptions struct {
	// Refresh is the interval of the ice_update push that renews shared-secret credentials (default half of the
	// credential lifetime).
	Refresh time.Duration
	// Disabled loads and keeps the configuration (so it can be edited) but does not hand it to apps, e.g. while
	// Network sharing is off.
	Disabled bool
}

type externalState struct {
	edit sync.Mutex // serializes read-modify-write of the saved list

	p atomic.Pointer[[]ExternalTURN] // servers handed to apps (secret set, server running with the feature enabled)

	mu     sync.RWMutex
	flags  []ExternalTURN // from -turn-external, with secrets read at startup
	cfg    []ExternalTURN // configured list (saved or flags), resolved secrets, may hold servers without a secret
	refs   []string       // per cfg entry: flag entry name the secret falls back to, or ""
	source string
	active bool
}

// storedExternalTURN is one saved server (JSON in the settings table). Secret is the shared secret (mode secret) or
// the password (mode static). FlagRef names the flag-configured server whose environment/file secret is used while
// Secret is empty (set when the first web edit copied the flag list).
type storedExternalTURN struct {
	Name     string   `json:"name"`
	URLs     []string `json:"urls"`
	Mode     string   `json:"mode"`
	Username string   `json:"username,omitempty"`
	Secret   string   `json:"secret,omitempty"`
	FlagRef  string   `json:"flag_ref,omitempty"`
}

func (e *externalState) list() []ExternalTURN {
	if p := e.p.Load(); p != nil {
		return *p
	}
	return nil
}

func hasSecret(p ExternalTURN) bool { return p.Secret != "" || p.Password != "" }

// ExternalTURNStatus lists the configured external TURN servers (saved list, else flags) without secrets.
func (s *Service) ExternalTURNStatus() []ExternalTURNStatus {
	s.ext.mu.RLock()
	defer s.ext.mu.RUnlock()
	var out []ExternalTURNStatus
	for _, p := range s.ext.cfg {
		out = append(out, ExternalTURNStatus{Name: p.Name, URLs: append([]string(nil), p.URLs...), Mode: p.Mode,
			Username: p.Username, SecretSet: hasSecret(p)})
	}
	return out
}

// ExternalTURNSource says where the list comes from: ExternalTURNFromWeb (saved in Settings) or
// ExternalTURNFromFlags (-turn-external, may be empty).
func (s *Service) ExternalTURNSource() string {
	s.ext.mu.RLock()
	defer s.ext.mu.RUnlock()
	if s.ext.source == "" {
		return ExternalTURNFromFlags
	}
	return s.ext.source
}

// ExternalTURNActive reports whether the configured servers are handed to apps (false while Network sharing is
// off or before StartExternalTURN).
func (s *Service) ExternalTURNActive() bool {
	s.ext.mu.RLock()
	defer s.ext.mu.RUnlock()
	return s.ext.active
}

func (s *Service) loadStoredExternal(ctx context.Context) ([]storedExternalTURN, bool, error) {
	v, ok, err := s.getSetting(ctx, settingExternalTURN)
	if err != nil || !ok {
		return nil, false, err
	}
	var st []storedExternalTURN
	if err := json.Unmarshal([]byte(v), &st); err != nil {
		return nil, true, internal(err)
	}
	return st, true, nil
}

// reloadExternal recomputes the configured and effective list from the saved list and the flags.
func (s *Service) reloadExternal(ctx context.Context) error {
	st, saved, err := s.loadStoredExternal(ctx)
	if err != nil {
		return err
	}
	e := &s.ext
	e.mu.Lock()
	if saved {
		e.source = ExternalTURNFromWeb
		e.cfg, e.refs = nil, nil
		for _, x := range st {
			p := ExternalTURN{Name: x.Name, URLs: x.URLs, Mode: x.Mode, Username: x.Username}
			if x.Mode == ExternalTURNSecret {
				p.Secret = x.Secret
			} else {
				p.Password = x.Secret
			}
			if x.Secret == "" && x.FlagRef != "" {
				for _, f := range e.flags {
					if f.Name == x.FlagRef && f.Mode == x.Mode {
						p.Secret, p.Password = f.Secret, f.Password
					}
				}
			}
			e.cfg, e.refs = append(e.cfg, p), append(e.refs, x.FlagRef)
		}
	} else {
		e.source = ExternalTURNFromFlags
		e.cfg, e.refs = append([]ExternalTURN(nil), e.flags...), make([]string, len(e.flags))
	}
	var eff []ExternalTURN
	if e.active {
		for _, p := range e.cfg {
			if hasSecret(p) {
				eff = append(eff, p)
			}
		}
	}
	if len(eff) == 0 {
		e.p.Store(nil)
	} else {
		e.p.Store(&eff)
	}
	e.mu.Unlock()
	return nil
}

// StartExternalTURN activates the external TURN servers until ctx ends. flags is the list from -turn-external; a
// list saved in the web interface replaces it. Apps get the servers by ice_update; shared-secret credentials are
// renewed by a periodic ice_update push that always reads the current list, so edits apply without a restart.
func (s *Service) StartExternalTURN(ctx context.Context, flags []ExternalTURN, o ExternalTURNOptions) {
	s.ext.mu.Lock()
	s.ext.flags = append([]ExternalTURN(nil), flags...)
	s.ext.active = !o.Disabled
	s.ext.mu.Unlock()
	if err := s.reloadExternal(ctx); err != nil { // unreadable saved list: fall back to the flags
		s.ext.mu.Lock()
		s.ext.cfg, s.ext.refs, s.ext.source = append([]ExternalTURN(nil), flags...), make([]string, len(flags)), ExternalTURNFromFlags
		var eff []ExternalTURN
		for _, p := range s.ext.cfg {
			if s.ext.active && hasSecret(p) {
				eff = append(eff, p)
			}
		}
		if len(eff) > 0 {
			s.ext.p.Store(&eff)
		}
		s.ext.mu.Unlock()
	}
	s.PushICEUpdate()
	refresh := o.Refresh
	if refresh <= 0 {
		refresh = turnsrv.CredentialTTL / 2
	}
	go func() {
		defer func() {
			s.ext.mu.Lock()
			s.ext.active = false
			s.ext.p.Store(nil)
			s.ext.mu.Unlock()
			s.PushICEUpdate()
		}()
		t := time.NewTicker(refresh)
		defer t.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-t.C:
				for _, p := range s.ext.list() {
					if p.Mode == ExternalTURNSecret {
						s.PushICEUpdate()
						break
					}
				}
			}
		}
	}()
}

// ExternalTURNInput is one add/edit of an external TURN server from the web interface. Secret is the new shared
// secret or password; empty keeps the existing one unless ClearSecret is set.
type ExternalTURNInput struct {
	Original    string // name of the server to edit; empty adds a new one
	Name, Mode  string
	URLs        []string
	Username    string
	Secret      string
	ClearSecret bool
}

// editableExternal returns the list to modify: the saved one, else a copy of the flag list without secrets (the
// secrets of those servers keep coming from the environment or file through FlagRef).
func (s *Service) editableExternal(ctx context.Context) ([]storedExternalTURN, error) {
	st, saved, err := s.loadStoredExternal(ctx)
	if err != nil || saved {
		return st, err
	}
	s.ext.mu.RLock()
	defer s.ext.mu.RUnlock()
	for _, f := range s.ext.flags {
		st = append(st, storedExternalTURN{Name: f.Name, URLs: append([]string(nil), f.URLs...), Mode: f.Mode, Username: f.Username, FlagRef: f.Name})
	}
	return st, nil
}

func (s *Service) saveExternal(ctx context.Context, st []storedExternalTURN) error {
	if st == nil {
		st = []storedExternalTURN{}
	}
	b, err := json.Marshal(st)
	if err != nil {
		return internal(err)
	}
	if err := s.setSetting(ctx, settingExternalTURN, string(b)); err != nil {
		return err
	}
	if err := s.reloadExternal(ctx); err != nil {
		return err
	}
	s.PushICEUpdate()
	return nil
}

// SaveExternalTURN adds or edits an external TURN server and saves the whole list as the web setting (it then
// replaces the flag list). The change applies to new joins and by ice_update to connected apps.
func (s *Service) SaveExternalTURN(ctx context.Context, in ExternalTURNInput) error {
	in.Name, in.Mode, in.Username, in.Secret = strings.TrimSpace(in.Name), strings.ToLower(strings.TrimSpace(in.Mode)),
		strings.TrimSpace(in.Username), strings.TrimSpace(in.Secret)
	if err := config.ValidateExternalTURNName(in.Name); err != nil {
		return badRequest("%s", upperFirst(err.Error()))
	}
	if len(in.URLs) == 0 {
		return badRequest("Enter at least one TURN URL")
	}
	if len(in.URLs) > maxExternalTURNURLs {
		return badRequest("At most %d URLs per server", maxExternalTURNURLs)
	}
	for _, u := range in.URLs {
		if err := config.ValidateTURNURL(u); err != nil {
			return badRequest("%s", upperFirst(err.Error()))
		}
	}
	switch in.Mode {
	case ExternalTURNSecret:
		in.Username = ""
	case ExternalTURNStatic:
		if in.Username == "" {
			return badRequest("Enter the username for a static server")
		}
	default:
		return badRequest("Mode must be secret or static")
	}
	if len(in.Secret) > maxExternalSecret || strings.ContainsAny(in.Secret, "\r\n") {
		return badRequest("The secret is not valid")
	}
	s.ext.edit.Lock()
	defer s.ext.edit.Unlock()
	st, err := s.editableExternal(ctx)
	if err != nil {
		return err
	}
	idx := -1
	for i, x := range st {
		if x.Name == in.Original && in.Original != "" {
			idx = i
		}
		if x.Name == in.Name && x.Name != in.Original {
			return badRequest("A server named %q already exists", in.Name)
		}
	}
	if in.Original != "" && idx < 0 {
		return badRequest("This server no longer exists")
	}
	n := storedExternalTURN{Name: in.Name, URLs: in.URLs, Mode: in.Mode, Username: in.Username, Secret: in.Secret}
	if idx < 0 {
		if len(st) >= maxExternalTURN {
			return badRequest("At most %d external TURN servers", maxExternalTURN)
		}
		if in.Secret == "" {
			return badRequest("Enter the secret or password")
		}
		st = append(st, n)
	} else {
		old := st[idx]
		if in.Secret == "" && !in.ClearSecret && old.Mode == in.Mode { // keep what is there
			n.Secret, n.FlagRef = old.Secret, old.FlagRef
		}
		st[idx] = n
	}
	return s.saveExternal(ctx, st)
}

// DeleteExternalTURN removes an external TURN server from the saved list (saving the list if it still came from
// the flags).
func (s *Service) DeleteExternalTURN(ctx context.Context, name string) error {
	s.ext.edit.Lock()
	defer s.ext.edit.Unlock()
	st, err := s.editableExternal(ctx)
	if err != nil {
		return err
	}
	var out []storedExternalTURN
	found := false
	for _, x := range st {
		if x.Name == name {
			found = true
			continue
		}
		out = append(out, x)
	}
	if !found {
		return badRequest("This server no longer exists")
	}
	return s.saveExternal(ctx, out)
}

// ResetExternalTURN drops the saved list: the flags/hub.env list applies again.
func (s *Service) ResetExternalTURN(ctx context.Context) error {
	s.ext.edit.Lock()
	defer s.ext.edit.Unlock()
	if _, err := s.db.ExecContext(ctx, `DELETE FROM settings WHERE key = ?`, settingExternalTURN); err != nil {
		return internal2(err)
	}
	if err := s.reloadExternal(ctx); err != nil {
		return err
	}
	s.PushICEUpdate()
	return nil
}

func upperFirst(s string) string {
	if s == "" {
		return s
	}
	return strings.ToUpper(s[:1]) + s[1:]
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
