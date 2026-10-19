package clientip

import (
	"net/http"
	"net/http/httptest"
	"net/netip"
	"testing"
)

func mustPrefixes(t *testing.T, s string) []netip.Prefix {
	t.Helper()
	l, err := ParseList(s)
	if err != nil {
		t.Fatal(err)
	}
	return l
}

func TestParseList(t *testing.T) {
	got := mustPrefixes(t, "10.0.0.0/8, 192.168.1.5 ::1\t2001:db8::/32")
	want := []string{"10.0.0.0/8", "192.168.1.5/32", "::1/128", "2001:db8::/32"}
	if len(got) != len(want) {
		t.Fatalf("%v", got)
	}
	for i := range want {
		if got[i].String() != want[i] {
			t.Fatalf("%v", got)
		}
	}
	if l, err := ParseList(""); err != nil || len(l) != 0 {
		t.Fatal("empty list must be valid and empty")
	}
	if _, err := ParseList("10.0.0.0/8,nope"); err == nil {
		t.Fatal("garbage must be rejected")
	}
}

// seen is what a handler behind the middleware observes.
type seen struct {
	remote string
	key    string
	local  bool
	proxy  bool
	https  bool
}

func resolve(t *testing.T, trusted, remote, host string, hdr map[string][]string) seen {
	t.Helper()
	var out seen
	h := Middleware(mustPrefixes(t, trusted))(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		out = seen{r.RemoteAddr, Key(r), Local(r), Proxied(r), HTTPS(r)}
	}))
	r := httptest.NewRequest("GET", "/", nil)
	r.RemoteAddr = remote
	r.Host = host
	for k, v := range hdr {
		r.Header[http.CanonicalHeaderKey(k)] = v
	}
	h.ServeHTTP(httptest.NewRecorder(), r)
	return out
}

type hdrs = map[string][]string

func TestLocalMatrixWithoutTrustedProxy(t *testing.T) {
	for _, c := range []struct {
		name, remote, host string
		hdr                hdrs
		want               bool
	}{
		{"loopback localhost", "127.0.0.1:1", "localhost:8443", nil, true},
		{"loopback ip host", "127.0.0.1:1", "127.0.0.1:8443", nil, true},
		{"loopback v6", "[::1]:1", "[::1]:8443", nil, true},
		{"loopback v6 no port", "[::1]:1", "[::1]", nil, true},
		{"loopback localhost dot caps", "127.0.0.1:1", "LOCALHOST.", nil, true},
		{"loopback dns host (nginx without headers)", "127.0.0.1:1", "hub.example.org", nil, false},
		{"loopback empty host", "127.0.0.1:1", "", nil, false},
		{"loopback private host", "127.0.0.1:1", "192.168.1.5:8443", nil, false},
		{"private peer", "192.168.1.5:1", "localhost", nil, false},
		{"public peer", "203.0.113.9:1", "localhost", nil, false},
		{"xff", "127.0.0.1:1", "localhost", hdrs{"X-Forwarded-For": {"203.0.113.9"}}, false},
		{"forwarded", "127.0.0.1:1", "localhost", hdrs{"Forwarded": {"for=203.0.113.9"}}, false},
		{"x-real-ip", "127.0.0.1:1", "localhost", hdrs{"X-Real-IP": {"203.0.113.9"}}, false},
		{"x-forwarded-host", "127.0.0.1:1", "localhost", hdrs{"X-Forwarded-Host": {"hub.example.org"}}, false},
		{"x-forwarded-proto", "127.0.0.1:1", "localhost", hdrs{"X-Forwarded-Proto": {"https"}}, false},
	} {
		got := resolve(t, "", c.remote, c.host, c.hdr)
		if got.local != c.want || got.proxy {
			t.Errorf("%s: local=%v proxied=%v, want local=%v", c.name, got.local, got.proxy, c.want)
		}
	}
}

func TestUntrustedPeerHeadersAreNotBelieved(t *testing.T) {
	hdr := hdrs{"X-Forwarded-For": {"198.51.100.7"}, "X-Forwarded-Proto": {"https"}}
	// The peer is not in the trusted list: no rewrite, no https; the headers only disqualify.
	got := resolve(t, "10.0.0.0/8", "203.0.113.9:4444", "hub.example.org", hdr)
	if got.remote != "203.0.113.9:4444" || got.https || got.proxy || got.key != "proxied" {
		t.Fatalf("%+v", got)
	}
	got = resolve(t, "", "127.0.0.1:4444", "localhost", hdr)
	if got.https || got.local || got.key != "proxied" {
		t.Fatalf("%+v", got)
	}
	// A loopback request that is not local (nginx without headers, Host rewritten) is one shared key, never exempt.
	got = resolve(t, "", "127.0.0.1:4444", "hub.example.org", nil)
	if got.local || got.key != "proxied" {
		t.Fatalf("%+v", got)
	}
	// A plain private client without headers keeps its own address.
	if got = resolve(t, "", "192.168.1.5:4444", "hub.example.org", nil); got.key != "192.168.1.5" {
		t.Fatalf("%+v", got)
	}
}

