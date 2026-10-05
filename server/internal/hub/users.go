package hub

import (
	"context"
	"database/sql"
	"errors"
	"regexp"
	"strings"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// Role ist die Rolle eines Users.
type Role string

const (
	RoleAdmin Role = "admin"
	RoleUser  Role = "user"
)

// User ist ein Hub-Benutzer. Normale User haben kein Passwort.
type User struct {
	ID          string
	Username    string
	DisplayName string
	Role        Role
	CreatedAt   time.Time
}

var usernameRe = regexp.MustCompile(`^[A-Za-z0-9._-]{1,64}$`)

func newUserID() string { return "u_" + strings.ReplaceAll(uuid.NewString(), "-", "")[:16] }

const userCols = `id, username, display_name, role, created_at`

type scanner interface{ Scan(...any) error }

func scanUser(r scanner) (User, error) {
	var u User
	var created int64
	if err := r.Scan(&u.ID, &u.Username, &u.DisplayName, &u.Role, &created); err != nil {
		return User{}, err
	}
	u.CreatedAt = time.Unix(created, 0).UTC()
	return u, nil
}

// HasAdmin meldet, ob bereits ein Admin existiert.
func (s *Service) HasAdmin(ctx context.Context) (bool, error) {
	var n int
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM users WHERE role = 'admin'`).Scan(&n); err != nil {
		return false, internal(err)
	}
	return n > 0, nil
}

// CreateAdmin legt den ersten Admin an; existiert schon einer, schlägt es mit ErrAdminExists fehl.
func (s *Service) CreateAdmin(ctx context.Context, username, password string) (User, error) {
	if !usernameRe.MatchString(username) {
		return User{}, badRequest("Benutzername: 1 bis 64 Zeichen aus A-Z, a-z, 0-9, '.', '_', '-'")
	}
	if len(password) < MinPasswordLen {
		return User{}, badRequest("Passwort muss mindestens %d Zeichen lang sein", MinPasswordLen)
	}
	hash, err := auth.HashPassword(password, s.params)
	if err != nil {
		return User{}, internal(err)
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return User{}, internal(err)
	}
	defer tx.Rollback()
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
			return User{}, conflict("Benutzername bereits vergeben")
		}
		return User{}, internal(err)
	}
	if err := tx.Commit(); err != nil {
		return User{}, internal(err)
	}
	return u, nil
}

// CreateUser legt einen normalen User ohne Passwort an (nur durch den Admin).
func (s *Service) CreateUser(ctx context.Context, username, displayName string) (User, error) {
	if !usernameRe.MatchString(username) {
		return User{}, badRequest("Benutzername: 1 bis 64 Zeichen aus A-Z, a-z, 0-9, '.', '_', '-'")
	}
	displayName = strings.TrimSpace(displayName)
	if displayName == "" {
		displayName = username
	}
	if len(displayName) > 100 {
		return User{}, badRequest("Anzeigename zu lang")
	}
	u := User{ID: newUserID(), Username: username, DisplayName: displayName, Role: RoleUser, CreatedAt: s.Now().Truncate(time.Second)}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO users(id, username, display_name, role, password_hash, created_at) VALUES (?,?,?,?,NULL,?)`,
		u.ID, u.Username, u.DisplayName, u.Role, u.CreatedAt.Unix()); err != nil {
		if isUnique(err) {
			return User{}, conflict("Benutzername bereits vergeben")
		}
		return User{}, internal(err)
	}
	return u, nil
}

// GetUser liefert einen User per ID.
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

// ListUsers liefert alle User nach Anlegedatum.
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

// VerifyPassword prüft Benutzername und Passwort (nur Admins haben ein Passwort).
// Fehler: ErrInvalidCredentials. Bei unbekanntem User läuft ein Dummy-Hash für gleiche Laufzeit.
func (s *Service) VerifyPassword(ctx context.Context, username, password string) (User, error) {
	var hash sql.NullString
	var u User
	var created int64
	err := s.db.QueryRowContext(ctx, `SELECT `+userCols+`, password_hash FROM users WHERE username = ?`, username).
		Scan(&u.ID, &u.Username, &u.DisplayName, &u.Role, &created, &hash)
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

// ChangePassword setzt das Passwort eines Users mit Passwort (Admin). Das alte Passwort prüft der
// Aufrufer (VerifyPassword). Bestehende Web-Sessions des Users werden beendet.
func (s *Service) ChangePassword(ctx context.Context, userID, newPassword string) error {
	if len(newPassword) < MinPasswordLen {
		return badRequest("Passwort muss mindestens %d Zeichen lang sein", MinPasswordLen)
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
	res, err := tx.ExecContext(ctx, `UPDATE users SET password_hash = ? WHERE id = ? AND password_hash IS NOT NULL`, hash, userID)
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

// WebSession ist eine angemeldete Web-Sitzung (Webinterface).
type WebSession struct {
	User      User
	CSRFToken string
	ExpiresAt time.Time
}

// CreateWebSession legt eine Web-Sitzung an und liefert das Session-Token (nur Klartext hier; gespeichert
// wird der Hash) samt Sitzung mit CSRF-Token.
func (s *Service) CreateWebSession(ctx context.Context, userID string) (token string, ws WebSession, err error) {
	u, err := s.GetUser(ctx, userID)
	if err != nil {
		return "", WebSession{}, err
	}
	token, err = auth.NewToken("fbw_")
	if err != nil {
		return "", WebSession{}, internal(err)
	}
	csrf, err := auth.NewToken("")
	if err != nil {
		return "", WebSession{}, internal(err)
	}
	now := s.Now()
	exp := now.Add(WebSessionTTL)
	if _, err := s.db.ExecContext(ctx, `INSERT INTO web_sessions(token_hash, user_id, csrf_token, created_at, expires_at) VALUES (?,?,?,?,?)`,
		auth.HashToken(token), u.ID, csrf, now.Unix(), exp.Unix()); err != nil {
		return "", WebSession{}, internal(err)
	}
	return token, WebSession{User: u, CSRFToken: csrf, ExpiresAt: exp.Truncate(time.Second)}, nil
}

// LookupWebSession liefert die Sitzung zum Token (ErrUnauthorized, wenn unbekannt oder abgelaufen).
func (s *Service) LookupWebSession(ctx context.Context, token string) (WebSession, error) {
	var ws WebSession
	var created, exp int64
	err := s.db.QueryRowContext(ctx, `SELECT u.id, u.username, u.display_name, u.role, u.created_at, w.csrf_token, w.expires_at
		FROM web_sessions w JOIN users u ON u.id = w.user_id WHERE w.token_hash = ?`, auth.HashToken(token)).
		Scan(&ws.User.ID, &ws.User.Username, &ws.User.DisplayName, &ws.User.Role, &created, &ws.CSRFToken, &exp)
	if errors.Is(err, sql.ErrNoRows) {
		return WebSession{}, ErrUnauthorized
	}
	if err != nil {
		return WebSession{}, internal(err)
	}
	if exp <= s.now().Unix() {
		return WebSession{}, ErrUnauthorized
	}
	ws.User.CreatedAt = time.Unix(created, 0).UTC()
	ws.ExpiresAt = time.Unix(exp, 0).UTC()
	return ws, nil
}

// DeleteWebSession beendet eine Web-Sitzung (Logout).
func (s *Service) DeleteWebSession(ctx context.Context, token string) error {
	_, err := s.db.ExecContext(ctx, `DELETE FROM web_sessions WHERE token_hash = ?`, auth.HashToken(token))
	return internal2(err)
}
