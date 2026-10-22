package hub

import (
	"context"
	"crypto/subtle"
	"sort"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/auth"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// Data for the admin Status page and the metrics endpoint (read-only; nothing here changes Sessions).

// SessionSummary is an active Session as the Status page lists it.
type SessionSummary struct {
	GameTitle  string
	Owner      string
	DeviceName string
	Visibility Visibility
	Since      time.Time
	Viewers    int
}

// ActiveSessionSummaries lists the running Sessions, newest first.
func (s *Service) ActiveSessionSummaries(ctx context.Context) ([]SessionSummary, error) {
	s.sess.mu.Lock()
	defer s.sess.mu.Unlock()
	ids, err := s.activeSessionIDs(ctx)
	if err != nil {
		return nil, err
	}
	out := make([]SessionSummary, 0, len(ids))
	for _, id := range ids {
		d, err := s.loadSession(ctx, id)
		if err != nil {
			return nil, err
		}
		out = append(out, SessionSummary{GameTitle: d.gameTitle, Owner: d.ownerName, DeviceName: d.ownerDeviceName,
			Visibility: d.visibility, Since: d.createdAt, Viewers: len(d.viewers)})
	}
	return out, nil
}

// Player states shown on the Status page.
const (
	PlayerInSession = "in_session" // owns a running Session
	PlayerWatching  = "watching"   // views a running Session
	PlayerPlaying   = "playing"    // plays a game on its own
	PlayerIdle      = "idle"
)

// PlayerStatus is a Player with an open WSS connection.
type PlayerStatus struct {
	DeviceName string
	User       string
	Version    string
	State      string
	// Outdated: the Player's version is below the Hub's (both are release versions).
	Outdated bool
}

// ConnectedPlayers lists the connected Players, sorted by device name.
func (s *Service) ConnectedPlayers(ctx context.Context) ([]PlayerStatus, error) {
	conns := s.clients()
	if len(conns) == 0 {
		return nil, nil
	}
	devs, err := s.ListDevices(ctx)
	if err != nil {
		return nil, err
	}
	users, err := s.ListUsers(ctx)
	if err != nil {
		return nil, err
	}
	userName := map[string]string{}
	for _, u := range users {
		userName[u.ID] = u.DisplayName
	}
	devByID := map[string]Device{}
	for _, d := range devs {
		devByID[d.ID] = d
	}
	owners, viewers := map[string]bool{}, map[string]bool{}
	s.sess.mu.Lock()
	ids, err := s.activeSessionIDs(ctx)
	if err == nil {
		for _, id := range ids {
			if d, err := s.loadSession(ctx, id); err == nil {
				owners[d.ownerDeviceID] = true
				for _, v := range d.viewers {
					viewers[v.deviceID] = true
				}
			}
		}
	}
	s.sess.mu.Unlock()
	hubVer := s.Info().HubVersion
	out := make([]PlayerStatus, 0, len(conns))
	for _, c := range conns {
		d, ok := devByID[c.deviceID]
		if !ok {
			continue
		}
		c.mu.Lock()
		state := c.state
		c.mu.Unlock()
		p := PlayerStatus{DeviceName: d.Name, User: userName[d.UserID], Version: d.PlayerVersion, State: PlayerIdle}
		switch {
		case owners[c.deviceID]:
			p.State = PlayerInSession
		case viewers[c.deviceID]:
			p.State = PlayerWatching
		case state == "in_game":
			p.State = PlayerPlaying
		}
		if updates.ValidSemVer(hubVer) && updates.ValidSemVer(d.PlayerVersion) {
			if cmp, err := updates.CompareVersions(d.PlayerVersion, hubVer); err == nil && cmp < 0 {
				p.Outdated = true
			}
		}
		out = append(out, p)
	}
	sort.Slice(out, func(i, j int) bool { return strings.ToLower(out[i].DeviceName) < strings.ToLower(out[j].DeviceName) })
	return out, nil
}

// LastUpdateFailure returns the last update install result when it failed, else nil (a later success replaces it).
func (s *Service) LastUpdateFailure() *updates.Result {
	r, err := updates.ReadResult(s.dataDir)
	if err != nil || r == nil || r.OK {
		return nil
	}
	return r
}

// ---- Metrics endpoint settings ----

const (
	settingMetricsEnabled   = "metrics_enabled"
	settingMetricsTokenHash = "metrics_token_hash"

	metricsTokenPrefix = "fbm_"
)

// MetricsEnabled reports whether the /metrics endpoint is on (default off).
func (s *Service) MetricsEnabled(ctx context.Context) bool {
	v, _, err := s.getSetting(ctx, settingMetricsEnabled)
	return err == nil && v == "1"
}

// MetricsTokenSet reports whether a metrics token exists (only its hash is stored).
func (s *Service) MetricsTokenSet(ctx context.Context) bool {
	v, ok, err := s.getSetting(ctx, settingMetricsTokenHash)
	return err == nil && ok && v != ""
}

// SetMetricsEnabled switches the endpoint. Switching it on without a token creates one and returns it; this is the
// only time the token is visible. In every other case the returned token is empty.
func (s *Service) SetMetricsEnabled(ctx context.Context, on bool) (token string, err error) {
	if !on {
		return "", s.setSetting(ctx, settingMetricsEnabled, "0")
	}
	if !s.MetricsTokenSet(ctx) {
		if token, err = s.RegenerateMetricsToken(ctx); err != nil {
			return "", err
		}
	}
	return token, s.setSetting(ctx, settingMetricsEnabled, "1")
}

// RegenerateMetricsToken replaces the token and returns the new one. Only its SHA-256 is stored; the old token stops
// working at once.
func (s *Service) RegenerateMetricsToken(ctx context.Context) (string, error) {
	tok, err := auth.NewToken(metricsTokenPrefix)
	if err != nil {
		return "", internal(err)
	}
	if err := s.setSetting(ctx, settingMetricsTokenHash, auth.HashToken(tok)); err != nil {
		return "", err
	}
	return tok, nil
}

// CheckMetricsToken reports whether the endpoint is on and token is its current token (constant time).
func (s *Service) CheckMetricsToken(ctx context.Context, token string) bool {
	if token == "" || !s.MetricsEnabled(ctx) {
		return false
	}
	want, ok, err := s.getSetting(ctx, settingMetricsTokenHash)
	if err != nil || !ok || want == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(auth.HashToken(token)), []byte(want)) == 1
}
