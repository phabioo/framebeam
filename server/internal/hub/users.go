package hub

import (
	"context"
	"database/sql"
	"errors"
	"regexp"
	"strings"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/auth"
)

// Role is the role of a user.
type Role string

const (
	RoleAdmin Role = "admin"
	RoleUser  Role = "user"
)

// User is a hub user. Regular users have no password.
type User struct {
	ID          string
	Username    string
	DisplayName string
	Role        Role
	CreatedAt   time.Time
	// DisabledAt is set while an admin has disabled the user (never for admins).
	DisabledAt *time.Time
}

// Disabled reports whether the user is disabled.
func (u User) Disabled() bool { return u.DisabledAt != nil }

var usernameRe = regexp.MustCompile(`^[A-Za-z0-9._-]{1,64}$`)

func newUserID() string { return "u_" + strings.ReplaceAll(uuid.NewString(), "-", "")[:16] }

const userCols = `id, username, display_name, role, created_at, disabled_at`

type scanner interface{ Scan(...any) error }

func scanUser(r scanner) (User, error) {
	var u User
	var created int64
	var dis sql.NullInt64
	if err := r.Scan(&u.ID, &u.Username, &u.DisplayName, &u.Role, &created, &dis); err != nil {
		return User{}, err
	}
	u.CreatedAt = time.Unix(created, 0).UTC()
	u.DisabledAt = ts(dis)
	return u, nil
}

// HasAdmin reports whether an admin already exists.
func (s *Service) HasAdmin(ctx context.Context) (bool, error) {
	var n int
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE role = 'admin'`).Scan(&n); err != nil {
		return false, internal(err)
	}
	return n > 0, nil
}

// CreateAdmin creates the first admin; if one already exists, it fails with ErrAdminExists. A pending setup code is
// removed with it.
func (s *Service) CreateAdmin(ctx context.Context, username, password string) (User, error) {
	if err := checkAdminFields(username, password); err != nil {
		return User{}, err
	}
	hash, err := s.hashPassword(password)
	if err != nil {
		return User{}, err
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return User{}, internal(err)
	}
	defer tx.Rollback()
	u, err := s.createAdminTx(ctx, tx, username, hash)
	if err != nil {
		return User{}, err
	}
	if err := tx.Commit(); err != nil {
		return User{}, internal(err)
	}
	return u, nil
}

// checkAdminFields applies the admin rules for user name and password (cheap, no hashing).
func checkAdminFields(username, password string) error {
	if !usernameRe.MatchString(username) {
		return badRequest("Username: 1 to 64 characters from A-Z, a-z, 0-9, '.', '_', '-'")
	}
	if len(password) < MinPasswordLen {
		return badRequest("Password must be at least %d characters long", MinPasswordLen)
	}
	return nil
}

// hashPassword hashes a password with the server's Argon2id parameters. Call it before a transaction when possible:
// the database has one connection and Argon2id is slow.
func (s *Service) hashPassword(password string) (string, error) {
	hash, err := auth.HashPassword(password, s.params)
	if err != nil {
		return "", internal(err)
	}
	return hash, nil
}

// createAdminTx inserts the first admin inside tx (ErrAdminExists when one exists) and drops the setup code.
func (s *Service) createAdminTx(ctx context.Context, tx *sql.Tx, username, hash string) (User, error) {
	var n int
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE role = 'admin'`).Scan(&n); err != nil {
		return User{}, internal(err)
	}
	if n > 0 {
		return User{}, ErrAdminExists
	}
	u := User{ID: newUserID(), Username: username, DisplayName: username, Role: RoleAdmin, CreatedAt: s.Now().Truncate(time.Second)}
	if _, err := tx.ExecContext(ctx, `INSERT INTO users(id, username, display_name, role, password_hash, created_at) VALUES (?,?,?,?,?,?)`,
		u.ID, u.Username, u.DisplayName, u.Role, hash, u.CreatedAt.Unix()); err != nil {
		if isUnique(err) {
			return User{}, conflict("Username or display name already taken")
		}
		return User{}, internal(err)
	}
	if _, err := tx.ExecContext(ctx, `DELETE FROM settings WHERE key = ?`, settingSetupCode); err != nil {
		return User{}, internal(err)
	}
	return u, nil
}

