package hub

import (
	"context"
	"crypto/subtle"
	"database/sql"
	"errors"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// DeviceStatus ist der Vertrauensstatus eines Geräts.
type DeviceStatus string

const (
	DeviceTrusted DeviceStatus = "trusted"
	DeviceRevoked DeviceStatus = "revoked"
)

// Device ist ein gekoppelter FrameBeam Player. Die ID meldet der Player selbst.
type Device struct {
	ID            string
	UserID        string
	Name          string
	Platform      string
	Arch          string
	PlayerVersion string
	Status        DeviceStatus
	CreatedAt     time.Time
	LastSeenAt    *time.Time
	RevokedAt     *time.Time
}

const deviceCols = `id, user_id, name, platform, arch, player_version, status, created_at, last_seen_at, revoked_at`

func scanDevice(r scanner) (Device, error) {
	var d Device
	var created int64
	var seen, rev sql.NullInt64
	if err := r.Scan(&d.ID, &d.UserID, &d.Name, &d.Platform, &d.Arch, &d.PlayerVersion, &d.Status, &created, &seen, &rev); err != nil {
		return Device{}, err
	}
	d.CreatedAt = time.Unix(created, 0).UTC()
	d.LastSeenAt, d.RevokedAt = ts(seen), ts(rev)
	return d, nil
}

// GetDevice liefert ein Gerät per ID.
func (s *Service) GetDevice(ctx context.Context, id string) (Device, error) {
	d, err := scanDevice(s.db.QueryRowContext(ctx, `SELECT `+deviceCols+` FROM devices WHERE id = ?`, id))
	if errors.Is(err, sql.ErrNoRows) {
		return Device{}, ErrNotFound
	}
	if err != nil {
		return Device{}, internal(err)
	}
	return d, nil
}

// ListDevices liefert alle Geräte (Clients-Seite), neueste zuerst.
func (s *Service) ListDevices(ctx context.Context) ([]Device, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT `+deviceCols+` FROM devices ORDER BY created_at DESC, name`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	var out []Device
	for rows.Next() {
		d, err := scanDevice(rows)
		if err != nil {
			return nil, internal(err)
		}
		out = append(out, d)
	}
	return out, rows.Err()
}

// RevokeDevice sperrt ein Gerät und löscht sofort alle seine Access Tokens (idempotent).
func (s *Service) RevokeDevice(ctx context.Context, deviceID string) error {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	var status string
	if err := tx.QueryRowContext(ctx, `SELECT status FROM devices WHERE id = ?`, deviceID).Scan(&status); errors.Is(err, sql.ErrNoRows) {
		return ErrNotFound
	} else if err != nil {
		return internal(err)
	}
	if status != string(DeviceRevoked) {
		if _, err := tx.ExecContext(ctx, `UPDATE devices SET status = 'revoked', revoked_at = ? WHERE id = ?`, s.now().Unix(), deviceID); err != nil {
			return internal(err)
		}
	}
	if _, err := tx.ExecContext(ctx, `DELETE FROM access_tokens WHERE device_id = ?`, deviceID); err != nil {
		return internal(err)
	}
	return internal2(tx.Commit())
}

// AccessToken ist ein ausgestelltes Access Token (Klartext nur hier, gespeichert wird der Hash).
type AccessToken struct {
	Token     string
	ExpiresIn time.Duration
}

// IssueAccessToken tauscht ein Device Credential gegen ein Access Token (15 min).
// Fehler: ErrInvalidCredentials (unbekannt/falsch), ErrDeviceRevoked (gültiges Credential, Gerät gesperrt).
func (s *Service) IssueAccessToken(ctx context.Context, deviceID, credential string) (AccessToken, error) {
	var hash, status string
	err := s.db.QueryRowContext(ctx, `SELECT credential_hash, status FROM devices WHERE id = ?`, deviceID).Scan(&hash, &status)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return AccessToken{}, internal(err)
	}
	known := err == nil
	if !known {
		hash = auth.HashToken("fbd_dummy") // gleiche Laufzeit wie bei bekanntem Gerät
	}
	match := strings.HasPrefix(credential, auth.PrefixDevice) &&
		subtle.ConstantTimeCompare([]byte(auth.HashToken(credential)), []byte(hash)) == 1
	if !known || !match {
		return AccessToken{}, ErrInvalidCredentials
	}
	if status != string(DeviceTrusted) {
		return AccessToken{}, ErrDeviceRevoked
	}
	tok, err := auth.NewToken(auth.PrefixAccess)
	if err != nil {
		return AccessToken{}, internal(err)
	}
	now := s.now()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return AccessToken{}, internal(err)
	}
	defer tx.Rollback()
	// Gerät in derselben Transaktion erneut auf trusted prüfen (Revoke-Race).
	res, err := tx.ExecContext(ctx, `UPDATE devices SET last_seen_at = ? WHERE id = ? AND status = 'trusted'`, now.Unix(), deviceID)
	if err != nil {
		return AccessToken{}, internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return AccessToken{}, ErrDeviceRevoked
	}
	if _, err := tx.ExecContext(ctx, `INSERT INTO access_tokens(token_hash, device_id, expires_at) VALUES (?,?,?)`,
		auth.HashToken(tok), deviceID, now.Add(AccessTokenTTL).Unix()); err != nil {
		return AccessToken{}, internal(err)
	}
	if err := tx.Commit(); err != nil {
		return AccessToken{}, internal(err)
	}
	return AccessToken{Token: tok, ExpiresIn: AccessTokenTTL}, nil
}

// Principal ist die authentifizierte Identität hinter einem Access Token.
type Principal struct {
	Device Device
	User   User
}

// Authenticate prüft ein Access Token bei jeder Nutzung: gültig, nicht abgelaufen, Gerät trusted.
// Aktualisiert last_seen_at. Fehler: ErrUnauthorized, ErrDeviceRevoked.
func (s *Service) Authenticate(ctx context.Context, token string) (Principal, error) {
	if !strings.HasPrefix(token, auth.PrefixAccess) || len(token) > 256 {
		return Principal{}, ErrUnauthorized
	}
	var p Principal
	var exp, dCreated, uCreated int64
	var seen, rev sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT t.expires_at,
		d.id, d.user_id, d.name, d.platform, d.arch, d.player_version, d.status, d.created_at, d.last_seen_at, d.revoked_at,
		u.id, u.username, u.display_name, u.role, u.created_at
		FROM access_tokens t JOIN devices d ON d.id = t.device_id JOIN users u ON u.id = d.user_id
		WHERE t.token_hash = ?`, auth.HashToken(token)).
		Scan(&exp, &p.Device.ID, &p.Device.UserID, &p.Device.Name, &p.Device.Platform, &p.Device.Arch, &p.Device.PlayerVersion,
			&p.Device.Status, &dCreated, &seen, &rev, &p.User.ID, &p.User.Username, &p.User.DisplayName, &p.User.Role, &uCreated)
	if errors.Is(err, sql.ErrNoRows) {
		return Principal{}, ErrUnauthorized
	}
	if err != nil {
		return Principal{}, internal(err)
	}
	now := s.now()
	if exp <= now.Unix() {
		return Principal{}, ErrUnauthorized
	}
	if p.Device.Status != DeviceTrusted {
		return Principal{}, ErrDeviceRevoked
	}
	p.Device.CreatedAt = time.Unix(dCreated, 0).UTC()
	p.Device.LastSeenAt, p.Device.RevokedAt = ts(seen), ts(rev)
	p.User.CreatedAt = time.Unix(uCreated, 0).UTC()
	if _, err := s.db.ExecContext(ctx, `UPDATE devices SET last_seen_at = ? WHERE id = ?`, now.Unix(), p.Device.ID); err != nil {
		return Principal{}, internal(err)
	}
	t := now.UTC().Truncate(time.Second)
	p.Device.LastSeenAt = &t
	return p, nil
}

