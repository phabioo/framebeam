package hub

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// Cloudflare Realtime TURN as an optional relay provider next to the embedded TURN server. The Hub fetches one
// shared credential set from the Cloudflare API (ttl 24 h), refreshes it at half the TTL and hands it to every
// Player in hello_ack, the join response and ice_update. The API token only exists in memory and in the request
// header; it is never logged, stored or part of an error.

const (
	// DefaultCloudflareTURNBase is the Cloudflare Realtime API base URL.
	DefaultCloudflareTURNBase = "https://rtc.live.cloudflare.com"
	cloudflareTTL             = 24 * time.Hour
	cloudflareMaxBody         = 1 << 20
)

// CloudflareTURNOptions configure StartCloudflareTURN. BaseURL, TTL, HTTPClient, RetryMin and RetryMax have
// defaults and are overridden by tests.
type CloudflareTURNOptions struct {
	KeyID      string
	Token      string
	BaseURL    string
	TTL        time.Duration
	HTTPClient *http.Client
	Log        *slog.Logger
	// RetryMin and RetryMax bound the backoff after a failed refresh (default 30 s and 10 min).
	RetryMin, RetryMax time.Duration
}

type cloudflareState struct {
	configured atomic.Bool
	mu         sync.RWMutex
	stun       []string
	creds      *turnsrv.Credentials
	keyID      string
	lastAt     time.Time // last refresh attempt (zero: none yet)
	lastErr    string    // empty: the last attempt succeeded
}

// CloudflareStatus is the Cloudflare TURN state for diagnostics. It never holds the API token.
type CloudflareStatus struct {
	Configured  bool
	KeyID       string // as configured; the caller shortens it
	LastAttempt time.Time
	LastError   string // empty: ok
}

// CloudflareStatus reports whether the Cloudflare TURN provider runs and how its last credential refresh ended.
func (s *Service) CloudflareStatus() CloudflareStatus {
	s.cf.mu.RLock()
	defer s.cf.mu.RUnlock()
	return CloudflareStatus{Configured: s.cf.configured.Load(), KeyID: s.cf.keyID, LastAttempt: s.cf.lastAt, LastError: s.cf.lastErr}
}

func (c *cloudflareState) record(at time.Time, err error) {
	c.mu.Lock()
	c.lastAt, c.lastErr = at, ""
	if err != nil {
		c.lastErr = err.Error()
	}
	c.mu.Unlock()
}

// current returns copies of the cached STUN URLs and credentials (nil while there are none).
func (c *cloudflareState) current() ([]string, *turnsrv.Credentials) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.creds == nil {
		return nil, nil
	}
	cp := *c.creds
	cp.URLs = append([]string(nil), c.creds.URLs...)
	return append([]string(nil), c.stun...), &cp
}

func (c *cloudflareState) set(stun []string, creds *turnsrv.Credentials) {
	c.mu.Lock()
	c.stun, c.creds = stun, creds
	c.mu.Unlock()
}

// StartCloudflareTURN starts the background refresh of the Cloudflare TURN credentials; it ends with ctx. It
// returns at once; Players get the credentials by ice_update as soon as the first fetch succeeded.
func (s *Service) StartCloudflareTURN(ctx context.Context, o CloudflareTURNOptions) {
	if o.BaseURL == "" {
		o.BaseURL = DefaultCloudflareTURNBase
	}
	if o.TTL <= 0 {
		o.TTL = cloudflareTTL
	}
	if o.HTTPClient == nil {
		o.HTTPClient = &http.Client{Timeout: 20 * time.Second}
	}
	if o.Log == nil {
		o.Log = slog.Default()
	}
	if o.RetryMin <= 0 {
		o.RetryMin = 30 * time.Second
	}
	if o.RetryMax < o.RetryMin {
		o.RetryMax = max(10*time.Minute, o.RetryMin)
	}
	s.cf.mu.Lock()
	s.cf.keyID = o.KeyID
	s.cf.mu.Unlock()
	s.cf.configured.Store(true)
	go func() {
		defer s.cf.configured.Store(false)
		s.cloudflareLoop(ctx, o)
	}()
}

