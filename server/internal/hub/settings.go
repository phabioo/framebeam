package hub

import (
	"context"
	"database/sql"
	"errors"
)

// Server settings live in the settings table (key/value).
const (
	settingAllowUserUploads = "allow_user_uploads"
)

func (s *Service) getSetting(ctx context.Context, key string) (string, bool, error) {
	var v string
	err := s.db.QueryRowContext(ctx, `SELECT value FROM settings WHERE key = ?`, key).Scan(&v)
	if errors.Is(err, sql.ErrNoRows) {
		return "", false, nil
	}
	if err != nil {
		return "", false, internal(err)
	}
	return v, true, nil
}

func (s *Service) setSetting(ctx context.Context, key, value string) error {
	_, err := s.db.ExecContext(ctx, `INSERT INTO settings(key, value) VALUES (?,?)
		ON CONFLICT(key) DO UPDATE SET value = excluded.value`, key, value)
	return internal2(err)
}

// AllowUserUploads reports whether regular users may upload ROMs (default off).
func (s *Service) AllowUserUploads(ctx context.Context) (bool, error) {
	v, _, err := s.getSetting(ctx, settingAllowUserUploads)
	return v == "1", err
}

// SetAllowUserUploads switches the upload permission for regular users.
func (s *Service) SetAllowUserUploads(ctx context.Context, on bool) error {
	v := "0"
	if on {
		v = "1"
	}
	return s.setSetting(ctx, settingAllowUserUploads, v)
}

// CanUpload reports whether the user may upload ROMs: admins always, users only if the setting is on.
func (s *Service) CanUpload(ctx context.Context, u User) (bool, error) {
	if u.Role == RoleAdmin {
		return true, nil
	}
	return s.AllowUserUploads(ctx)
}
