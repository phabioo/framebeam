package hub

import (
	"context"
	"strings"
)

// Network settings saved on the web interface (Settings > Network). Each value is its own row in the settings
// table, "net.<key>", so that "Reset to hub.env value" deletes exactly one override. The keys and the meaning of
// the raw string values belong to internal/config (config.Net*); the service only stores them. The effective
// configuration is "hub.env/flags, then these overrides" and is assembled by cmd/framebeam-hub at startup.

const netSettingPrefix = "net."

// NetOverrides returns all stored network overrides (key without prefix -> raw value).
func (s *Service) NetOverrides(ctx context.Context) (map[string]string, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT key, value FROM settings WHERE key LIKE 'net.%'`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	out := map[string]string{}
	for rows.Next() {
		var k, v string
		if err := rows.Scan(&k, &v); err != nil {
			return nil, internal(err)
		}
		out[strings.TrimPrefix(k, netSettingPrefix)] = v
	}
	return out, internal2(rows.Err())
}

func validNetKey(k string) bool {
	if k == "" || len(k) > 40 {
		return false
	}
	for _, r := range k {
		if (r < 'a' || r > 'z') && (r < '0' || r > '9') && r != '_' {
			return false
		}
	}
	return true
}

// SetNetOverride stores the raw value of a network setting (the caller validated it).
func (s *Service) SetNetOverride(ctx context.Context, key, raw string) error {
	if !validNetKey(key) {
		return badRequest("Unknown network setting")
	}
	return s.setSetting(ctx, netSettingPrefix+key, raw)
}

// ResetNetOverride deletes a stored network setting: the value from hub.env/flags applies again.
func (s *Service) ResetNetOverride(ctx context.Context, key string) error {
	if !validNetKey(key) {
		return badRequest("Unknown network setting")
	}
	_, err := s.db.ExecContext(ctx, `DELETE FROM settings WHERE key = ?`, netSettingPrefix+key)
	return internal2(err)
}

// SetSaveRetention replaces the save history retention rules (ADR 0012 D7) without a restart; the next sweep
// and the next upload use them. 0 = unlimited for that rule (recent 0 = no thinning at all).
func (s *Service) SetSaveRetention(recent, daily, weekly int) {
	s.saveMu.Lock()
	s.saveKeep = saveRetention{recent: recent, daily: daily, weekly: weekly}
	s.saveMu.Unlock()
}

// SaveRetention returns the retention rules in effect (newest versions, days, weeks).
func (s *Service) SaveRetention() (recent, daily, weekly int) {
	s.saveMu.Lock()
	defer s.saveMu.Unlock()
	return s.saveKeep.recent, s.saveKeep.daily, s.saveKeep.weekly
}

// SetICEServers replaces the external stun: URLs handed to Players without a restart. Sessions already running
// keep what they were given.
func (s *Service) SetICEServers(urls []string) {
	s.sess.iceMu.Lock()
	s.sess.ice = append([]string{}, urls...)
	s.sess.iceMu.Unlock()
}
