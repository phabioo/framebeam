package hub

import (
	"context"
	"crypto/rand"
	"database/sql"
	"errors"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/auth"
)

// Server accounts (0.18 package 4, ADR 0030 D5, handshake feature `accounts_v1`): every user can have a password
// (admins must), a password signs a new device in without the admin's approval, a user manages their own account and
// devices, and "Confirm it's you" guards the dangerous steps for a short time.

// FeatureAccountsV1 is the handshake feature flag for server accounts.
const FeatureAccountsV1 = "accounts_v1"

// Sign-in kinds of a device (devices.sign_in_kind).
const (
	SignInApproval = "approval" // an admin approved a pairing request
	SignInInvite   = "invite"   // onboarding invite or pairing code
	SignInPassword = "password" // user name and password
	SignInLocal    = "local"    // the app on the same PC as the server
)

// Account limits.
const (
	// MaxPasswordBytes bounds the password so that hashing cannot be used to burn CPU.
	MaxPasswordBytes = 1024
	// MaxInstallIDLen is the longest install_id.
	MaxInstallIDLen = 64
	// MaxSignInPerIPPerMinute limits sign-in attempts per remote IP (IPv4 address or IPv6 /64).
	MaxSignInPerIPPerMinute = 10
	// SignInFailureLimit failures in a row lock the sign-in of that user name for SignInLockDuration.
	SignInFailureLimit = 10
	SignInLockDuration = 15 * time.Minute
	// ConfirmFailureLimit wrong passwords lock "Confirm it's you" on one device for ConfirmLockDuration.
	ConfirmFailureLimit = 3
	ConfirmLockDuration = 5 * time.Minute
	// failureMemory: failures older than this no longer count (the counter starts again).
	failureMemory = SignInLockDuration
	// DefaultConfirmSeconds is the confirmation lifetime when the app does not ask for one.
	DefaultConfirmSeconds = 300
)

// ConfirmDurations are the selectable confirmation lifetimes in seconds.
var ConfirmDurations = []int{120, 300, 900}

const settingRequirePasswords = "require_passwords"

// accountState holds the in-memory sign-in limits.
type accountState struct {
	mu      sync.Mutex
	hits    map[string][]time.Time // limit key of the source -> attempt times of the last minute
	unknown map[string]*nameFails  // lower-case user name that does not exist -> failures (same lockout as a real user)
}

type nameFails struct {
	n      int
	last   time.Time
	locked time.Time
}

// ---- Policy ----

// RequirePasswords reports whether the server requires every user to have a password (members cannot remove theirs).
func (s *Service) RequirePasswords(ctx context.Context) (bool, error) {
	v, _, err := s.getSetting(ctx, settingRequirePasswords)
	return v == "1", err
}

// SetRequirePasswords switches the policy. Existing users without a password are not touched.
func (s *Service) SetRequirePasswords(ctx context.Context, on bool) (err error) {
	defer s.publishOK(&err, TopicUsers)
	v := "0"
	if on {
		v = "1"
	}
	return s.setSetting(ctx, settingRequirePasswords, v)
}

// ---- Helpers ----

// checkNewPassword applies the password rules: at least MinPasswordLen characters, at most MaxPasswordBytes bytes.
func checkNewPassword(pw string) error {
	if len(pw) > MaxPasswordBytes {
		return badRequest("Password must be at most %d bytes long", MaxPasswordBytes)
	}
	if utf8.RuneCountInString(pw) < MinPasswordLen {
		return badRequest("Password must be at least %d characters long", MinPasswordLen)
	}
	return nil
}

func retryAfterSeconds(until, now time.Time) int {
	d := until.Sub(now)
	if d <= 0 {
		return 1
	}
	return int((d + time.Second - 1) / time.Second)
}

func rateLimitedUntil(msg string, until, now time.Time) *Error {
	return &Error{Code: CodeRateLimited, Message: msg, RetryAfter: retryAfterSeconds(until, now)}
}

type trustedDevice struct {
	ID, UserID, Name, Platform, Arch, Version, CredHash, Kind, InstallID string
}

