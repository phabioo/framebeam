package hub

import (
	"context"
	"database/sql"
	"errors"
)

// Hub settings live in the settings table (key/value).
const (
	settingAllowUserUploads = "allow_user_uploads"
	settingAppearance       = "appearance"
)

// Appearance values of the web interface.
const (
	AppearanceLight  = "light"
	AppearanceDark   = "dark"
	AppearanceSystem = "system"
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

// Appearance returns the web interface mode (light, dark, system; default light).
func (s *Service) Appearance(ctx context.Context) (string, error) {
	v, ok, err := s.getSetting(ctx, settingAppearance)
	if err != nil || !ok {
		return AppearanceLight, err
	}
	switch v {
	case AppearanceDark, AppearanceSystem:
		return v, nil
	}
	return AppearanceLight, nil
}

// SetAppearance stores the web interface mode.
func (s *Service) SetAppearance(ctx context.Context, mode string) error {
	switch mode {
	case AppearanceLight, AppearanceDark, AppearanceSystem:
		return s.setSetting(ctx, settingAppearance, mode)
	}
	return badRequest("Appearance must be light, dark or system")
}
