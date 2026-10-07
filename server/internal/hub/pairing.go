package hub

import (
	"context"
	"database/sql"
	"errors"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// PairingStatus is the status of a pairing request.
type PairingStatus string

const (
	PairingPending  PairingStatus = "pending"
	PairingApproved PairingStatus = "approved"
	PairingDenied   PairingStatus = "denied"
	PairingExpired  PairingStatus = "expired"
	PairingConsumed PairingStatus = "consumed"
)

// PairingInput is a pairing request from a player. RemoteAddr is the IP (without port).
type PairingInput struct {
	DeviceID        string
	DeviceName      string
	Platform        string
	Arch            string
	PlayerVersion   string
	ProtocolVersion int
	RemoteAddr      string
}

// PairingCreated is the response to a new request; PollToken is delivered in plaintext only here.
type PairingCreated struct {
	RequestID string
	PollToken string
	ExpiresIn time.Duration
}

// PairingRequest is a request for the pending list in the web interface.
type PairingRequest struct {
	ID              string
	DeviceID        string
	DeviceName      string
	Platform        string
	Arch            string
	PlayerVersion   string
	ProtocolVersion int
	RemoteAddr      string
	Status          PairingStatus
	UserID          string
	CreatedAt       time.Time
	ExpiresAt       time.Time
}

// PairingResult is the poll result. HubID, UserID and DeviceCredential are set only when approved
// and are delivered exactly once.
type PairingResult struct {
	Status           PairingStatus
	HubID            string
	UserID           string
	DeviceCredential string
}

func validPairingField(v string, max int) bool { return v != "" && len(v) <= max }

// CreatePairingRequest accepts a request (no auth). Limits: 20 open requests in total and
// 5 requests per remote IP per minute, otherwise ErrRateLimited.
// An already trusted device stays untouched until an admin allows the new request.
func (s *Service) CreatePairingRequest(ctx context.Context, in PairingInput) (_ PairingCreated, err error) {
	defer s.publishOK(&err, TopicClients)
	if _, err := uuid.Parse(in.DeviceID); err != nil {
		return PairingCreated{}, badRequest("device_id must be a UUID")
	}
	if !validPairingField(in.DeviceName, 100) || !validPairingField(in.Platform, 50) ||
		!validPairingField(in.Arch, 50) || !validPairingField(in.PlayerVersion, 50) || in.ProtocolVersion < 1 {
		return PairingCreated{}, badRequest("Required field missing or too long")
	}
	poll, err := auth.NewToken(auth.PrefixPoll)
	if err != nil {
		return PairingCreated{}, internal(err)
	}
	id := uuid.NewString()
	now := s.now()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return PairingCreated{}, internal(err)
	}
	defer tx.Rollback()
	var open, perIP int
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM pairing_requests WHERE status = 'pending' AND expires_at > ?`, now.Unix()).Scan(&open); err != nil {
		return PairingCreated{}, internal(err)
	}
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM pairing_requests WHERE remote_addr = ? AND created_at > ?`,
		in.RemoteAddr, now.Add(-time.Minute).Unix()).Scan(&perIP); err != nil {
		return PairingCreated{}, internal(err)
	}
	if open >= MaxOpenPairingRequests || perIP >= MaxPairingPerIPPerMinute {
		return PairingCreated{}, &Error{Code: CodeRateLimited, Message: "Too many open requests"}
	}
	if _, err := tx.ExecContext(ctx, `INSERT INTO pairing_requests(id, poll_token_hash, device_id, device_name, platform, arch,
		player_version, protocol_version, remote_addr, status, created_at, expires_at) VALUES (?,?,?,?,?,?,?,?,?,'pending',?,?)`,
		id, auth.HashToken(poll), in.DeviceID, in.DeviceName, in.Platform, in.Arch, in.PlayerVersion, in.ProtocolVersion,
		in.RemoteAddr, now.Unix(), now.Add(PairingTTL).Unix()); err != nil {
		return PairingCreated{}, internal(err)
	}
	if err := tx.Commit(); err != nil {
		return PairingCreated{}, internal(err)
	}
	return PairingCreated{RequestID: id, PollToken: poll, ExpiresIn: PairingTTL}, nil
}

func (s *Service) effectiveStatus(status string, expires int64) PairingStatus {
	if status == string(PairingPending) && expires <= s.now().Unix() {
		return PairingExpired
	}
	return PairingStatus(status)
}