// upsertTrustedDevice registers the device as trusted (or re-enables it) with a new credential, ends an earlier
// confirmation and deletes the device's old access tokens. The caller has checked that no other user owns the ID.
func upsertTrustedDevice(ctx context.Context, tx *sql.Tx, d trustedDevice, now int64) error {
	if _, err := tx.ExecContext(ctx, `INSERT INTO devices(id, user_id, name, platform, arch, player_version, credential_hash, status, created_at, sign_in_kind, install_id)
		VALUES (?,?,?,?,?,?,?,'trusted',?,?,NULLIF(?,''))
		ON CONFLICT(id) DO UPDATE SET user_id = excluded.user_id, name = excluded.name, platform = excluded.platform,
			arch = excluded.arch, player_version = excluded.player_version, credential_hash = excluded.credential_hash,
			status = 'trusted', revoked_at = NULL, sign_in_kind = excluded.sign_in_kind, install_id = excluded.install_id,
			confirmed_until = NULL`,
		d.ID, d.UserID, d.Name, d.Platform, d.Arch, d.Version, d.CredHash, now, d.Kind, d.InstallID); err != nil {
		return internal(err)
	}
	// New credential: this device's old access tokens are no longer valid.
	if _, err := tx.ExecContext(ctx, `DELETE FROM access_tokens WHERE device_id = ?`, d.ID); err != nil {
		return internal(err)
	}
	return nil
}

// ---- Sign in with a password ----

// SignInInput is a sign-in request of an app. RemoteAddr is the IP (without port).
type SignInInput struct {
	Username        string
	Password        string
	DeviceID        string
	DeviceName      string
	Platform        string
	Arch            string
	AppVersion      string
	ProtocolVersion int
	InstallID       string
	RemoteAddr      string
}

// SignInResult is the result of a successful sign-in; DeviceCredential is delivered exactly once.
type SignInResult struct {
	HubID            string
	UserID           string
	DeviceCredential string
}

// rateLimitSignIn counts this attempt of the source; ErrRateLimited when MaxSignInPerIPPerMinute is reached.
func (s *Service) rateLimitSignIn(addr string) error {
	now := s.now()
	cut := now.Add(-time.Minute)
	key := limitKey(addr)
	st := &s.acct
	st.mu.Lock()
	defer st.mu.Unlock()
	if st.hits == nil {
		st.hits = map[string][]time.Time{}
	}
	prune := func(h []time.Time) []time.Time {
		i := 0
		for i < len(h) && !h[i].After(cut) {
			i++
		}
		return h[i:]
	}
	h := prune(st.hits[key])
	if len(h) >= MaxSignInPerIPPerMinute {
		st.hits[key] = h
		return &Error{Code: CodeRateLimited, Message: "Too many attempts", RetryAfter: retryAfterSeconds(h[0].Add(time.Minute), now)}
	}
	st.hits[key] = append(h, now)
	if len(st.hits) > 1024 { // bound the map: drop idle entries
		for k, v := range st.hits {
			if len(prune(v)) == 0 {
				delete(st.hits, k)
			}
		}
	}
	return nil
}

// unknownLocked reports a lock on a user name that does not exist (kept in memory so that unknown and known names
// behave the same).
func (s *Service) unknownLocked(name string) (time.Time, bool) {
	st := &s.acct
	st.mu.Lock()
	defer st.mu.Unlock()
	if f := st.unknown[name]; f != nil && f.locked.After(s.now()) {
		return f.locked, true
	}
	return time.Time{}, false
}

// noteUnknownFailure counts a failure for a user name that does not exist; it returns the lock end when this
// failure locked the name.
func (s *Service) noteUnknownFailure(name string) (time.Time, bool) {
	now := s.now()
	st := &s.acct
	st.mu.Lock()
	defer st.mu.Unlock()
	if st.unknown == nil {
		st.unknown = map[string]*nameFails{}
	}
	if len(st.unknown) > 4096 {
		for k, f := range st.unknown {
			if !f.locked.After(now) && now.Sub(f.last) > failureMemory {
				delete(st.unknown, k)
			}
		}
		if len(st.unknown) > 4096 {
			st.unknown = map[string]*nameFails{}
		}
	}
	f := st.unknown[name]
	if f == nil {
		f = &nameFails{}
		st.unknown[name] = f
	}
	if now.Sub(f.last) > failureMemory {
		f.n = 0
	}
	f.n++
	f.last = now
	if f.n >= SignInFailureLimit {
		f.n = 0
		f.locked = now.Add(SignInLockDuration)
		return f.locked, true
	}
	return time.Time{}, false
}