// CreateUser creates a regular user without a password (by the admin only).
func (s *Service) CreateUser(ctx context.Context, username, displayName string) (_ User, err error) {
	defer s.publishOK(&err, TopicUsers)
	if !usernameRe.MatchString(username) {
		return User{}, badRequest("Username: 1 to 64 characters from A-Z, a-z, 0-9, '.', '_', '-'")
	}
	displayName = strings.TrimSpace(displayName)
	if displayName == "" {
		displayName = username
	}
	if len(displayName) > 100 {
		return User{}, badRequest("Display name too long")
	}
	u := User{ID: newUserID(), Username: username, DisplayName: displayName, Role: RoleUser, CreatedAt: s.Now().Truncate(time.Second)}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO users(id, username, display_name, role, password_hash, created_at) VALUES (?,?,?,?,NULL,?)`,
		u.ID, u.Username, u.DisplayName, u.Role, u.CreatedAt.Unix()); err != nil {
		if isUnique(err) {
			return User{}, conflict("Username or display name already taken")
		}
		return User{}, internal(err)
	}
	return u, nil
}

// GetUser returns a user by ID.
func (s *Service) GetUser(ctx context.Context, id string) (User, error) {
	u, err := scanUser(s.db.QueryRowContext(ctx, `SELECT `+userCols+` FROM users WHERE id = ?`, id))
	if errors.Is(err, sql.ErrNoRows) {
		return User{}, ErrNotFound
	}
	if err != nil {
		return User{}, internal(err)
	}
	return u, nil
}

// ListUsers returns all users by creation date.
func (s *Service) ListUsers(ctx context.Context) ([]User, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT `+userCols+` FROM users ORDER BY created_at, username`)
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

func (s *Service) dummyHash() string {
	s.dummyO.Do(func() { s.dummy, _ = auth.HashPassword("dummy-password", s.params) })
	return s.dummy
}

// VerifyPassword checks username and password (only admins have a password).
// Error: ErrInvalidCredentials. For an unknown user a dummy hash runs to keep the run time the same.
func (s *Service) VerifyPassword(ctx context.Context, username, password string) (User, error) {
	var hash sql.NullString
	var u User
	var created int64
	var dis sql.NullInt64
	err := s.db.QueryRowContext(ctx, `SELECT `+userCols+`, password_hash FROM users WHERE username = ?`, username).
		Scan(&u.ID, &u.Username, &u.DisplayName, &u.Role, &created, &dis, &hash)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return User{}, internal(err)
	}
	enc := s.dummyHash()
	known := err == nil && hash.Valid
	if known {
		enc = hash.String
	}
	ok, verr := auth.VerifyPassword(password, enc)
	if !known || verr != nil || !ok {
		return User{}, ErrInvalidCredentials
	}
	u.CreatedAt = time.Unix(created, 0).UTC()
	return u, nil
}

// ChangePassword sets the password of a user who has one (admin). The caller checks the old password
// (VerifyPassword). The user's existing web sessions are ended.
func (s *Service) ChangePassword(ctx context.Context, userID, newPassword string) error {
	if len(newPassword) < MinPasswordLen {
		return badRequest("Password must be at least %d characters long", MinPasswordLen)
	}
	hash, err := auth.HashPassword(newPassword, s.params)
	if err != nil {
		return internal(err)
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	res, err := tx.ExecContext(ctx, `UPDATE users SET password_hash = ?, password_changed_at = ?, must_change_password = 0
		WHERE id = ? AND password_hash IS NOT NULL`, hash, s.now().Unix(), userID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	if _, err := tx.ExecContext(ctx, `DELETE FROM web_sessions WHERE user_id = ?`, userID); err != nil {
		return internal(err)
	}
	return internal2(tx.Commit())
}

func internal2(err error) error {
	if err == nil {
		return nil
	}
	return internal(err)
}
