// Package turnsrv embeds the Hub's STUN/TURN server (ADR 0012 D2/D3): pion/turn with short-lived
// REST-scheme credentials bound to paired devices.
package turnsrv

import (
	"crypto/hmac"
	"crypto/sha1" //nolint:gosec // TURN REST API scheme (HMAC-SHA1)
	"encoding/base64"
	"net"
	"strconv"
	"strings"
	"time"
)

// Realm is the TURN realm.
const Realm = "framebeam"

// CredentialTTL is the lifetime of issued credentials (ADR 0012 D3).
const CredentialTTL = 12 * time.Hour

// Credentials are short-lived TURN credentials of one device.
type Credentials struct {
	URLs       []string
	Username   string
	Credential string
	ExpiresAt  time.Time
}

// password is base64(HMAC-SHA1(secret, username)).
func password(secret []byte, username string) string {
	m := hmac.New(sha1.New, secret)
	m.Write([]byte(username))
	return base64.StdEncoding.EncodeToString(m.Sum(nil))
}

// Issue creates credentials for deviceID valid until now+CredentialTTL.
func Issue(secret []byte, deviceID string, now time.Time) (username, credential string, expires time.Time) {
	expires = now.Add(CredentialTTL).UTC().Truncate(time.Second)
	username = strconv.FormatInt(expires.Unix(), 10) + ":" + deviceID
	return username, password(secret, username), expires
}

// ParseUsername splits "<expiry>:<device_id>".
func ParseUsername(username string) (expires time.Time, deviceID string, ok bool) {
	exp, id, found := strings.Cut(username, ":")
	if !found || id == "" {
		return time.Time{}, "", false
	}
	n, err := strconv.ParseInt(exp, 10, 64)
	if err != nil || n <= 0 {
		return time.Time{}, "", false
	}
	return time.Unix(n, 0).UTC(), id, true
}

// Validate checks a username/credential pair against the secret and the clock (device status is checked separately).
func Validate(secret []byte, username, credential string, now time.Time) (deviceID string, ok bool) {
	exp, id, ok := ParseUsername(username)
	if !ok || !now.Before(exp) {
		return "", false
	}
	want := password(secret, username)
	if !hmac.Equal([]byte(want), []byte(credential)) {
		return "", false
	}
	return id, true
}

// HostFor returns the host part for TURN URLs: the request Host without port, falling back to publicHost.
// IPv6 literals are bracketed.
func HostFor(reqHost, publicHost string) string {
	h := strings.TrimSpace(reqHost)
	if hh, _, err := net.SplitHostPort(h); err == nil {
		h = hh
	}
	h = strings.TrimSuffix(strings.TrimPrefix(h, "["), "]")
	if h == "" {
		h = strings.TrimSuffix(strings.TrimPrefix(strings.TrimSpace(publicHost), "["), "]")
	}
	if strings.Contains(h, ":") {
		return "[" + h + "]"
	}
	return h
}

// URLs returns the turn: URLs for host and port.
func URLs(host string, port int) []string {
	hp := net.JoinHostPort(strings.Trim(host, "[]"), strconv.Itoa(port))
	return []string{"turn:" + hp + "?transport=udp", "turn:" + hp + "?transport=tcp"}
}

// STUNURL returns the stun: URL for host and port.
func STUNURL(host string, port int) string {
	return "stun:" + net.JoinHostPort(strings.Trim(host, "[]"), strconv.Itoa(port))
}

// PeerAllowed is the relay peer filter (ADR 0012 D2): loopback, unspecified, multicast and link-local peers are
// refused unless the relay address itself is loopback (tests). Private LAN addresses are allowed on purpose.
func PeerAllowed(relay, peer net.IP) bool {
	if relay != nil && relay.IsLoopback() {
		return true
	}
	if peer == nil {
		return false
	}
	return !(peer.IsLoopback() || peer.IsUnspecified() || peer.IsMulticast() ||
		peer.IsLinkLocalUnicast() || peer.IsLinkLocalMulticast() || peer.IsInterfaceLocalMulticast())
}