// SignIn signs a device in with user name and password (no admin approval). Wrong user, wrong password and a user
// without a password all give ErrInvalidCredentials, with the same work done (a dummy hash is checked when there is
// nothing to check against). After SignInFailureLimit failures the user name is locked for SignInLockDuration
// (rate_limited with retry_after). A trusted device of another user is never taken over (conflict). Old access
// tokens of the device ID are deleted. A user with must_change_password can sign in; GetAccount reports it.
func (s *Service) SignIn(ctx context.Context, in SignInInput) (_ SignInResult, err error) {
	defer s.publishOK(&err, TopicClients, TopicUsers)
	if _, err := uuid.Parse(in.DeviceID); err != nil {
		return SignInResult{}, badRequest("device_id must be a UUID")
	}
	in.Username = strings.TrimSpace(in.Username)
	if !validPairingField(in.DeviceName, 100) || !validPairingField(in.Platform, 50) || !validPairingField(in.Arch, 50) ||
		!validPairingField(in.AppVersion, 50) || in.ProtocolVersion < 1 || len(in.InstallID) > MaxInstallIDLen ||
		in.Username == "" || len(in.Username) > 64 || in.Password == "" || len(in.Password) > MaxPasswordBytes {
		return SignInResult{}, badRequest("Required field missing or too long")
	}
	if err := s.rateLimitSignIn(in.RemoteAddr); err != nil {
		return SignInResult{}, err
	}
	now := s.now()
	var (
		u         User
		hash      sql.NullString
		created   int64
		dis       sql.NullInt64
		lockedTil sql.NullInt64
	)
	err = s.db.QueryRowContext(ctx, `SELECT `+userCols+`, password_hash, sign_in_locked_until FROM users WHERE username = ?`, in.Username).
		Scan(&u.ID, &u.Username, &u.DisplayName, &u.Role, &created, &dis, &hash, &lockedTil)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return SignInResult{}, internal(err)
	}
	exists := err == nil
	lname := strings.ToLower(in.Username)
	if exists && lockedTil.Valid && lockedTil.Int64 > now.Unix() {
		return SignInResult{}, rateLimitedUntil("Too many failed attempts; try again later", time.Unix(lockedTil.Int64, 0), now)
	}
	if !exists {
		if until, ok := s.unknownLocked(lname); ok {
			return SignInResult{}, rateLimitedUntil("Too many failed attempts; try again later", until, now)
		}
	}
	enc := s.dummyHash()
	if exists && hash.Valid {
		enc = hash.String
	}
	ok, verr := auth.VerifyPassword(in.Password, enc)
	if !exists || !hash.Valid || verr != nil || !ok {
		var until time.Time
		var locked bool
		if exists {
			until, locked, err = s.noteSignInFailure(ctx, u.ID)
			if err != nil {
				return SignInResult{}, err
			}
		} else {
			until, locked = s.noteUnknownFailure(lname)
		}
		if locked {
			return SignInResult{}, rateLimitedUntil("Too many failed attempts; try again later", until, now)
		}
		return SignInResult{}, ErrInvalidCredentials
	}
	if dis.Valid {
		return SignInResult{}, ErrUserDisabled
	}
	cred, err := auth.NewToken(auth.PrefixDevice)
	if err != nil {
		return SignInResult{}, internal(err)
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return SignInResult{}, internal(err)
	}
	defer tx.Rollback()
	if other, err := deviceOfOtherUser(ctx, tx, in.DeviceID, u.ID); err != nil {
		return SignInResult{}, err
	} else if other {
		return SignInResult{}, conflict("This device ID already belongs to a device of another user")
	}
	if err := upsertTrustedDevice(ctx, tx, trustedDevice{ID: in.DeviceID, UserID: u.ID, Name: in.DeviceName, Platform: in.Platform,
		Arch: in.Arch, Version: in.AppVersion, CredHash: auth.HashToken(cred), Kind: SignInPassword, InstallID: in.InstallID}, now.Unix()); err != nil {
		return SignInResult{}, err
	}
	if _, err := tx.ExecContext(ctx, `UPDATE users SET sign_in_failures = 0, sign_in_failed_at = NULL, sign_in_locked_until = NULL WHERE id = ?`, u.ID); err != nil {
		return SignInResult{}, internal(err)
	}
	if err := tx.Commit(); err != nil {
		return SignInResult{}, internal(err)
	}
	return SignInResult{HubID: s.Info().HubID, UserID: u.ID, DeviceCredential: cred}, nil
}

