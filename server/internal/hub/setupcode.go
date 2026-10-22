package hub

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"database/sql"
	"encoding/json"
	"errors"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/tlsutil"
)

// Setup code (0.18 package 6, ADR 0030 D4, handshake feature `setup_code_v1`): a server without a screen is set up
// from the app. While no admin exists the server holds one code of six Crockford base32 characters (shown as
// `7H4K-2Q`), valid for SetupCodeValidity and usable once. `POST /api/v1/setup` trades the code for the first admin
// and a paired device in one step. The code is stored in the settings table (the database already gives full control
// over the server) and is never logged except by the deliberate start banner of the server.

// FeatureSetupCodeV1 marks a server that offers `POST /api/v1/setup` and `setup_required` in the server info.
const FeatureSetupCodeV1 = "setup_code_v1"

// Setup code limits.
const (
	// SetupCodeValidity is how long a setup code works.
	SetupCodeValidity = 60 * time.Minute
	// SetupCodeMaxAttempts wrong codes in total invalidate the current code.
	SetupCodeMaxAttempts = 10
	// SetupCodeLen is the number of symbols of a setup code.
	SetupCodeLen = 6
)

// Setup code error codes (stable, part of the API).
const (
	// CodeInvalidSetupCode: the code is wrong, used, replaced or invalidated by too many wrong tries (401).
	CodeInvalidSetupCode Code = "invalid_setup_code"
	// CodeSetupCodeExpired: the code is right but older than SetupCodeValidity (410).
	CodeSetupCodeExpired Code = "setup_code_expired"
	// CodeAlreadySetUp: an admin already exists (409).
	CodeAlreadySetUp Code = "already_set_up"
)

var (
	ErrInvalidSetupCode = &Error{Code: CodeInvalidSetupCode, Message: "That setup code does not match"}
	ErrSetupCodeExpired = &Error{Code: CodeSetupCodeExpired, Message: "This setup code has expired; run 'uzume-server setup-code' on the server for a new one"}
	ErrAlreadySetUp     = &Error{Code: CodeAlreadySetUp, Message: "This server is already set up"}
)

const settingSetupCode = "setup_code"

// SetupCode is a setup code with its expiry.
type SetupCode struct {
	// Code is the code as shown: `XXXX-XX`.
	Code    string
	Expires time.Time
}

// storedSetupCode is the JSON value of the setting.
type storedSetupCode struct {
	Code     string `json:"code"` // normalized: six symbols
	Expires  int64  `json:"expires"`
	Attempts int    `json:"attempts"`
}

func (c storedSetupCode) public() SetupCode {
	return SetupCode{Code: FormatSetupCode(c.Code), Expires: time.Unix(c.Expires, 0).UTC()}
}

// FormatSetupCode shows a normalized code as `XXXX-XX`.
func FormatSetupCode(c string) string {
	if len(c) <= 4 {
		return c
	}
	return c[:4] + "-" + c[4:]
}

// randomSetupCode returns six Crockford symbols from crypto/rand (256 is a multiple of 32: no bias).
func randomSetupCode() (string, error) {
	var b [SetupCodeLen]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	out := make([]byte, SetupCodeLen)
	for i, v := range b {
		out[i] = tlsutil.Crockford[v&31]
	}
	return string(out), nil
}

// readSetupCode reads the stored code through q (ok=false when none).
func readSetupCode(ctx context.Context, q interface {
	QueryRowContext(context.Context, string, ...any) *sql.Row
}) (c storedSetupCode, ok bool, err error) {
	var raw string
	err = q.QueryRowContext(ctx, `SELECT value FROM settings WHERE key = ?`, settingSetupCode).Scan(&raw)
	if errors.Is(err, sql.ErrNoRows) {
		return c, false, nil
	}
	if err != nil {
		return c, false, internal(err)
	}
	if json.Unmarshal([]byte(raw), &c) != nil || len(c.Code) != SetupCodeLen {
		return storedSetupCode{}, false, nil
	}
	return c, true, nil
}

func writeSetupCode(ctx context.Context, ex interface {
	ExecContext(context.Context, string, ...any) (sql.Result, error)
}, c storedSetupCode) error {
	b, _ := json.Marshal(c)
	_, err := ex.ExecContext(ctx, `INSERT INTO settings(key, value) VALUES (?,?) ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
		settingSetupCode, string(b))
	return internal2(err)
}

// SetupRequired reports whether the server still needs its first admin (no admin exists).
func (s *Service) SetupRequired(ctx context.Context) (bool, error) {
	has, err := s.HasAdmin(ctx)
	return !has, err
}

// NewSetupCode creates a fresh setup code and replaces any earlier one. ErrAlreadySetUp when an admin exists.
func (s *Service) NewSetupCode(ctx context.Context) (SetupCode, error) {
	code, err := randomSetupCode()
	if err != nil {
		return SetupCode{}, internal(err)
	}
	s.setupMu.Lock()
	defer s.setupMu.Unlock()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return SetupCode{}, internal(err)
	}
	defer tx.Rollback()
	var n int
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE role = 'admin'`).Scan(&n); err != nil {
		return SetupCode{}, internal(err)
	}
	if n > 0 {
		return SetupCode{}, ErrAlreadySetUp
	}
	c := storedSetupCode{Code: code, Expires: s.now().Add(SetupCodeValidity).Unix()}
	if err := writeSetupCode(ctx, tx, c); err != nil {
		return SetupCode{}, err
	}
	if err := tx.Commit(); err != nil {
		return SetupCode{}, internal(err)
	}
	return c.public(), nil
}

