package main

import (
	"crypto/tls"
	"net"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/tlsutil"
)

// handshakeFP performs a TLS handshake against conf and returns the fingerprint the server presented.
func handshakeFP(t *testing.T, conf *tls.Config) string {
	t.Helper()
	cl, sv := net.Pipe()
	defer cl.Close()
	go func() {
		defer sv.Close()
		_ = tls.Server(sv, conf).Handshake()
	}()
	c := tls.Client(cl, &tls.Config{InsecureSkipVerify: true, ServerName: "localhost"}) //nolint:gosec // test: self-signed
	if err := c.Handshake(); err != nil {
		t.Fatal(err)
	}
	return tlsutil.Fingerprint(tls.Certificate{Certificate: [][]byte{c.ConnectionState().PeerCertificates[0].Raw}})
}

func TestCertHolderRenewWithoutRestart(t *testing.T) {
	dir := t.TempDir()
	c, err := tlsutil.EnsureSelfSigned(dir)
	if err != nil {
		t.Fatal(err)
	}
	h := newCertHolder(c)
	conf := &tls.Config{GetCertificate: h.getCertificate, MinVersion: tls.VersionTLS12}
	fp0, na0 := h.state()
	if fp0 == "" || na0.IsZero() || handshakeFP(t, conf) != fp0 {
		t.Fatal("initial certificate not served")
	}
	fp1, na1, err := h.renew(dir, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if fp1 == fp0 || !na1.After(time.Now()) {
		t.Fatalf("renewal did not change the certificate: %s %s", fp0, fp1)
	}
	if got := handshakeFP(t, conf); got != fp1 {
		t.Fatalf("new handshake served %s, want %s", got, fp1)
	}
	if s, _ := h.state(); s != fp1 {
		t.Fatal("state not updated")
	}
}

func TestCertHolderRenewFailureKeepsCurrent(t *testing.T) {
	dir := t.TempDir()
	c, _ := tlsutil.EnsureSelfSigned(dir)
	h := newCertHolder(c)
	fp0, _ := h.state()
	if _, _, err := h.renew(t.TempDir(), time.Now()); err == nil { // no certificate in that data dir
		t.Fatal("want error")
	}
	if fp, _ := h.state(); fp != fp0 {
		t.Fatal("failed renewal must keep the current certificate")
	}
}

func TestPublicPort(t *testing.T) {
	for in, want := range map[string]int{":8443": 8443, "0.0.0.0:443": 0, "bad": 0, ":0": 0} {
		if got := publicPort(in); got != want {
			t.Errorf("%q: %d want %d", in, got, want)
		}
	}
}