// HandshakeInput enthält die für Phase 1 relevanten Handshake-Felder.
type HandshakeInput struct {
	Platform           string
	Arch               string
	PlayerVersion      string
	ProtocolVersion    int
	MinProtocolVersion int
	// Cores, Video, Audio, Input: noch nicht ausgewertet (siehe TODO in Handshake).
}

// Problem beschreibt eine Inkompatibilität (HandshakeProblem der Spec).
type Problem struct {
	Code   Code
	Detail string
	CoreID string
}

// HandshakeResult ist das Ergebnis der Kompatibilitätsprüfung.
type HandshakeResult struct {
	Info       Info
	Compatible bool
	Problems   []Problem
}

// Problem-Codes des Handshakes.
const (
	ProblemPlayerTooOld Code = "player_too_old"
	ProblemHubTooOld    Code = "hub_too_old"
)

// Handshake prüft die Protokollversionen und speichert die gemeldeten Geräteinfos.
// TODO(Phase 5): Core-Prüfung (core_missing, core_version_mismatch) gegen die Core-Registry.
// TODO(Phase 4): Codec-/Capability-Prüfung (capability_missing) für Sessions.
func (s *Service) Handshake(ctx context.Context, deviceID string, in HandshakeInput) (HandshakeResult, error) {
	if in.ProtocolVersion < 1 || in.MinProtocolVersion < 1 || in.MinProtocolVersion > in.ProtocolVersion {
		return HandshakeResult{}, badRequest("protocol_version und min_protocol_version müssen ab 1 und konsistent sein")
	}
	if len(in.Platform) > 100 || len(in.Arch) > 100 || len(in.PlayerVersion) > 100 {
		return HandshakeResult{}, badRequest("Feld zu lang")
	}
	info := s.Info()
	res := HandshakeResult{Info: info, Compatible: true, Problems: []Problem{}}
	if in.ProtocolVersion < info.MinProtocolVersion {
		res.Problems = append(res.Problems, Problem{Code: ProblemPlayerTooOld,
			Detail: "Player-Protokollversion unter dem Minimum des Hub"})
	}
	if info.ProtocolVersion < in.MinProtocolVersion {
		res.Problems = append(res.Problems, Problem{Code: ProblemHubTooOld,
			Detail: "Hub-Protokollversion unter dem Minimum des Players"})
	}
	res.Compatible = len(res.Problems) == 0
	if _, err := s.db.ExecContext(ctx, `UPDATE devices SET platform = ?, arch = ?, player_version = ? WHERE id = ?`,
		in.Platform, in.Arch, in.PlayerVersion, deviceID); err != nil {
		return HandshakeResult{}, internal(err)
	}
	return res, nil
}