// CurrentSetupCode returns the code that can still be used (nil when there is none, it expired, too many wrong tries
// invalidated it, or an admin exists).
func (s *Service) CurrentSetupCode(ctx context.Context) (*SetupCode, error) {
	has, err := s.HasAdmin(ctx)
	if err != nil || has {
		return nil, err
	}
	c, ok, err := readSetupCode(ctx, s.db)
	if err != nil || !ok || c.Attempts >= SetupCodeMaxAttempts || !s.now().Before(time.Unix(c.Expires, 0)) {
		return nil, err
	}
	p := c.public()
	return &p, nil
}

// EnsureSetupCode returns the usable code, creating a new one when there is none.
func (s *Service) EnsureSetupCode(ctx context.Context) (SetupCode, error) {
	c, err := s.CurrentSetupCode(ctx)
	if err != nil {
		return SetupCode{}, err
	}
	if c != nil {
		return *c, nil
	}
	return s.NewSetupCode(ctx)
}

// SetupCodeInput is the body of `POST /api/v1/setup`.
type SetupCodeInput struct {
	Code               string
	Username, Password string
	Device             PairingInput
}

// SetupWithCode creates the first admin and pairs the calling app as that admin when the setup code is right.
//
// Order of answers: field problems (400) and the per-IP limit (429) first; ErrAlreadySetUp when an admin exists;
// ErrInvalidSetupCode for a wrong code (it counts; SetupCodeMaxAttempts wrong tries in total delete the code);
// ErrSetupCodeExpired for the right code after SetupCodeValidity (the code is deleted). A right code is consumed
// together with the admin in one transaction, so of two parallel requests exactly one wins.
func (s *Service) SetupWithCode(ctx context.Context, in SetupCodeInput) (PairingResult, User, error) {
	if err := s.validLocalDevice(in.Device); err != nil {
		return PairingResult{}, User{}, err
	}
	in.Username = strings.TrimSpace(in.Username)
	if err := checkAdminFields(in.Username, in.Password); err != nil { // 400 before the code is looked at: a typo never burns it
		return PairingResult{}, User{}, err
	}
	if err := s.rateLimitSignIn(in.Device.RemoteAddr); err != nil {
		return PairingResult{}, User{}, err
	}
	typed := tlsutil.NormalizeCode(in.Code)

	s.setupMu.Lock()
	defer s.setupMu.Unlock()
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return PairingResult{}, User{}, internal(err)
	}
	defer tx.Rollback()
	var admins int
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE role = 'admin'`).Scan(&admins); err != nil {
		return PairingResult{}, User{}, internal(err)
	}
	if admins > 0 {
		return PairingResult{}, User{}, ErrAlreadySetUp
	}
	stored, ok, err := readSetupCode(ctx, tx)
	if err != nil {
		return PairingResult{}, User{}, err
	}
	if !ok || stored.Attempts >= SetupCodeMaxAttempts || len(typed) != SetupCodeLen ||
		subtle.ConstantTimeCompare([]byte(typed), []byte(stored.Code)) != 1 {
		if ok {
			// A wrong try counts; the tenth wrong one deletes the code. Committed although the request fails.
			stored.Attempts++
			if stored.Attempts >= SetupCodeMaxAttempts {
				_, err = tx.ExecContext(ctx, `DELETE FROM settings WHERE key = ?`, settingSetupCode)
			} else {
				err = writeSetupCode(ctx, tx, stored)
			}
			if err != nil {
				return PairingResult{}, User{}, internal(err)
			}
			if err := tx.Commit(); err != nil {
				return PairingResult{}, User{}, internal(err)
			}
		}
		return PairingResult{}, User{}, ErrInvalidSetupCode
	}
	if !s.now().Before(time.Unix(stored.Expires, 0)) {
		if _, err := tx.ExecContext(ctx, `DELETE FROM settings WHERE key = ?`, settingSetupCode); err != nil {
			return PairingResult{}, User{}, internal(err)
		}
		if err := tx.Commit(); err != nil {
			return PairingResult{}, User{}, internal(err)
		}
		return PairingResult{}, User{}, ErrSetupCodeExpired
	}
	// Only a right code reaches the (slow) hash: wrong codes cost an attacker no Argon2 work on the server.
	hash, err := s.hashPassword(in.Password)
	if err != nil {
		return PairingResult{}, User{}, err
	}
	u, err := s.createAdminTx(ctx, tx, in.Username, hash) // also deletes the code
	if err != nil {
		return PairingResult{}, User{}, err
	}
	if err := tx.Commit(); err != nil {
		return PairingResult{}, User{}, internal(err)
	}
	res, err := s.pairAs(ctx, u.ID, in.Device, SignInPassword)
	if err != nil {
		// Admin and pairing succeed or fail together: the code works again for the next try.
		s.discardLocalAdmin(u.ID, in.Device.DeviceID)
		stored.Attempts = 0
		_ = writeSetupCode(ctx, s.db, stored)
		return PairingResult{}, User{}, err
	}
	return res, u, nil
}