// noteSignInFailure counts a failed sign-in of a real user; locked is true when this failure locked the user.
func (s *Service) noteSignInFailure(ctx context.Context, userID string) (until time.Time, locked bool, err error) {
	now := s.now()
	if _, err := s.db.ExecContext(ctx, `UPDATE users SET
		sign_in_failures = CASE WHEN sign_in_failed_at IS NULL OR sign_in_failed_at <= ? THEN 1 ELSE sign_in_failures + 1 END,
		sign_in_failed_at = ? WHERE id = ?`, now.Add(-failureMemory).Unix(), now.Unix(), userID); err != nil {
		return time.Time{}, false, internal(err)
	}
	var n int
	if err := s.db.QueryRowContext(ctx, `SELECT sign_in_failures FROM users WHERE id = ?`, userID).Scan(&n); err != nil {
		return time.Time{}, false, internal(err)
	}
	if n < SignInFailureLimit {
		return time.Time{}, false, nil
	}
	until = now.Add(SignInLockDuration)
	if _, err := s.db.ExecContext(ctx, `UPDATE users SET sign_in_failures = 0, sign_in_locked_until = ? WHERE id = ?`, until.Unix(), userID); err != nil {
		return time.Time{}, false, internal(err)
	}
	return until, true, nil
}

// ---- Account ----

// Account is the account of the signed-in user as seen from one device.
type Account struct {
	UserID             string
	Username           string
	DisplayName        string
	Role               Role
	HasPassword        bool
	PasswordRequired   bool // admin, or the server requires passwords
	MustChangePassword bool
	// ConfirmedUntil is set while this device is confirmed.
	ConfirmedUntil *time.Time
	// ConfirmLockedUntil is set while "Confirm it's you" is locked on this device.
	ConfirmLockedUntil *time.Time
}

