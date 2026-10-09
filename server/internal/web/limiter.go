package web

import (
	"context"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

// Login rate limiting and password-verification throttling.
const (
	loginMaxPerIP    = 5                   // attempts per window and client address
	loginWindow      = time.Minute         // window of the per-address limit
	userMaxFailures  = 10                  // failures per username within userWindow before the backoff starts
	userWindow       = 15 * time.Minute    // window of the per-username failure count
	userBackoffBase  = 30 * time.Second    // first username block; doubles per further strike
	userBackoffMax   = 15 * time.Minute    // upper bound of a username block
	limiterSweepSize = 1024                // sweep stale entries once a map grows beyond this
	trustedFor       = 30 * 24 * time.Hour // a successful login exempts its address from the username block
	limiterHardCap   = 10000               // hard bound per map; arbitrary entries are dropped beyond it

	maxConcurrentVerify = 2 // simultaneous Argon2id verifications (64 MiB each)
)

// verifyWait is how long a request waits for a free verification slot (a variable for tests).
var verifyWait = 3 * time.Second

// limiter counts login attempts per client address (IPv6 by /64) and per username. Attempts are reserved
// (take) before the password is verified, so concurrent requests cannot exceed the limit; a successful login
// refunds its reservation.
type limiter struct {
	mu      sync.Mutex
	now     func() time.Time
	hits    map[string][]time.Time
	users   map[string]*userFails
	sweptAt time.Time
	// trusted: address (/64) + username -> last successful login; such addresses skip the username block.
	trusted map[string]time.Time
}

type userFails struct {
	times        []time.Time
	strikes      int
	blockedUntil time.Time
	last         time.Time
}

func newLimiter(now func() time.Time) *limiter {
	return &limiter{now: now, hits: map[string][]time.Time{}, users: map[string]*userFails{}, trusted: map[string]time.Time{}}
}

// ipKey keys a client address: IPv4 by the full address, IPv6 by its /64 prefix.
func ipKey(addr string) string {
	ip := net.ParseIP(addr)
	if ip == nil {
		return addr
	}
	if v4 := ip.To4(); v4 != nil {
		return v4.String()
	}
	return ip.Mask(net.CIDRMask(64, 128)).String() + "/64"
}

// userExempt reports whether the username block does not apply to addr: private, loopback and link-local
// addresses (the admin on the LAN) and addresses that logged in as this user within trustedFor.
func (l *limiter) userExempt(addr, uk string, now time.Time) bool {
	if ip := net.ParseIP(addr); ip != nil && (ip.IsPrivate() || ip.IsLoopback() || ip.IsLinkLocalUnicast()) {
		return true
	}
	t, ok := l.trusted[ipKey(addr)+"|"+uk]
	return ok && now.Sub(t) <= trustedFor
}

// userKey normalizes a username for the failure counter. Unknown names are counted the same as known ones.
func userKey(name string) string {
	name = strings.ToLower(strings.TrimSpace(name))
	if len(name) > 128 {
		name = name[:128]
	}
	return name
}

func (l *limiter) pruneHits(key string, now time.Time) []time.Time {
	cut := now.Add(-loginWindow)
	h := l.hits[key]
	i := 0
	for i < len(h) && !h[i].After(cut) {
		i++
	}
	h = h[i:]
	if len(h) == 0 {
		delete(l.hits, key)
	} else {
		l.hits[key] = h
	}
	return h
}

// sweep drops stale entries; it runs when a map has grown large (at most every 10 s) and enforces the hard cap.
func (l *limiter) sweep(now time.Time) {
	if (len(l.hits) > limiterSweepSize || len(l.users) > limiterSweepSize || len(l.trusted) > limiterSweepSize) && now.Sub(l.sweptAt) >= 10*time.Second {
		l.sweptAt = now
		for k := range l.hits {
			l.pruneHits(k, now)
		}
		for k, t := range l.trusted {
			if now.Sub(t) > trustedFor {
				delete(l.trusted, k)
			}
		}
		for k, u := range l.users {
			if now.After(u.blockedUntil) && now.Sub(u.last) > userWindow {
				delete(l.users, k)
			}
		}
	}
	for k := range l.hits {
		if len(l.hits) <= limiterHardCap {
			break
		}
		delete(l.hits, k)
	}
	for k := range l.trusted {
		if len(l.trusted) <= limiterHardCap {
			break
		}
		delete(l.trusted, k)
	}
	for k := range l.users {
		if len(l.users) <= limiterHardCap {
			break
		}
		delete(l.users, k)
	}
}

// take reserves one login attempt for the client address and username. It returns false (nothing reserved) when
// the address is over its limit or the username is blocked; the caller does not verify the password then.
func (l *limiter) take(addr, user string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	l.sweep(now)
	key, uk := ipKey(addr), userKey(user)
	exempt := l.userExempt(addr, uk, now)
	if u := l.users[uk]; !exempt && u != nil && now.Before(u.blockedUntil) {
		return false
	}
	h := l.pruneHits(key, now)
	if len(h) >= loginMaxPerIP {
		return false
	}
	l.hits[key] = append(h, now)
	if exempt {
		return true
	}
	u := l.users[uk]
	if u == nil {
		u = &userFails{}
		l.users[uk] = u
	}
	cut := now.Add(-userWindow)
	i := 0
	for i < len(u.times) && !u.times[i].After(cut) {
		i++
	}
	u.times = append(u.times[i:], now)
	u.last = now
	if len(u.times) >= userMaxFailures {
		u.strikes++
		d := userBackoffMax
		if u.strikes < 6 {
			d = min(userBackoffBase<<(u.strikes-1), userBackoffMax)
		}
		u.blockedUntil = now.Add(d)
		u.times = nil
	}
	return true
}

// refund releases a reservation made by take (successful login, or verification not carried out).
func (l *limiter) refund(addr, user string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	key, uk := ipKey(addr), userKey(user)
	exempt := l.userExempt(addr, uk, l.now())
	if h := l.hits[key]; len(h) > 0 {
		if len(h) == 1 {
			delete(l.hits, key)
		} else {
			l.hits[key] = h[:len(h)-1]
		}
	}
	if u := l.users[uk]; !exempt && u != nil && len(u.times) > 0 {
		u.times = u.times[:len(u.times)-1]
		if len(u.times) == 0 && u.strikes == 0 {
			delete(l.users, uk)
		}
	}
}

// success refunds the reservation of a successful login and remembers the address for this user.
func (l *limiter) success(addr, user string) {
	l.refund(addr, user)
	l.mu.Lock()
	defer l.mu.Unlock()
	l.trusted[ipKey(addr)+"|"+userKey(user)] = l.now()
}

// acquireVerify takes one of the Argon2id verification slots. When none frees up within verifyWait (or the request
// ends), ok is false. The caller calls release exactly once when ok.
func (s *Server) acquireVerify(ctx context.Context) (release func(), ok bool) {
	select {
	case s.verifySem <- struct{}{}:
	default:
		t := time.NewTimer(verifyWait)
		defer t.Stop()
		select {
		case s.verifySem <- struct{}{}:
		case <-t.C:
			return nil, false
		case <-ctx.Done():
			return nil, false
		}
	}
	return func() { <-s.verifySem }, true
}

// busy answers a request that found no free verification slot.
func busy(w http.ResponseWriter) {
	w.Header().Set("Retry-After", "5")
	http.Error(w, "The Hub is busy verifying passwords. Please try again in a moment.", http.StatusServiceUnavailable)
}
