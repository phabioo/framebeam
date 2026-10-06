// Package hub is the service layer of the FrameBeam Hub (hub identity, users, devices, pairing,
// tokens, library). The web interface and HTTP API use the same functions.
package hub

import (
	"context"
	"database/sql"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// Protocol constants in one place (ADR 0002); the info endpoint and handshake read them here.
const (
	ProtocolVersion    = 1
	MinProtocolVersion = 1
)

// Lifetimes and limits (ADR 0002 / orchestrator requirements).
const (
	AccessTokenTTL           = 15 * time.Minute
	PairingTTL               = 10 * time.Minute
	MaxOpenPairingRequests   = 20
	MaxPairingPerIPPerMinute = 5
	WebSessionTTL            = 12 * time.Hour
	MinPasswordLen           = 8
)

// Options configure the service.
type Options struct {
	DataDir string
	// Name is the hub name on first start (empty: hostname, or "FrameBeam Hub").
	Name       string
	HubVersion string
	// Now is the clock (injectable for tests); default time.Now.
	Now func() time.Time
	// PasswordParams: Argon2id parameters; default auth.DefaultParams.
	PasswordParams *auth.Params
	// ProtocolVersion/MinProtocolVersion override the constants (tests only); 0 = default.
	ProtocolVersion    int
	MinProtocolVersion int
	// ICEServers are the stun: URLs delivered in hello_ack and the join response (default empty, ADR 0006 D4).
	ICEServers []string
	// OwnerGrace is how long a Session survives the owner's dropped WSS connection (default 30 s; injectable for tests).
	OwnerGrace time.Duration
}

// Service is the service layer. Times are stored in SQLite as Unix seconds (UTC).
type Service struct {
	db       *sql.DB
	dataDir  string
	hubVer   string
	now      func() time.Time
	params   auth.Params
	protoVer int
	minProto int

	saveMu sync.Mutex // serializes save uploads/resolutions (and content cleanup)
	sess   sessionState
	mu     sync.RWMutex
	hubID  string
	name   string
	dummyO sync.Once
	dummy  string
}

// Open initializes the service on a migrated database and creates the hub identity and
// data directories.
func Open(ctx context.Context, db *sql.DB, o Options) (*Service, error) {
	s := &Service{db: db, dataDir: o.DataDir, hubVer: o.HubVersion, now: o.Now, params: auth.DefaultParams,
		protoVer: ProtocolVersion, minProto: MinProtocolVersion}
	if s.now == nil {
		s.now = time.Now
	}
	if o.PasswordParams != nil {
		s.params = *o.PasswordParams
	}
	if o.ProtocolVersion != 0 {
		s.protoVer = o.ProtocolVersion
	}
	if o.MinProtocolVersion != 0 {
		s.minProto = o.MinProtocolVersion
	}
	s.sess.init(o)
	for _, d := range []string{"roms", "tmp"} {
		if err := os.MkdirAll(filepath.Join(s.dataDir, d), 0o750); err != nil {
			return nil, internal(err)
		}
	}
	err := db.QueryRowContext(ctx, `SELECT id, name FROM hub LIMIT 1`).Scan(&s.hubID, &s.name)
	if errors.Is(err, sql.ErrNoRows) {
		name := strings.TrimSpace(o.Name)
		if name == "" {
			name, _ = os.Hostname()
		}
		if name == "" {
			name = "FrameBeam Hub"
		}
		s.hubID, s.name = uuid.NewString(), name
		_, err = db.ExecContext(ctx, `INSERT INTO hub(id, name) VALUES (?, ?)`, s.hubID, s.name)
	}
	if err != nil {
		return nil, internal(err)
	}
	s.recoverSessions(ctx)
	return s, nil
}

// DataDir returns the data directory.
func (s *Service) DataDir() string { return s.dataDir }

// Now returns the service's current time (clock injectable).
func (s *Service) Now() time.Time { return s.now().UTC() }

// Info describes the hub for the info endpoint.
type Info struct {
	HubID              string
	Name               string
	HubVersion         string
	ProtocolVersion    int
	MinProtocolVersion int
}

// Info returns the hub identity and protocol versions.
func (s *Service) Info() Info {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return Info{HubID: s.hubID, Name: s.name, HubVersion: s.hubVer,
		ProtocolVersion: s.protoVer, MinProtocolVersion: s.minProto}
}

// SetHubName changes the hub name.
func (s *Service) SetHubName(ctx context.Context, name string) error {
	name = strings.TrimSpace(name)
	if name == "" || len(name) > 100 {
		return badRequest("Name must be 1 to 100 characters long")
	}
	if _, err := s.db.ExecContext(ctx, `UPDATE hub SET name = ?`, name); err != nil {
		return internal(err)
	}
	s.mu.Lock()
	s.name = name
	s.mu.Unlock()
	return nil
}

// Cleanup removes expired access tokens, web sessions and old pairing requests.
func (s *Service) Cleanup(ctx context.Context) error {
	now := s.now().Unix()
	for _, q := range []struct {
		sql string
		arg int64
	}{
		{`DELETE FROM access_tokens WHERE expires_at <= ?`, now},
		{`DELETE FROM web_sessions WHERE expires_at <= ?`, now},
		// Expired requests stay visible for 1 h so the player can still poll "expired".
		{`DELETE FROM pairing_requests WHERE expires_at <= ?`, now - 3600},
	} {
		if _, err := s.db.ExecContext(ctx, q.sql, q.arg); err != nil {
			return internal(err)
		}
	}
	return nil
}

// RunCleanup calls Cleanup periodically until ctx ends. Errors go to onErr (may be nil).
func (s *Service) RunCleanup(ctx context.Context, every time.Duration, onErr func(error)) {
	t := time.NewTicker(every)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			if err := s.Cleanup(ctx); err != nil && onErr != nil && ctx.Err() == nil {
				onErr(err)
			}
		}
	}
}

func ts(t sql.NullInt64) *time.Time {
	if !t.Valid {
		return nil
	}
	v := time.Unix(t.Int64, 0).UTC()
	return &v
}

func isUnique(err error) bool {
	return err != nil && strings.Contains(err.Error(), "UNIQUE constraint failed")
}
