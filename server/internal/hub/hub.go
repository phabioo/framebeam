// Package hub ist die Service-Schicht des FrameBeam Hub (Hub-Identität, Users, Devices, Pairing,
// Tokens, Library). Webinterface und HTTP-API nutzen dieselben Funktionen.
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

// Protokollkonstanten an einer Stelle (ADR 0002); Info-Endpunkt und Handshake lesen sie hier.
const (
	ProtocolVersion    = 1
	MinProtocolVersion = 1
)

// Laufzeiten und Limits (ADR 0002 / Orchestrator-Vorgaben).
const (
	AccessTokenTTL           = 15 * time.Minute
	PairingTTL               = 10 * time.Minute
	MaxOpenPairingRequests   = 20
	MaxPairingPerIPPerMinute = 5
	WebSessionTTL            = 12 * time.Hour
	MinPasswordLen           = 8
)

// Options konfigurieren den Service.
type Options struct {
	DataDir string
	// Name ist der Hub-Name beim ersten Start (leer: Hostname bzw. "FrameBeam Hub").
	Name       string
	HubVersion string
	// Now ist die Uhr (injizierbar für Tests); Default time.Now.
	Now func() time.Time
	// PasswordParams: Argon2id-Parameter; Default auth.DefaultParams.
	PasswordParams *auth.Params
	// ProtocolVersion/MinProtocolVersion überschreiben die Konstanten (nur Tests); 0 = Default.
	ProtocolVersion    int
	MinProtocolVersion int
}

// Service ist die Service-Schicht. Zeiten liegen als Unix-Sekunden (UTC) in SQLite.
type Service struct {
	db       *sql.DB
	dataDir  string
	hubVer   string
	now      func() time.Time
	params   auth.Params
	protoVer int
	minProto int

	mu     sync.RWMutex
	hubID  string
	name   string
	dummyO sync.Once
	dummy  string
}

// Open initialisiert den Service auf einer migrierten Datenbank und legt Hub-Identität und
// Datenverzeichnisse an.
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
	return s, nil
}

// DataDir liefert das Datenverzeichnis.
func (s *Service) DataDir() string { return s.dataDir }

// Now liefert die aktuelle Zeit des Service (Uhr injizierbar).
func (s *Service) Now() time.Time { return s.now().UTC() }

// Info beschreibt den Hub für den Info-Endpunkt.
type Info struct {
	HubID              string
	Name               string
	HubVersion         string
	ProtocolVersion    int
	MinProtocolVersion int
}

// Info liefert Hub-Identität und Protokollversionen.
func (s *Service) Info() Info {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return Info{HubID: s.hubID, Name: s.name, HubVersion: s.hubVer,
		ProtocolVersion: s.protoVer, MinProtocolVersion: s.minProto}
}

// SetHubName ändert den Hub-Namen.
func (s *Service) SetHubName(ctx context.Context, name string) error {
	name = strings.TrimSpace(name)
	if name == "" || len(name) > 100 {
		return badRequest("Name muss 1 bis 100 Zeichen lang sein")
	}
	if _, err := s.db.ExecContext(ctx, `UPDATE hub SET name = ?`, name); err != nil {
		return internal(err)
	}
	s.mu.Lock()
	s.name = name
	s.mu.Unlock()
	return nil
}

// Cleanup entfernt abgelaufene Access Tokens, Web-Sessions und alte Pairing-Anfragen.
func (s *Service) Cleanup(ctx context.Context) error {
	now := s.now().Unix()
	for _, q := range []struct {
		sql string
		arg int64
	}{
		{`DELETE FROM access_tokens WHERE expires_at <= ?`, now},
		{`DELETE FROM web_sessions WHERE expires_at <= ?`, now},
		// Abgelaufene Anfragen bleiben 1 h sichtbar, damit der Player "expired" noch abfragen kann.
		{`DELETE FROM pairing_requests WHERE expires_at <= ?`, now - 3600},
	} {
		if _, err := s.db.ExecContext(ctx, q.sql, q.arg); err != nil {
			return internal(err)
		}
	}
	return nil
}

// RunCleanup ruft Cleanup periodisch auf, bis ctx endet. Fehler gehen an onErr (darf nil sein).
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