func TestTrustedProxyNeverLocal(t *testing.T) {
	for _, hdr := range []hdrs{nil, {"X-Forwarded-For": {"203.0.113.9"}}, {"X-Forwarded-For": {"127.0.0.1"}}} {
		got := resolve(t, "127.0.0.1/32", "127.0.0.1:5000", "localhost", hdr)
		if got.local || !got.proxy {
			t.Fatalf("%v: %+v", hdr, got)
		}
	}
	got := resolve(t, "10.0.0.0/8", "10.1.2.3:5000", "localhost", nil)
	if got.local || !got.proxy {
		t.Fatalf("%+v", got)
	}
}

func TestClientIPExtraction(t *testing.T) {
	const trusted = "10.0.0.0/8,2001:db8::/32"
	for _, c := range []struct {
		name string
		hdr  hdrs
		want string // RemoteAddr after the middleware
		key  string
	}{
		{"single", hdrs{"X-Forwarded-For": {"203.0.113.9"}}, "203.0.113.9:0", "203.0.113.9"},
		{"spoofed left-most ignored", hdrs{"X-Forwarded-For": {"1.1.1.1, 203.0.113.9"}}, "203.0.113.9:0", "203.0.113.9"},
		{"spoofed loopback left-most ignored", hdrs{"X-Forwarded-For": {"127.0.0.1, 203.0.113.9"}}, "203.0.113.9:0", "203.0.113.9"},
		{"trusted hops skipped", hdrs{"X-Forwarded-For": {"1.1.1.1, 203.0.113.9, 10.9.9.9"}}, "203.0.113.9:0", "203.0.113.9"},
		{"multiple header lines", hdrs{"X-Forwarded-For": {"1.1.1.1", "203.0.113.9"}}, "203.0.113.9:0", "203.0.113.9"},
		{"port and v6", hdrs{"X-Forwarded-For": {"[2001:4860::1]:5555"}}, "[2001:4860::1]:0", "2001:4860::1"},
		{"all trusted: left-most", hdrs{"X-Forwarded-For": {"10.5.5.5, 10.9.9.9"}}, "10.5.5.5:0", "10.5.5.5"},
		{"forwarded for", hdrs{"Forwarded": {`for=1.1.1.1;proto=http, for="[2001:4860::2]:99";proto=https`}}, "[2001:4860::2]:0", "2001:4860::2"},
		{"xff wins over forwarded", hdrs{"X-Forwarded-For": {"203.0.113.9"}, "Forwarded": {"for=198.51.100.1"}}, "203.0.113.9:0", "203.0.113.9"},
		{"garbage: peer, shared key", hdrs{"X-Forwarded-For": {"203.0.113.9, unknown"}}, "10.0.0.2:4000", "proxy:10.0.0.2"},
		{"no header: peer, shared key", nil, "10.0.0.2:4000", "proxy:10.0.0.2"},
	} {
		got := resolve(t, trusted, "10.0.0.2:4000", "hub.example.org", c.hdr)
		if got.remote != c.want || got.key != c.key || !got.proxy || got.local {
			t.Errorf("%s: %+v, want remote %s key %s", c.name, got, c.want, c.key)
		}
	}
}

func TestForwardedProto(t *testing.T) {
	for _, c := range []struct {
		name string
		hdr  hdrs
		want bool
	}{
		{"https", hdrs{"X-Forwarded-Proto": {"https"}}, true},
		{"HTTPS caps", hdrs{"X-Forwarded-Proto": {"HTTPS"}}, true},
		{"http", hdrs{"X-Forwarded-Proto": {"http"}}, false},
		{"client-prepended http, proxy https", hdrs{"X-Forwarded-Proto": {"http, https"}}, true},
		{"client-prepended https, proxy http", hdrs{"X-Forwarded-Proto": {"https, http"}}, false},
		{"forwarded proto", hdrs{"Forwarded": {"for=1.2.3.4;proto=https"}}, true},
		{"absent", nil, false},
	} {
		got := resolve(t, "10.0.0.0/8", "10.0.0.2:1", "h", c.hdr)
		if got.https != c.want {
			t.Errorf("%s: %v", c.name, got.https)
		}
	}
}
