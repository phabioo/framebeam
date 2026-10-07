package hub

import (
	"context"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"sync/atomic"

	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// FeatureTURNV1 is announced in the handshake while the embedded TURN server is on (ADR 0012 D4).
const FeatureTURNV1 = "turn_v1"

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
		return
	}
	s.turn.p.Store(&t)
}

// TURNEnabled reports whether the embedded TURN server is on.
func (s *Service) TURNEnabled() bool { return s.turn.p.Load() != nil }

// turnFor returns the stun: URL to add to ice_servers and the TURN credentials for the device; both empty when off.
func (s *Service) turnFor(reqHost, deviceID string) (stun string, creds *turnsrv.Credentials) {
	p := s.turn.p.Load()
	if p == nil {
		return "", nil
	}
	c := (*p).Credentials(reqHost, deviceID)
	return (*p).STUNURL(reqHost), &c
}

// iceFor returns ice_servers (configured plus the embedded STUN URL) and the TURN credentials for a device.
func (s *Service) iceFor(reqHost, deviceID string) ([]string, *turnsrv.Credentials) {
	ice := s.ICEServers()
	stun, creds := s.turnFor(reqHost, deviceID)
	if stun != "" {
		ice = append(ice, stun)
	}
	return ice, creds
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
