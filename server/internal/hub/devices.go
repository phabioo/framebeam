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

// DeviceStatus is the trust status of a device.
type DeviceStatus string

const (
	DeviceTrusted DeviceStatus = "trusted"
	DeviceRevoked DeviceStatus = "revoked"
)

// Device is a paired FrameBeam Player. The player reports the ID itself.
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

// GetDevice returns a device by ID.
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

// ListDevices returns all devices (Clients page), newest first.
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

// RevokeDevice revokes a device and immediately deletes all its access tokens (idempotent).
func (s *Service) RevokeDevice(ctx context.Context, deviceID string) (err error) {
	defer s.publishOK(&err, TopicClients)
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
	if err := tx.Commit(); err != nil {
		return internal(err)
	}
	s.onDeviceRevoked(deviceID)
	return nil
}

func boolInt(b *bool) any {
	if b == nil {
		return nil
	}
	if *b {
		return 1
	}
	return 0
}

// AccessToken is an issued access token (plaintext only here; the hash is stored).
type AccessToken struct {
	Token     string
	ExpiresIn time.Duration
}

// IssueAccessToken exchanges a device credential for an access token (15 min).
// Errors: ErrInvalidCredentials (unknown/wrong), ErrDeviceRevoked (valid credential, device revoked).
func (s *Service) IssueAccessToken(ctx context.Context, deviceID, credential string) (AccessToken, error) {
	var hash, status string
	var disabled sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT d.credential_hash, d.status, u.disabled_at FROM devices d JOIN users u ON u.id = d.user_id
		WHERE d.id = ?`, deviceID).Scan(&hash, &status, &disabled)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return AccessToken{}, internal(err)
	}
	known := err == nil
	if !known {
		hash = auth.HashToken("fbd_dummy") // same run time as for a known device
	}
	match := strings.HasPrefix(credential, auth.PrefixDevice) &&
		subtle.ConstantTimeCompare([]byte(auth.HashToken(credential)), []byte(hash)) == 1
	if !known || !match {
		return AccessToken{}, ErrInvalidCredentials
	}
	if status != string(DeviceTrusted) {
		return AccessToken{}, ErrDeviceRevoked
	}
	if disabled.Valid {
		return AccessToken{}, ErrUserDisabled
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
	// Re-check in the same transaction that the device is trusted (revoke race).
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

// Principal is the authenticated identity behind an access token.
type Principal struct {
	Device Device
	User   User
}

// Authenticate checks an access token on every use: valid, not expired, device trusted.
// Updates last_seen_at. Errors: ErrUnauthorized, ErrDeviceRevoked.
func (s *Service) Authenticate(ctx context.Context, token string) (Principal, error) {
	if !strings.HasPrefix(token, auth.PrefixAccess) || len(token) > 256 {
		return Principal{}, ErrUnauthorized
	}
	var p Principal
	var exp, dCreated, uCreated int64
	var seen, rev, disabled sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT t.expires_at,
		d.id, d.user_id, d.name, d.platform, d.arch, d.player_version, d.status, d.created_at, d.last_seen_at, d.revoked_at,
		u.id, u.username, u.display_name, u.role, u.created_at, u.disabled_at
		FROM access_tokens t JOIN devices d ON d.id = t.device_id JOIN users u ON u.id = d.user_id
		WHERE t.token_hash = ?`, auth.HashToken(token)).
		Scan(&exp, &p.Device.ID, &p.Device.UserID, &p.Device.Name, &p.Device.Platform, &p.Device.Arch, &p.Device.PlayerVersion,
			&p.Device.Status, &dCreated, &seen, &rev, &p.User.ID, &p.User.Username, &p.User.DisplayName, &p.User.Role, &uCreated, &disabled)
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
	if disabled.Valid {
		return Principal{}, ErrUserDisabled
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

// HandshakeInput contains the handshake fields relevant for the Hub.
type HandshakeInput struct {
	Platform           string
	Arch               string
	PlayerVersion      string
	ProtocolVersion    int
	MinProtocolVersion int
	// H264Encode/H264Decode are the reported codec capabilities (nil = not reported); sessions check them.
	H264Encode *bool
	H264Decode *bool
	// Cores are the reported cores; nil = not reported (no core check, no cores stored).
	Cores *[]CoreReport
	// Audio, Input: not evaluated.
}

// Problem describes an incompatibility (HandshakeProblem in the spec).
type Problem struct {
	Code   Code
	Detail string
	CoreID string
}

// HandshakeResult is the result of the compatibility check.
type HandshakeResult struct {
	Info       Info
	Compatible bool
	Problems   []Problem
}

// Problem codes of the handshake.
const (
	ProblemPlayerTooOld Code = "player_too_old"
	ProblemHubTooOld    Code = "hub_too_old"
)

// Handshake checks the protocol versions and stores the reported device info.
// The reported cores are compared with the registry (core_missing, core_version_mismatch: warnings, compatible stays
// true) and the last report is stored per device.
// The codec capabilities are stored; the capability_missing check happens when publishing/joining a Session.
func (s *Service) Handshake(ctx context.Context, deviceID string, in HandshakeInput) (_ HandshakeResult, err error) {
	defer s.publishOK(&err, TopicClients, TopicSystems)
	if in.ProtocolVersion < 1 || in.MinProtocolVersion < 1 || in.MinProtocolVersion > in.ProtocolVersion {
		return HandshakeResult{}, badRequest("protocol_version and min_protocol_version must be at least 1 and consistent")
	}
	if len(in.Platform) > 100 || len(in.Arch) > 100 || len(in.PlayerVersion) > 100 {
		return HandshakeResult{}, badRequest("Field too long")
	}
	info := s.Info()
	res := HandshakeResult{Info: info, Compatible: true, Problems: []Problem{}}
	if in.ProtocolVersion < info.MinProtocolVersion {
		res.Problems = append(res.Problems, Problem{Code: ProblemPlayerTooOld,
			Detail: "Player protocol version below the hub's minimum"})
	}
	if info.ProtocolVersion < in.MinProtocolVersion {
		res.Problems = append(res.Problems, Problem{Code: ProblemHubTooOld,
			Detail: "Hub protocol version below the player's minimum"})
	}
	res.Compatible = len(res.Problems) == 0 // only protocol problems so far
	if in.Cores != nil {
		reg, err := s.ListRegistry(ctx)
		if err != nil {
			return HandshakeResult{}, err
		}
		res.Problems = append(res.Problems, checkCores(reg, *in.Cores)...)
	}
	if _, err := s.db.ExecContext(ctx, `UPDATE devices SET platform = ?, arch = ?, player_version = ?,
		h264_encode = COALESCE(?, h264_encode), h264_decode = COALESCE(?, h264_decode) WHERE id = ?`,
		in.Platform, in.Arch, in.PlayerVersion, boolInt(in.H264Encode), boolInt(in.H264Decode), deviceID); err != nil {
		return HandshakeResult{}, internal(err)
	}
	if in.Cores != nil {
		if err := s.storeReport(ctx, deviceID, in, *in.Cores); err != nil {
			return HandshakeResult{}, err
		}
	}
	return res, nil
}
