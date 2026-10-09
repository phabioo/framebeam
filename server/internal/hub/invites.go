package hub

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"database/sql"
	"errors"
	"strconv"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// Invite limits.
const (
	// MaxRedeemPerIPPerMinute limits redeem attempts per remote IP (like pairing requests).
	MaxRedeemPerIPPerMinute = 5
	// MaxRedeemFailuresPerMinute limits failed attempts over all IPs: only a safety net, far above what
	// per-IP limits let through from a few sources, so a single client cannot lock everyone out.
	MaxRedeemFailuresPerMinute = 1000
	MaxDisplayNameLen          = 32
	inviteHistoryKeep          = 90 * 24 * time.Hour
)

// InviteTTLs are the selectable invite lifetimes (Users page).
var InviteTTLs = []time.Duration{15 * time.Minute, time.Hour, 24 * time.Hour}

// DefaultInviteTTL is the preselected lifetime.
const DefaultInviteTTL = time.Hour

// inviteAlphabet: Crockford-style base32 without the ambiguous symbols 0, 1, O, I and L (31 symbols).
const inviteAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ"

// InviteStatus is the status of an invite as shown in the history.
type InviteStatus string

const (
	InviteActive   InviteStatus = "active"
	InviteRedeemed InviteStatus = "redeemed"
	InviteRevoked  InviteStatus = "revoked"
	InviteExpired  InviteStatus = "expired"
)

// Invite is an onboarding invite. The code itself is never stored; it exists in plaintext only in the
// result of CreateInvite.
type Invite struct {
	ID              string
	AuthorizeDevice bool
	Status          InviteStatus
	CreatedAt       time.Time
	ExpiresAt       time.Time
	RedeemedAt      *time.Time
	RevokedAt       *time.Time
	// RedeemedBy is the display name of the user created by the redemption.
	RedeemedBy string
}

// newInviteCode creates a code of the form FB-XXXX-XXXX.
func newInviteCode() (string, error) {
	var raw [8]byte
	if _, err := rand.Read(raw[:]); err != nil {
		return "", err
	}
	var b strings.Builder
	b.WriteString("FB-")
	for i, v := range raw {
		if i == 4 {
			b.WriteByte('-')
		}
		// 256 mod 31 = 8: reject the top values so every symbol is equally likely.
		for v >= 248 {
			var one [1]byte
			if _, err := rand.Read(one[:]); err != nil {
				return "", err
			}
			v = one[0]
		}
		b.WriteByte(inviteAlphabet[int(v)%len(inviteAlphabet)])
	}
	return b.String(), nil
}

// normalizeInviteCode uppercases and strips spaces/dashes; it returns the canonical FB-XXXX-XXXX form or "".
func normalizeInviteCode(in string) string {
	in = strings.ToUpper(strings.TrimSpace(in))
	in = strings.NewReplacer("-", "", " ", "").Replace(in)
	if len(in) != 10 || !strings.HasPrefix(in, "FB") {
		return ""
	}
	body := in[2:]
	for _, r := range body {
		if !strings.ContainsRune(inviteAlphabet, r) {
			return ""
		}
	}
	return "FB-" + body[:4] + "-" + body[4:]
}

func hashInviteCode(code string) string { return auth.HashToken("invite:" + code) }

