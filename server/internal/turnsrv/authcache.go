package turnsrv

import (
	"context"
	"sync"
	"time"
)

// The AuthHandler of the TURN library runs before the message integrity can be checked, so anybody who can reach
// the port can make the server look up arbitrary device IDs. Results are therefore cached (positive and negative,
// bounded) and lookups of IDs that are not cached are rate limited, so a flood cannot saturate the server's one
// database connection.
const (
	// DefaultAuthCacheTTL is how long a DeviceOK result is reused.
	DefaultAuthCacheTTL = 30 * time.Second
	// authNegativeTTL is how long a refusal is reused at most. DeviceOK cannot tell "unknown device" from a database
	// error or timeout, so refusals are kept short: a transient hiccup must not lock a real device out for DefaultAuthCacheTTL.
	authNegativeTTL = 5 * time.Second
	// authCacheMax bounds the cache (positive and negative entries together).
	authCacheMax = 4096
	// Uncached lookups per second (steady) and burst; beyond it an unknown device ID is refused without a lookup.
	authLookupRate  = 20.0
	authLookupBurst = 50.0
)

type authEntry struct {
	ok    bool
	until time.Time
}

type authCache struct {
	ttl time.Duration
	now func() time.Time

	mu      sync.Mutex
	m       map[string]authEntry
	tokens  float64
	refilAt time.Time
}

func newAuthCache(ttl time.Duration, now func() time.Time) *authCache {
	if ttl <= 0 {
		ttl = DefaultAuthCacheTTL
	}
	return &authCache{ttl: ttl, now: now, m: map[string]authEntry{}, tokens: authLookupBurst}
}

// check returns the cached or freshly looked up result for id.
func (c *authCache) check(ctx context.Context, id string, lookup func(context.Context, string) bool) bool {
	now := c.now()
	c.mu.Lock()
	if e, ok := c.m[id]; ok && now.Before(e.until) {
		c.mu.Unlock()
		return e.ok
	}
	// Uncached: take a token for a database lookup.
	if c.refilAt.IsZero() {
		c.refilAt = now
	}
	if d := now.Sub(c.refilAt); d > 0 {
		c.tokens = min(authLookupBurst, c.tokens+d.Seconds()*authLookupRate)
		c.refilAt = now
	}
	if c.tokens < 1 {
		c.mu.Unlock()
		return false
	}
	c.tokens--
	c.mu.Unlock()

	ok := lookup(ctx, id)
	ttl := c.ttl
	if !ok {
		ttl = min(ttl, authNegativeTTL)
		if ctx.Err() != nil {
			ttl = 0 // the lookup timed out or was cancelled: that is no statement about the device, do not cache
		}
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.m) >= authCacheMax {
		for k, e := range c.m {
			if !now.Before(e.until) {
				delete(c.m, k)
			}
		}
		for k := range c.m { // still full: drop arbitrary entries so the map stays bounded
			if len(c.m) < authCacheMax {
				break
			}
			delete(c.m, k)
		}
	}
	if ttl > 0 {
		c.m[id] = authEntry{ok: ok, until: now.Add(ttl)}
	}
	return ok
}

// invalidate forgets id (device revoked).
func (c *authCache) invalidate(id string) {
	c.mu.Lock()
	delete(c.m, id)
	c.mu.Unlock()
}

// InvalidateDevice drops the cached authentication result of a device, e.g. after it was revoked.
func (s *Server) InvalidateDevice(id string) { s.authc.invalidate(id) }
