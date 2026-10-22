// Package clientip decides who a request really comes from when the server runs behind a reverse proxy.
//
// Forwarding headers are honoured only when the TCP peer is inside the configured trusted-proxy list
// (-trusted-proxy). Requests from a trusted proxy are never "local" (setup and the local API stay closed to them).
// Without a trusted proxy the headers are never believed; their mere presence only disqualifies a loopback
// request from being local.
package clientip

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"net/netip"
	"strings"
)

// proxyHeaders are the headers a reverse proxy sets on requests it forwards. Their presence on a request from an
// untrusted peer means the request was proxied (it is then not local); their values are not believed.
var proxyHeaders = []string{"Forwarded", "X-Forwarded-For", "X-Real-IP", "X-Forwarded-Host", "X-Forwarded-Proto"}

type ctxKey struct{}

// info is what Middleware learned about a request from a trusted proxy.
type info struct {
	peer    string // TCP peer (the proxy), host only
	unknown bool   // no usable client address in the forwarding headers
	https   bool   // the proxy says the public side is https
}

// ParseList parses a comma or space separated list of CIDRs or single IPs.
func ParseList(s string) ([]netip.Prefix, error) {
	var out []netip.Prefix
	for _, f := range strings.FieldsFunc(s, func(r rune) bool { return r == ',' || r == ' ' || r == '\t' || r == '\n' }) {
		if p, err := netip.ParsePrefix(f); err == nil {
			out = append(out, p.Masked())
			continue
		}
		a, err := netip.ParseAddr(f)
		if err != nil {
			return nil, fmt.Errorf("trusted-proxy: %q is not a CIDR or IP address", f)
		}
		a = a.Unmap()
		out = append(out, netip.PrefixFrom(a, a.BitLen()))
	}
	return out, nil
}

func contains(nets []netip.Prefix, a netip.Addr) bool {
	a = a.Unmap().WithZone("")
	for _, n := range nets {
		if n.Contains(a) {
			return true
		}
	}
	return false
}

func peerHost(remoteAddr string) string {
	host, _, err := net.SplitHostPort(remoteAddr)
	if err != nil {
		return remoteAddr
	}
	return host
}

// Middleware resolves the client of requests that arrive from a trusted proxy: r.RemoteAddr becomes the real
// client address (so rate limits, pairing records and the WSS peer IP use it) and the context records that the
// request is proxied. With an empty list it does nothing.
func Middleware(trusted []netip.Prefix) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		if len(trusted) == 0 {
			return next
		}
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			host := peerHost(r.RemoteAddr)
			peer, err := netip.ParseAddr(host)
			if err != nil || !contains(trusted, peer) {
				next.ServeHTTP(w, r)
				return
			}
			in := info{peer: host}
			if client, ok := forwardedClient(r, trusted); ok {
				r.RemoteAddr = net.JoinHostPort(client.String(), "0")
			} else {
				in.unknown = true
			}
			in.https = forwardedHTTPS(r)
			next.ServeHTTP(w, r.WithContext(context.WithValue(r.Context(), ctxKey{}, in)))
		})
	}
}

func from(r *http.Request) (info, bool) {
	in, ok := r.Context().Value(ctxKey{}).(info)
	return in, ok
}

// Proxied reports whether r arrived from a trusted proxy.
func Proxied(r *http.Request) bool { _, ok := from(r); return ok }

// HTTPS reports whether a trusted proxy says the client connection is https.
func HTTPS(r *http.Request) bool { in, ok := from(r); return ok && in.https }

// Key returns the address that rate limits are keyed on: the real client IP, or, for a trusted proxy that gave no
// usable client address, a non-IP key for the proxy itself (never exempt, one shared bucket).
func Key(r *http.Request) string {
	if in, ok := from(r); ok && in.unknown {
		return "proxy:" + in.peer
	}
	host := peerHost(r.RemoteAddr)
	if _, ok := from(r); !ok {
		// No trusted proxy configured (or this peer is not one). A loopback peer that is not local, or any peer
		// that sends proxy headers, is an unconfigured proxy: all its clients share one non-IP key.
		if ip, err := netip.ParseAddr(host); err == nil && ip.Unmap().IsLoopback() && !Local(r) || hasProxyHeader(r) {
			return "proxied"
		}
	}
	return host
}