// CreateInvite creates an invite (admin). ttl must be one of InviteTTLs. The plaintext code is returned only here.
func (s *Service) CreateInvite(ctx context.Context, createdBy string, ttl time.Duration, authorizeDevice bool) (_ Invite, _ string, err error) {
	defer s.publishOK(&err, TopicUsers)
	valid := false
	for _, t := range InviteTTLs {
		valid = valid || t == ttl
	}
	if !valid {
		return Invite{}, "", badRequest("Expiry must be 15 minutes, 1 hour or 24 hours")
	}
	code, err := newInviteCode()
	if err != nil {
		return Invite{}, "", internal(err)
	}
	now := s.now().Truncate(time.Second).UTC()
	inv := Invite{ID: uuid.NewString(), AuthorizeDevice: authorizeDevice, Status: InviteActive, CreatedAt: now, ExpiresAt: now.Add(ttl)}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO invites(id, code_hash, authorize_device, status, created_by, created_at, expires_at)
		VALUES (?,?,?, 'active', ?,?,?)`, inv.ID, hashInviteCode(code), boolToInt(authorizeDevice), createdBy, now.Unix(), inv.ExpiresAt.Unix()); err != nil {
		return Invite{}, "", internal(err)
	}
	return inv, code, nil
}

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

// RevokeInvite revokes an active invite (ErrNotFound if unknown or no longer active).
func (s *Service) RevokeInvite(ctx context.Context, id string) (err error) {
	defer s.publishOK(&err, TopicUsers)
	res, err := s.db.ExecContext(ctx, `UPDATE invites SET status = 'revoked', revoked_at = ?
		WHERE id = ? AND status = 'active' AND expires_at > ?`, s.now().Unix(), id, s.now().Unix())
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return nil
}

// ListInvites returns the invites, newest first (at most limit; 0 = all).
func (s *Service) ListInvites(ctx context.Context, limit int) ([]Invite, error) {
	q := `SELECT i.id, i.authorize_device, i.status, i.created_at, i.expires_at, i.redeemed_at, i.revoked_at, COALESCE(u.display_name,'')
		FROM invites i LEFT JOIN users u ON u.id = i.redeemed_by ORDER BY i.created_at DESC, i.rowid DESC`
	var args []any
	if limit > 0 {
		q += ` LIMIT ?`
		args = append(args, limit)
	}
	rows, err := s.db.QueryContext(ctx, q, args...)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	now := s.now().Unix()
	var out []Invite
	for rows.Next() {
		var i Invite
		var auth int
		var created, exp int64
		var red, rev sql.NullInt64
		if err := rows.Scan(&i.ID, &auth, &i.Status, &created, &exp, &red, &rev, &i.RedeemedBy); err != nil {
			return nil, internal(err)
		}
		i.AuthorizeDevice = auth == 1
		i.CreatedAt, i.ExpiresAt, i.RedeemedAt, i.RevokedAt = time.Unix(created, 0).UTC(), time.Unix(exp, 0).UTC(), ts(red), ts(rev)
		if i.Status == InviteActive && exp <= now {
			i.Status = InviteExpired
		}
		out = append(out, i)
	}
	return out, rows.Err()
}

// RedeemInput is the redeem request of a Player. RemoteAddr is the IP (without port).
type RedeemInput struct {
	Code            string
	DisplayName     string
	DeviceID        string
	DeviceName      string
	Platform        string
	Arch            string
	PlayerVersion   string
	ProtocolVersion int
	RemoteAddr      string
}

// RedeemResult is the result of a redemption. Approved: Credential is set (device trusted). Otherwise a pending
// pairing request pre-assigned to the new user was created (RequestID, PollToken, ExpiresIn).
type RedeemResult struct {
	Approved   bool
	HubID      string
	UserID     string
	Credential string
	RequestID  string
	PollToken  string
	ExpiresIn  time.Duration
}

// rateLimitRedeem counts this attempt; ErrRateLimited when the per-IP limit is reached.
func (s *Service) rateLimitRedeem(ip string) error {
	now := s.now()
	cut := now.Add(-time.Minute)
	s.redeemMu.Lock()
	defer s.redeemMu.Unlock()
	if s.redeemHits == nil {
		s.redeemHits = map[string][]time.Time{}
	}
	prune := func(h []time.Time) []time.Time {
		i := 0
		for i < len(h) && !h[i].After(cut) {
			i++
		}
		return h[i:]
	}
	ip = limitKey(ip)
	s.redeemFails = prune(s.redeemFails)
	h := prune(s.redeemHits[ip])
	if len(h) >= MaxRedeemPerIPPerMinute || len(s.redeemFails) >= MaxRedeemFailuresPerMinute {
		s.redeemHits[ip] = h
		return &Error{Code: CodeRateLimited, Message: "Too many attempts"}
	}
	s.redeemHits[ip] = append(h, now)
	if len(s.redeemHits) > 1024 { // bound the map: drop idle entries
		for k, v := range s.redeemHits {
			if len(prune(v)) == 0 {
				delete(s.redeemHits, k)
			}
		}
	}
	return nil
}

func (s *Service) noteRedeemFailure() {
	s.redeemMu.Lock()
	s.redeemFails = append(s.redeemFails, s.now())
	s.redeemMu.Unlock()
}

// cleanDisplayName trims, removes control characters and checks 1-32 characters.
func cleanDisplayName(v string) (string, error) {
	v = strings.TrimSpace(strings.Map(func(r rune) rune {
		if unicode.IsControl(r) {
			return -1
		}
		return r
	}, v))
	if n := utf8.RuneCountInString(v); n < 1 || n > MaxDisplayNameLen {
		return "", badRequest("Display name must be 1 to %d characters long", MaxDisplayNameLen)
	}
	return v, nil
}

// slugUsername derives a username from a display name (lowercase a-z0-9._-).
func slugUsername(name string) string {
	var b strings.Builder
	dash := false
	for _, r := range strings.ToLower(name) {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '.' || r == '_' {
			b.WriteRune(r)
			dash = false
		} else if !dash && b.Len() > 0 {
			b.WriteByte('-')
			dash = true
		}
	}
	out := strings.Trim(b.String(), "-")
	if out == "" {
		out = "user"
	}
	if len(out) > 56 {
		out = out[:56]
	}
	return out
}

// RedeemInvite redeems an onboarding invite: creates the user (role user, no password) and either a trusted
// device with a device credential (invite authorizes the first device) or a pending pairing request that is
// pre-assigned to the new user. Everything happens in one transaction: the invite is consumed exactly once;
// a taken display name or any other failure leaves it usable. Invalid, expired, used and revoked codes are not
// distinguishable (ErrInviteInvalid).
func (s *Service) RedeemInvite(ctx context.Context, in RedeemInput) (_ RedeemResult, err error) {
	defer s.publishOK(&err, TopicUsers, TopicClients)
	if _, err := uuid.Parse(in.DeviceID); err != nil {
		return RedeemResult{}, badRequest("device_id must be a UUID")
	}
	if !validPairingField(in.DeviceName, 100) || !validPairingField(in.Platform, 50) ||
		!validPairingField(in.Arch, 50) || !validPairingField(in.PlayerVersion, 50) || in.ProtocolVersion < 1 {
		return RedeemResult{}, badRequest("Required field missing or too long")
	}
	name, err := cleanDisplayName(in.DisplayName)
	if err != nil {
		return RedeemResult{}, err
	}
	if err := s.rateLimitRedeem(in.RemoteAddr); err != nil {
		return RedeemResult{}, err
	}
	code := normalizeInviteCode(in.Code)
	now := s.now()

	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return RedeemResult{}, internal(err)
	}
	defer tx.Rollback()

	// Compare against every open invite in constant time (no early exit, no lookup by the secret).
	want := []byte(hashInviteCode(code))
	rows, err := tx.QueryContext(ctx, `SELECT id, code_hash, authorize_device FROM invites WHERE status = 'active' AND expires_at > ?`, now.Unix())
	if err != nil {
		return RedeemResult{}, internal(err)
	}
	var inviteID string
	var authorize bool
	for rows.Next() {
		var id, hash string
		var auth int
		if err := rows.Scan(&id, &hash, &auth); err != nil {
			rows.Close()
			return RedeemResult{}, internal(err)
		}
		if code != "" && subtle.ConstantTimeCompare([]byte(hash), want) == 1 {
			inviteID, authorize = id, auth == 1
		}
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return RedeemResult{}, internal(err)
	}
	if inviteID == "" {
		s.noteRedeemFailure()
		return RedeemResult{}, ErrInviteInvalid
	}
	// Consume (guards against a concurrent redemption of the same invite).
	res, err := tx.ExecContext(ctx, `UPDATE invites SET status = 'redeemed', redeemed_at = ? WHERE id = ? AND status = 'active' AND expires_at > ?`,
		now.Unix(), inviteID, now.Unix())
	if err != nil {
		return RedeemResult{}, internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		s.noteRedeemFailure()
		return RedeemResult{}, ErrInviteInvalid
	}

	var taken int
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE display_name = ? COLLATE NOCASE`, name).Scan(&taken); err != nil {
		return RedeemResult{}, internal(err)
	}
	if taken > 0 {
		return RedeemResult{}, ErrDisplayNameTaken
	}
	base := slugUsername(name)
	username := base
	for i := 2; ; i++ {
		var n int
		if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE username = ? COLLATE NOCASE`, username).Scan(&n); err != nil {
			return RedeemResult{}, internal(err)
		}
		if n == 0 {
			break
		}
		username = base + "-" + strconv.Itoa(i)
	}
	userID := newUserID()
	if _, err := tx.ExecContext(ctx, `INSERT INTO users(id, username, display_name, role, password_hash, created_at) VALUES (?,?,?, 'user', NULL, ?)`,
		userID, username, name, now.Unix()); err != nil {
		if isUnique(err) {
			return RedeemResult{}, ErrDisplayNameTaken
		}
		return RedeemResult{}, internal(err)
	}
	if _, err := tx.ExecContext(ctx, `UPDATE invites SET redeemed_by = ? WHERE id = ?`, userID, inviteID); err != nil {
		return RedeemResult{}, internal(err)
	}

	out := RedeemResult{UserID: userID, HubID: s.Info().HubID}
	if authorize {
		var status string
		err := tx.QueryRowContext(ctx, `SELECT status FROM devices WHERE id = ?`, in.DeviceID).Scan(&status)
		if err == nil && status == string(DeviceTrusted) {
			return RedeemResult{}, conflict("This device is already paired with the Hub")
		} else if err != nil && !errors.Is(err, sql.ErrNoRows) {
			return RedeemResult{}, internal(err)
		}
		cred, err := auth.NewToken(auth.PrefixDevice)
		if err != nil {
			return RedeemResult{}, internal(err)
		}
		if _, err := tx.ExecContext(ctx, `INSERT INTO devices(id, user_id, name, platform, arch, player_version, credential_hash, status, created_at)
			VALUES (?,?,?,?,?,?,?,'trusted',?)
			ON CONFLICT(id) DO UPDATE SET user_id = excluded.user_id, name = excluded.name, platform = excluded.platform,
				arch = excluded.arch, player_version = excluded.player_version, credential_hash = excluded.credential_hash,
				status = 'trusted', revoked_at = NULL`,
			in.DeviceID, userID, in.DeviceName, in.Platform, in.Arch, in.PlayerVersion, auth.HashToken(cred), now.Unix()); err != nil {
			return RedeemResult{}, internal(err)
		}
		if _, err := tx.ExecContext(ctx, `DELETE FROM access_tokens WHERE device_id = ?`, in.DeviceID); err != nil {
			return RedeemResult{}, internal(err)
		}
		out.Approved, out.Credential = true, cred
	} else {
		var status string
		err := tx.QueryRowContext(ctx, `SELECT status FROM devices WHERE id = ?`, in.DeviceID).Scan(&status)
		if err == nil && status == string(DeviceTrusted) { // the new user cannot own it: never reassign on approval
			return RedeemResult{}, conflict("This device is already paired with the Hub")
		} else if err != nil && !errors.Is(err, sql.ErrNoRows) {
			return RedeemResult{}, internal(err)
		}
		var open int
		if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM pairing_requests WHERE status = 'pending' AND expires_at > ? AND user_id IS NOT NULL`,
			now.Unix()).Scan(&open); err != nil {
			return RedeemResult{}, internal(err)
		}
		if open >= MaxOpenInviteRequests {
			return RedeemResult{}, &Error{Code: CodeRateLimited, Message: "Too many open requests"}
		}
		poll, err := auth.NewToken(auth.PrefixPoll)
		if err != nil {
			return RedeemResult{}, internal(err)
		}
		out.RequestID, out.PollToken, out.ExpiresIn = uuid.NewString(), poll, PairingTTL
		if _, err := tx.ExecContext(ctx, `INSERT INTO pairing_requests(id, poll_token_hash, device_id, device_name, platform, arch,
			player_version, protocol_version, remote_addr, status, user_id, created_at, expires_at) VALUES (?,?,?,?,?,?,?,?,?,'pending',?,?,?)`,
			out.RequestID, auth.HashToken(poll), in.DeviceID, in.DeviceName, in.Platform, in.Arch, in.PlayerVersion, in.ProtocolVersion,
			in.RemoteAddr, userID, now.Unix(), now.Add(PairingTTL).Unix()); err != nil {
			return RedeemResult{}, internal(err)
		}
	}
	if err := tx.Commit(); err != nil {
		return RedeemResult{}, internal(err)
	}
	return out, nil
}