func (s *Service) cloudflareLoop(ctx context.Context, o CloudflareTURNOptions) {
	backoff, failing := o.RetryMin, false
	for {
		var wait time.Duration
		stun, creds, err := fetchCloudflare(ctx, o, s.now())
		if ctx.Err() == nil {
			s.cf.record(s.now(), err)
		}
		switch {
		case ctx.Err() != nil:
			return
		case err == nil:
			if failing {
				o.Log.Info("Cloudflare TURN credentials refreshed again")
			}
			failing, backoff, wait = false, o.RetryMin, o.TTL/2
			s.cf.set(stun, &creds)
			s.PushICEUpdate()
		default:
			if !failing {
				o.Log.Warn("Cloudflare TURN credentials could not be refreshed; retrying with backoff", "err", err.Error())
			}
			failing, wait = true, backoff
			backoff = min(backoff*2, o.RetryMax)
			if _, cur := s.cf.current(); cur != nil && !cur.ExpiresAt.After(s.now()) {
				s.cf.set(nil, nil) // expired credentials are useless
				s.PushICEUpdate()
			}
		}
		t := time.NewTimer(wait)
		select {
		case <-ctx.Done():
			t.Stop()
			return
		case <-t.C:
		}
	}
}

// cfURLs accepts "urls" as a string or an array of strings.
type cfURLs []string

func (u *cfURLs) UnmarshalJSON(b []byte) error {
	var one string
	if json.Unmarshal(b, &one) == nil {
		*u = cfURLs{one}
		return nil
	}
	var many []string
	if err := json.Unmarshal(b, &many); err != nil {
		return err
	}
	*u = many
	return nil
}

type cfResponse struct {
	ICEServers []struct {
		URLs       cfURLs `json:"urls"`
		Username   string `json:"username"`
		Credential string `json:"credential"`
	} `json:"iceServers"`
}

// cloudflareURLUsable drops URLs on port 53 (blocked by browsers and some firewalls, and unused by libdatachannel).
func cloudflareURLUsable(u string) bool {
	_, rest, _ := strings.Cut(u, ":")
	rest, _, _ = strings.Cut(rest, "?")
	return rest != "" && !strings.HasSuffix(rest, ":53")
}

// fetchCloudflare requests one credential set. Errors never contain the token or the response body.
func fetchCloudflare(ctx context.Context, o CloudflareTURNOptions, now time.Time) (stun []string, creds turnsrv.Credentials, err error) {
	redact := func(e error) error {
		if o.Token != "" {
			return errors.New(strings.ReplaceAll(e.Error(), o.Token, "***"))
		}
		return e
	}
	endpoint := strings.TrimRight(o.BaseURL, "/") + "/v1/turn/keys/" + url.PathEscape(o.KeyID) + "/credentials/generate-ice-servers"
	body, _ := json.Marshal(map[string]int{"ttl": int(o.TTL / time.Second)})
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint, bytes.NewReader(body))
	if err != nil {
		return nil, creds, redact(err)
	}
	req.Header.Set("Authorization", "Bearer "+o.Token)
	req.Header.Set("Content-Type", "application/json")
	resp, err := o.HTTPClient.Do(req)
	if err != nil {
		return nil, creds, redact(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusCreated && resp.StatusCode != http.StatusOK {
		io.Copy(io.Discard, io.LimitReader(resp.Body, cloudflareMaxBody))
		return nil, creds, fmt.Errorf("cloudflare TURN API answered HTTP %d", resp.StatusCode)
	}
	var r cfResponse
	if err := json.NewDecoder(io.LimitReader(resp.Body, cloudflareMaxBody)).Decode(&r); err != nil {
		return nil, creds, errors.New("cloudflare TURN API sent an unreadable answer")
	}
	for _, e := range r.ICEServers {
		for _, u := range e.URLs {
			u = strings.TrimSpace(u)
			switch {
			case !cloudflareURLUsable(u):
			case strings.HasPrefix(u, "stun:"):
				if !contains(stun, u) {
					stun = append(stun, u)
				}
			case (strings.HasPrefix(u, "turn:") || strings.HasPrefix(u, "turns:")) && e.Username != "" && e.Credential != "":
				if creds.Username == "" {
					creds.Username, creds.Credential = e.Username, e.Credential
				}
				if e.Username == creds.Username && !contains(creds.URLs, u) {
					creds.URLs = append(creds.URLs, u)
				}
			}
		}
	}
	if len(creds.URLs) == 0 {
		return nil, creds, errors.New("cloudflare TURN API sent no usable TURN URL")
	}
	creds.ExpiresAt = now.Add(o.TTL)
	return stun, creds, nil
}

func contains(l []string, v string) bool {
	for _, x := range l {
		if x == v {
			return true
		}
	}
	return false
}