// GetAccount returns the account of userID as seen from deviceID (ErrNotFound if the device is not the user's).
func (s *Service) GetAccount(ctx context.Context, userID, deviceID string) (Account, error) {
	var a Account
	var hasPw, must int
	var conf, lock sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT u.id, u.username, u.display_name, u.role, u.password_hash IS NOT NULL, u.must_change_password,
		d.confirmed_until, d.confirm_locked_until FROM users u JOIN devices d ON d.user_id = u.id WHERE u.id = ? AND d.id = ?`, userID, deviceID).
		Scan(&a.UserID, &a.Username, &a.DisplayName, &a.Role, &hasPw, &must, &conf, &lock)
	if errors.Is(err, sql.ErrNoRows) {
		return Account{}, ErrNotFound
	}
	if err != nil {
		return Account{}, internal(err)
	}
	req, err := s.RequirePasswords(ctx)
	if err != nil {
		return Account{}, err
	}
	now := s.now().Unix()
	a.HasPassword, a.MustChangePassword = hasPw != 0, must != 0
	a.PasswordRequired = a.Role == RoleAdmin || req
	if conf.Valid && conf.Int64 > now {
		a.ConfirmedUntil = ts(conf)
	}
	if lock.Valid && lock.Int64 > now {
		a.ConfirmLockedUntil = ts(lock)
	}
	return a, nil
}

// checkDevicePassword verifies the password of userID for "Confirm it's you" style checks on one device. Wrong
// passwords count per device: ConfirmFailureLimit of them lock the checks on that device for ConfirmLockDuration
// (rate_limited with retry_after; the failure that locks already answers rate_limited). A success resets the count.
// ErrPasswordNotSet when the user has no password.
func (s *Service) checkDevicePassword(ctx context.Context, userID, deviceID, password string) error {
	if len(password) > MaxPasswordBytes {
		return ErrInvalidCredentials
	}
	var hash sql.NullString
	var lock sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT u.password_hash, d.confirm_locked_until FROM users u JOIN devices d ON d.user_id = u.id
		WHERE u.id = ? AND d.id = ?`, userID, deviceID).Scan(&hash, &lock)
	if errors.Is(err, sql.ErrNoRows) {
		return ErrNotFound
	}
	if err != nil {
		return internal(err)
	}
	now := s.now()
	if lock.Valid && lock.Int64 > now.Unix() {
		return rateLimitedUntil("Confirming is locked on this device for a while", time.Unix(lock.Int64, 0), now)
	}
	if !hash.Valid {
		return ErrPasswordNotSet
	}
	ok, verr := auth.VerifyPassword(password, hash.String)
	if verr == nil && ok {
		_, err := s.db.ExecContext(ctx, `UPDATE devices SET confirm_failures = 0, confirm_failed_at = NULL, confirm_locked_until = NULL WHERE id = ?`, deviceID)
		return internal2(err)
	}
	if _, err := s.db.ExecContext(ctx, `UPDATE devices SET
		confirm_failures = CASE WHEN confirm_failed_at IS NULL OR confirm_failed_at <= ? THEN 1 ELSE confirm_failures + 1 END,
		confirm_failed_at = ? WHERE id = ?`, now.Add(-failureMemory).Unix(), now.Unix(), deviceID); err != nil {
		return internal(err)
	}
	var n int
	if err := s.db.QueryRowContext(ctx, `SELECT confirm_failures FROM devices WHERE id = ?`, deviceID).Scan(&n); err != nil {
		return internal(err)
	}
	if n >= ConfirmFailureLimit {
		until := now.Add(ConfirmLockDuration)
		if _, err := s.db.ExecContext(ctx, `UPDATE devices SET confirm_failures = 0, confirm_locked_until = ?, confirmed_until = NULL WHERE id = ?`,
			until.Unix(), deviceID); err != nil {
			return internal(err)
		}
		return rateLimitedUntil("Confirming is locked on this device for a while", until, now)
	}
	return ErrInvalidCredentials
}

// SetPassword sets or changes the password of the user. currentPassword is required when the user has one (a wrong
// one counts like a wrong "Confirm it's you" password). The new password needs MinPasswordLen characters. It clears
// must_change_password; the user's devices stay signed in, their web sessions end.
func (s *Service) SetPassword(ctx context.Context, userID, deviceID, currentPassword, newPassword string) (err error) {
	defer s.publishOK(&err, TopicUsers)
	if err := checkNewPassword(newPassword); err != nil {
		return err
	}
	acc, err := s.GetAccount(ctx, userID, deviceID)
	if err != nil {
		return err
	}
	if acc.HasPassword {
		if err := s.checkDevicePassword(ctx, userID, deviceID, currentPassword); err != nil {
			return err
		}
	}
	hash, err := auth.HashPassword(newPassword, s.params)
	if err != nil {
		return internal(err)
	}
	return s.storePassword(ctx, userID, hash, false)
}