// ListPendingRequests returns open (non-expired) requests, oldest first.
func (s *Service) ListPendingRequests(ctx context.Context) ([]PairingRequest, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT id, device_id, device_name, platform, arch, player_version, protocol_version,
		remote_addr, status, COALESCE(user_id,''), created_at, expires_at
		FROM pairing_requests WHERE status = 'pending' AND expires_at > ? ORDER BY created_at`, s.now().Unix())
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	var out []PairingRequest
	for rows.Next() {
		var r PairingRequest
		var c, e int64
		if err := rows.Scan(&r.ID, &r.DeviceID, &r.DeviceName, &r.Platform, &r.Arch, &r.PlayerVersion, &r.ProtocolVersion,
			&r.RemoteAddr, &r.Status, &r.UserID, &c, &e); err != nil {
			return nil, internal(err)
		}
		r.CreatedAt, r.ExpiresAt = time.Unix(c, 0).UTC(), time.Unix(e, 0).UTC()
		out = append(out, r)
	}
	return out, rows.Err()
}

// ApprovePairing allows an open request and assigns it to the user userID (admin).
// The device is registered only on the first poll; the deadline is extended to 10 min from now.
func (s *Service) ApprovePairing(ctx context.Context, requestID, userID string) (err error) {
	defer s.publishOK(&err, TopicClients)
	if _, err := s.GetUser(ctx, userID); err != nil {
		if errors.Is(err, ErrNotFound) {
			return badRequest("User does not exist")
		}
		return err
	}
	return s.decide(ctx, requestID, func(tx *sql.Tx, now time.Time) error {
		_, err := tx.ExecContext(ctx, `UPDATE pairing_requests SET status = 'approved', user_id = ?, expires_at = ? WHERE id = ?`,
			userID, now.Add(PairingTTL).Unix(), requestID)
		return err
	})
}

// DenyPairing denies an open request.
func (s *Service) DenyPairing(ctx context.Context, requestID string) (err error) {
	defer s.publishOK(&err, TopicClients)
	return s.decide(ctx, requestID, func(tx *sql.Tx, _ time.Time) error {
		_, err := tx.ExecContext(ctx, `UPDATE pairing_requests SET status = 'denied' WHERE id = ?`, requestID)
		return err
	})
}

func (s *Service) decide(ctx context.Context, requestID string, apply func(*sql.Tx, time.Time) error) error {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	var status string
	var exp int64
	err = tx.QueryRowContext(ctx, `SELECT status, expires_at FROM pairing_requests WHERE id = ?`, requestID).Scan(&status, &exp)
	if errors.Is(err, sql.ErrNoRows) {
		return ErrNotFound
	}
	if err != nil {
		return internal(err)
	}
	switch s.effectiveStatus(status, exp) {
	case PairingPending:
	case PairingExpired:
		return ErrPairingExpired
	default:
		return conflict("Request has already been handled")
	}
	if err := apply(tx, s.now()); err != nil {
		return internal(err)
	}
	return internal2(tx.Commit())
}

// PollPairing returns the status of a request (poll token required: ErrUnauthorized).
// When approved, the device credential is created now and stored only as a hash, the device is registered
// or re-enabled, the request is consumed and the credential is delivered exactly this once.
// Afterwards: ErrNotFound.
func (s *Service) PollPairing(ctx context.Context, requestID, pollToken string) (PairingResult, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return PairingResult{}, internal(err)
	}
	defer tx.Rollback()
	var r PairingRequest
	var hash, status string
	var exp, created int64
	err = tx.QueryRowContext(ctx, `SELECT poll_token_hash, status, expires_at, created_at, device_id, device_name, platform, arch,
		player_version, COALESCE(user_id,'') FROM pairing_requests WHERE id = ?`, requestID).
		Scan(&hash, &status, &exp, &created, &r.DeviceID, &r.DeviceName, &r.Platform, &r.Arch, &r.PlayerVersion, &r.UserID)
	if errors.Is(err, sql.ErrNoRows) {
		return PairingResult{}, ErrNotFound
	}
	if err != nil {
		return PairingResult{}, internal(err)
	}
	if !auth.EqualHash(auth.HashToken(pollToken), hash) {
		return PairingResult{}, ErrUnauthorized
	}
	eff := s.effectiveStatus(status, exp)
	switch eff {
	case PairingConsumed:
		return PairingResult{}, ErrNotFound
	case PairingPending, PairingDenied, PairingExpired:
		return PairingResult{Status: eff}, nil
	}
	// approved
	now := s.now()
	if exp <= now.Unix() {
		return PairingResult{Status: PairingExpired}, nil
	}
	cred, err := auth.NewToken(auth.PrefixDevice)
	if err != nil {
		return PairingResult{}, internal(err)
	}
	if _, err := tx.ExecContext(ctx, `INSERT INTO devices(id, user_id, name, platform, arch, player_version, credential_hash, status, created_at)
		VALUES (?,?,?,?,?,?,?,'trusted',?)
		ON CONFLICT(id) DO UPDATE SET user_id = excluded.user_id, name = excluded.name, platform = excluded.platform,
			arch = excluded.arch, player_version = excluded.player_version, credential_hash = excluded.credential_hash,
			status = 'trusted', revoked_at = NULL`,
		r.DeviceID, r.UserID, r.DeviceName, r.Platform, r.Arch, r.PlayerVersion, auth.HashToken(cred), now.Unix()); err != nil {
		return PairingResult{}, internal(err)
	}
	// New credential: this device's old access tokens are no longer valid.
	if _, err := tx.ExecContext(ctx, `DELETE FROM access_tokens WHERE device_id = ?`, r.DeviceID); err != nil {
		return PairingResult{}, internal(err)
	}
	if _, err := tx.ExecContext(ctx, `UPDATE pairing_requests SET status = 'consumed' WHERE id = ?`, requestID); err != nil {
		return PairingResult{}, internal(err)
	}
	if err := tx.Commit(); err != nil {
		return PairingResult{}, internal(err)
	}
	return PairingResult{Status: PairingApproved, HubID: s.Info().HubID, UserID: r.UserID, DeviceCredential: cred}, nil
}
