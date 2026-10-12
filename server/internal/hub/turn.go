package hub

import (
	"context"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"sync/atomic"
	"time"

	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// FeatureTURNV1 is announced in the handshake while the embedded TURN server is on (ADR 0012 D4).
const FeatureTURNV1 = "turn_v1"

// FeatureICEUpdateV1 is announced in the handshake: the Hub pushes ice_update when the ICE configuration changes.
const FeatureICEUpdateV1 = "ice_update_v1"

const settingTURNSecret = "turn_secret"

// TURN is the embedded STUN/TURN server as seen by the service (implemented by *turnsrv.Server).
type TURN interface {
	STUNURL(reqHost string) string
	Credentials(reqHost, deviceID string) turnsrv.Credentials
}

type turnState struct{ p atomic.Pointer[TURN] }

type reqHostKey struct{}

// WithRequestHost returns a context carrying the Host the Player used to reach the Hub (for TURN URLs).
func WithRequestHost(ctx context.Context, host string) context.Context {
	return context.WithValue(ctx, reqHostKey{}, host)
}

func requestHost(ctx context.Context) string {
	h, _ := ctx.Value(reqHostKey{}).(string)
	return h
}

// SetTURN registers the running TURN server (nil switches it off).
func (s *Service) SetTURN(t TURN) {
	if t == nil {
		s.turn.p.Store(nil)
	} else {
		s.turn.p.Store(&t)
	}
	s.PushICEUpdate()
}

// TURNEnabled reports whether the embedded TURN server is on.
func (s *Service) TURNEnabled() bool { return s.turn.p.Load() != nil }

// RelayEnabled reports whether any relay provider is on (embedded TURN, Cloudflare TURN or an external TURN
// server); turn_v1 is announced while it is.
func (s *Service) RelayEnabled() bool {
	return s.TURNEnabled() || s.cf.configured.Load() || len(s.ext.list()) > 0
}

// turnFor returns the embedded server's stun: URL and the TURN credentials for the device; both empty when off.
func (s *Service) turnFor(reqHost, deviceID string) (stun string, creds *turnsrv.Credentials) {
	p := s.turn.p.Load()
	if p == nil {
		return "", nil
	}
	c := (*p).Credentials(reqHost, deviceID)
	return (*p).STUNURL(reqHost), &c
}

// relaysFor returns the stun: URLs of the relay providers (embedded first, then Cloudflare, then the external TURN
// servers) and all TURN credentials of the device.
func (s *Service) relaysFor(reqHost, deviceID string) (stun []string, creds []turnsrv.Credentials) {
	if u, c := s.turnFor(reqHost, deviceID); c != nil {
		stun, creds = append(stun, u), append(creds, *c)
	}
	cfStun, cfCreds := s.cf.current()
	stun = append(stun, cfStun...)
	if cfCreds != nil {
		creds = append(creds, *cfCreds)
	}
	creds = append(creds, s.externalFor(deviceID)...)
	return stun, creds
}

// iceFor returns ice_servers (configured, embedded and provider STUN URLs, without duplicates) and the TURN
// credentials for a device.
func (s *Service) iceFor(reqHost, deviceID string) ([]string, []turnsrv.Credentials) {
	ice := s.ICEServers()
	stun, creds := s.relaysFor(reqHost, deviceID)
	for _, u := range stun {
		if !contains(ice, u) {
			ice = append(ice, u)
		}
	}
	return ice, creds
}

// turnPayload is the turn_servers array of hello_ack, ice_update and viewer_joined.
func turnPayload(creds []turnsrv.Credentials) []map[string]any {
	out := make([]map[string]any, 0, len(creds))
	for _, c := range creds {
		m := map[string]any{"urls": c.URLs, "username": c.Username, "credential": c.Credential}
		if !c.ExpiresAt.IsZero() { // static credentials of an external TURN server do not expire
			m["expires_at"] = c.ExpiresAt.UTC().Format(time.RFC3339)
		}
		out = append(out, m)
	}
	return out
}

// PushICEUpdate sends every connected Player its current ICE configuration (ice_update, feature ice_update_v1),
// computed per connection because embedded TURN credentials are per device. Called whenever the effective
// configuration changes.
func (s *Service) PushICEUpdate() {
	for _, c := range s.clients() {
		ice, turn := s.iceFor(c.reqHost, c.deviceID)
		msg := map[string]any{"ice_servers": ice}
		if len(turn) > 0 {
			msg["turn_servers"] = turnPayload(turn)
		}
		c.sendMsg("ice_update", "", msg)
	}
}

// TURNSecret returns the TURN credential secret (32 random bytes), creating it on first use. Never log it.
func (s *Service) TURNSecret(ctx context.Context) ([]byte, error) {
	for i := 0; i < 3; i++ {
		v, ok, err := s.getSetting(ctx, settingTURNSecret)
		if err != nil {
			return nil, err
		}
		if ok {
			if b, err := base64.StdEncoding.DecodeString(v); err == nil && len(b) == 32 {
				return b, nil
			}
		}
		b := make([]byte, 32)
		if _, err := rand.Read(b); err != nil {
			return nil, internal(err)
		}
		enc := base64.StdEncoding.EncodeToString(b)
		if ok { // unusable stored value: replace
			err = s.setSetting(ctx, settingTURNSecret, enc)
		} else { // a concurrent creator wins; re-read below
			_, err = s.db.ExecContext(ctx, `INSERT INTO settings(key, value) VALUES (?,?) ON CONFLICT(key) DO NOTHING`, settingTURNSecret, enc)
			err = internal2(err)
		}
		if err != nil {
			return nil, err
		}
	}
	return nil, internal(errors.New("turn secret not stored"))
}

// DeviceActive reports whether the device exists and is not revoked (TURN auth).
func (s *Service) DeviceActive(ctx context.Context, id string) bool {
	d, err := s.GetDevice(ctx, id)
	return err == nil && d.Status == DeviceTrusted
}