// storePassword writes a password hash, clears the sign-in lockout of the user and ends their web sessions.
func (s *Service) storePassword(ctx context.Context, userID, hash string, mustChange bool) error {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	res, err := tx.ExecContext(ctx, `UPDATE users SET password_hash = ?, password_changed_at = ?, must_change_password = ?,
		sign_in_failures = 0, sign_in_failed_at = NULL, sign_in_locked_until = NULL WHERE id = ?`, hash, s.now().Unix(), boolToInt(mustChange), userID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return internal2(tx.Commit())
}

// RemovePassword removes the password of a member. ErrPasswordRequired for admins and while the server requires
// passwords. currentPassword is checked like in SetPassword. Without a password it does nothing.
func (s *Service) RemovePassword(ctx context.Context, userID, deviceID, currentPassword string) (err error) {
	defer s.publishOK(&err, TopicUsers, TopicClients)
	acc, err := s.GetAccount(ctx, userID, deviceID)
	if err != nil {
		return err
	}
	if acc.Role == RoleAdmin {
		return &Error{Code: CodePasswordRequired, Message: "An admin's password cannot be removed"}
	}
	if acc.PasswordRequired {
		return &Error{Code: CodePasswordRequired, Message: "This server requires a password for every account"}
	}
	if !acc.HasPassword {
		return nil
	}
	if err := s.checkDevicePassword(ctx, userID, deviceID, currentPassword); err != nil {
		return err
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, `UPDATE users SET password_hash = NULL, password_changed_at = ?, must_change_password = 0,
		sign_in_failures = 0, sign_in_failed_at = NULL, sign_in_locked_until = NULL WHERE id = ? AND role = 'user'`, s.now().Unix(), userID); err != nil {
		return internal(err)
	}
	// Without a password nothing can be confirmed any more.
	if _, err := tx.ExecContext(ctx, `UPDATE devices SET confirmed_until = NULL WHERE user_id = ?`, userID); err != nil {
		return internal(err)
	}
	return internal2(tx.Commit())
}

// ---- Confirm it's you ----

// Confirm checks the password and confirms the device for durationSeconds (120, 300 or 900; 0 = 300). It returns
// the end of the confirmation. See checkDevicePassword for the lockout; ErrPasswordNotSet without a password.
func (s *Service) Confirm(ctx context.Context, userID, deviceID, password string, durationSeconds int) (time.Time, error) {
	if durationSeconds == 0 {
		durationSeconds = DefaultConfirmSeconds
	}
	valid := false
	for _, d := range ConfirmDurations {
		valid = valid || d == durationSeconds
	}
	if !valid {
		return time.Time{}, badRequest("duration_seconds must be 120, 300 or 900")
	}
	if err := s.checkDevicePassword(ctx, userID, deviceID, password); err != nil {
		return time.Time{}, err
	}
	until := s.now().Add(time.Duration(durationSeconds) * time.Second).Truncate(time.Second).UTC()
	if _, err := s.db.ExecContext(ctx, `UPDATE devices SET confirmed_until = ? WHERE id = ? AND user_id = ?`, until.Unix(), deviceID, userID); err != nil {
		return time.Time{}, internal(err)
	}
	return until, nil
}

// EndConfirmation ends the confirmation of the device early.
func (s *Service) EndConfirmation(ctx context.Context, userID, deviceID string) error {
	_, err := s.db.ExecContext(ctx, `UPDATE devices SET confirmed_until = NULL WHERE id = ? AND user_id = ?`, deviceID, userID)
	return internal2(err)
}

// RequireConfirmed is the "Confirm it's you" check for a dangerous step: nil while the device of the user is
// confirmed, otherwise ErrConfirmationRequired (403 confirmation_required). Call it after the role check and before
// the step.
func (s *Service) RequireConfirmed(ctx context.Context, userID, deviceID string) error {
	var until sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT confirmed_until FROM devices WHERE id = ? AND user_id = ? AND status = 'trusted'`, deviceID, userID).Scan(&until)
	if errors.Is(err, sql.ErrNoRows) {
		return ErrConfirmationRequired
	}
	if err != nil {
		return internal(err)
	}
	if !until.Valid || until.Int64 <= s.now().Unix() {
		return ErrConfirmationRequired
	}
	return nil
}

// ---- Own devices ----

// OwnDevice is a device of the signed-in user.
type OwnDevice struct {
	Device
	// Current is true for the device the request comes from.
	Current bool
}

// ListOwnDevices returns the devices of the user, newest first (signed-out devices included, status revoked).
func (s *Service) ListOwnDevices(ctx context.Context, userID, currentDeviceID string) ([]OwnDevice, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT `+deviceCols+` FROM devices WHERE user_id = ? ORDER BY created_at DESC, name, id`, userID)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	out := []OwnDevice{}
	for rows.Next() {
		d, err := scanDevice(rows)
		if err != nil {
			return nil, internal(err)
		}
		out = append(out, OwnDevice{Device: d, Current: d.ID == currentDeviceID})
	}
	return out, internal2(rows.Err())
}

// SignOutDevice signs out one device of the user (revokes it: access tokens die, live connections close). A device of
// another user is ErrNotFound.
func (s *Service) SignOutDevice(ctx context.Context, userID, deviceID string) error {
	d, err := s.GetDevice(ctx, deviceID)
	if err != nil {
		return err
	}
	if d.UserID != userID {
		return ErrNotFound
	}
	return s.RevokeDevice(ctx, deviceID)
}