func hasProxyHeader(r *http.Request) bool {
	for _, h := range proxyHeaders {
		if len(r.Header.Values(h)) > 0 {
			return true
		}
	}
	return false
}

// Local reports whether r comes from the machine the server runs on: never when it came through a trusted proxy;
// otherwise the TCP peer is loopback, no proxy header is present and the Host header names a loopback address.
// A proxy on the same host that sends none of the headers and rewrites Host to a loopback name cannot be told
// apart from a local client: such deployments must set -trusted-proxy.
func Local(r *http.Request) bool {
	if Proxied(r) {
		return false
	}
	if ip, err := netip.ParseAddr(peerHost(r.RemoteAddr)); err != nil || !ip.Unmap().IsLoopback() {
		return false
	}
	if hasProxyHeader(r) {
		return false
	}
	return loopbackHost(r.Host)
}

func loopbackHost(h string) bool {
	if host, _, err := net.SplitHostPort(h); err == nil {
		h = host
	}
	h = strings.TrimSuffix(strings.Trim(h, "[]"), ".")
	if strings.EqualFold(h, "localhost") {
		return true
	}
	a, err := netip.ParseAddr(h)
	return err == nil && a.Unmap().IsLoopback()
}

func parseHostAddr(s string) (netip.Addr, bool) {
	s = strings.TrimSpace(strings.Trim(strings.TrimSpace(s), `"`))
	if ap, err := netip.ParseAddrPort(s); err == nil {
		return ap.Addr().Unmap(), true
	}
	s = strings.Trim(s, "[]")
	if i := strings.IndexByte(s, '%'); i >= 0 {
		s = s[:i]
	}
	a, err := netip.ParseAddr(s)
	if err != nil {
		return netip.Addr{}, false
	}
	return a.Unmap(), true
}

// forwardedClient returns the right-most address of the chain that is not itself a trusted proxy. Entries added
// by the client (left of the first trusted hop) are therefore never used when a trusted proxy appended its own.
func forwardedClient(r *http.Request, trusted []netip.Prefix) (netip.Addr, bool) {
	var chain []string
	if v := r.Header.Values("X-Forwarded-For"); len(v) > 0 {
		for _, line := range v {
			chain = append(chain, strings.Split(line, ",")...)
		}
	} else {
		for _, el := range forwardedElements(r) {
			if f := el["for"]; f != "" {
				chain = append(chain, f)
			}
		}
	}
	var first netip.Addr
	for i := len(chain) - 1; i >= 0; i-- {
		a, ok := parseHostAddr(chain[i])
		if !ok {
			return netip.Addr{}, false
		}
		if !contains(trusted, a) {
			return a, true
		}
		first = a
	}
	return first, first.IsValid()
}

func forwardedHTTPS(r *http.Request) bool {
	if v := r.Header.Values("X-Forwarded-Proto"); len(v) > 0 {
		parts := strings.Split(v[len(v)-1], ",")
		return strings.EqualFold(strings.TrimSpace(parts[len(parts)-1]), "https")
	}
	if els := forwardedElements(r); len(els) > 0 {
		return strings.EqualFold(els[len(els)-1]["proto"], "https")
	}
	return false
}

// forwardedElements parses the RFC 7239 Forwarded header(s): one map of lower-case parameter names per element.
func forwardedElements(r *http.Request) []map[string]string {
	var out []map[string]string
	for _, line := range r.Header.Values("Forwarded") {
		for _, el := range splitQuoted(line, ',') {
			m := map[string]string{}
			for _, kv := range splitQuoted(el, ';') {
				if k, v, ok := strings.Cut(kv, "="); ok {
					m[strings.ToLower(strings.TrimSpace(k))] = strings.Trim(strings.TrimSpace(v), `"`)
				}
			}
			out = append(out, m)
		}
	}
	return out
}

func splitQuoted(s string, sep byte) []string {
	var out []string
	inQ, start := false, 0
	for i := 0; i < len(s); i++ {
		switch {
		case s[i] == '"':
			inQ = !inQ
		case s[i] == sep && !inQ:
			out = append(out, s[start:i])
			start = i + 1
		}
	}
	return append(out, s[start:])
}