// SignOutAllDevices signs out all trusted devices of the user, the current one only when includeCurrent. It returns
// how many devices were signed out.
func (s *Service) SignOutAllDevices(ctx context.Context, userID, currentDeviceID string, includeCurrent bool) (int, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT id FROM devices WHERE user_id = ? AND status = 'trusted'`, userID)
	if err != nil {
		return 0, internal(err)
	}
	var ids []string
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			rows.Close()
			return 0, internal(err)
		}
		if id != currentDeviceID || includeCurrent {
			ids = append(ids, id)
		}
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return 0, internal(err)
	}
	n := 0
	for _, id := range ids {
		if err := s.RevokeDevice(ctx, id); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}

// ---- Admin password reset ----

// tempPasswordGroups x tempPasswordGroupLen symbols of inviteAlphabet (no 0, 1, O, I, L): about 79 bits.
const (
	tempPasswordGroups   = 4
	tempPasswordGroupLen = 4
)

func newTempPassword() (string, error) {
	var b strings.Builder
	for g := 0; g < tempPasswordGroups; g++ {
		if g > 0 {
			b.WriteByte('-')
		}
		for i := 0; i < tempPasswordGroupLen; i++ {
			for {
				var one [1]byte
				if _, err := rand.Read(one[:]); err != nil {
					return "", err
				}
				if one[0] < 248 { // 256 mod 31 = 8: reject the top values so every symbol is equally likely
					b.WriteByte(inviteAlphabet[int(one[0])%len(inviteAlphabet)])
					break
				}
			}
		}
	}
	return b.String(), nil
}

// ResetPassword sets a new one-time temporary password for the user (admin step: the actor's device must be
// confirmed; not for the actor themself). The password is returned only here. The user must change it
// (must_change_password); their devices stay signed in and a sign-in lockout ends.
func (s *Service) ResetPassword(ctx context.Context, actorID, actorDeviceID, userID string) (_ string, err error) {
	defer s.publishOK(&err, TopicUsers)
	if userID == actorID {
		return "", &Error{Code: CodeForbidden, Message: "You cannot reset your own password here; change it in your account"}
	}
	if err := s.RequireConfirmed(ctx, actorID, actorDeviceID); err != nil {
		return "", err
	}
	return s.setTempPassword(ctx, userID)
}

// setTempPassword gives the user a new one-time temporary password (they must change it), ends their sign-in
// lockout and keeps their devices signed in. The password is returned only here.
func (s *Service) setTempPassword(ctx context.Context, userID string) (string, error) {
	if _, err := s.GetUser(ctx, userID); err != nil {
		return "", err
	}
	temp, err := newTempPassword()
	if err != nil {
		return "", internal(err)
	}
	hash, err := auth.HashPassword(temp, s.params)
	if err != nil {
		return "", internal(err)
	}
	if err := s.storePassword(ctx, userID, hash, true); err != nil {
		return "", err
	}
	return temp, nil
}

// ListAdmins returns the admins, oldest first.
func (s *Service) ListAdmins(ctx context.Context) ([]User, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT `+userCols+` FROM users WHERE role = 'admin' ORDER BY created_at, username`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	var out []User
	for rows.Next() {
		u, err := scanUser(rows)
		if err != nil {
			return nil, internal(err)
		}
		out = append(out, u)
	}
	return out, rows.Err()
}

// ResetAdminPasswordOnServer is the emergency reset of `uzume-server reset-admin`: whoever can run it on the server
// owns the data directory, so no "Confirm it's you" is asked. It changes the password of one admin only (users,
// games, saves and devices stay untouched) and returns the temporary password.
func (s *Service) ResetAdminPasswordOnServer(ctx context.Context, adminID string) (_ string, err error) {
	defer s.publishOK(&err, TopicUsers)
	u, err := s.GetUser(ctx, adminID)
	if err != nil {
		return "", err
	}
	if u.Role != RoleAdmin {
		return "", ErrForbidden
	}
	return s.setTempPassword(ctx, adminID)
}
