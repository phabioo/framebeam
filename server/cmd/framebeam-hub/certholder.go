package main

import (
	"crypto/tls"
	"crypto/x509"
	"net"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/phabioo/framebeam/server/internal/tlsutil"
)

// certHolder serves the current TLS certificate through tls.Config.GetCertificate so a renewal takes effect
// for new handshakes without a restart. Existing connections keep the certificate they started with.
type certHolder struct {
	cur atomic.Pointer[certState]
	mu  sync.Mutex // serializes renewals
}

type certState struct {
	cert        *tls.Certificate
	fingerprint string
	notAfter    time.Time
}

func newCertHolder(c tls.Certificate) *certHolder {
	h := &certHolder{}
	h.set(c)
	return h
}

func (h *certHolder) set(c tls.Certificate) {
	st := &certState{cert: &c, fingerprint: tlsutil.Fingerprint(c)}
	if len(c.Certificate) > 0 {
		if leaf, err := x509.ParseCertificate(c.Certificate[0]); err == nil {
			st.notAfter = leaf.NotAfter
		}
	}
	h.cur.Store(st)
}

func (h *certHolder) getCertificate(*tls.ClientHelloInfo) (*tls.Certificate, error) {
	return h.cur.Load().cert, nil
}

func (h *certHolder) state() (string, time.Time) {
	st := h.cur.Load()
	return st.fingerprint, st.notAfter
}

// renew replaces the self-generated certificate in dataDir and switches new handshakes to it.
func (h *certHolder) renew(dataDir string, now time.Time) (string, time.Time, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	c, _, err := tlsutil.RenewSelfSigned(dataDir, now)
	if err != nil {
		return "", time.Time{}, err
	}
	h.set(c)
	fp, na := h.state()
	return fp, na, nil
}

// publicPort returns the port of a listen address for invite links, 0 when unknown or the HTTPS default (443).
func publicPort(listen string) int {
	_, p, err := net.SplitHostPort(listen)
	if err != nil {
		return 0
	}
	n, err := strconv.Atoi(p)
	if err != nil || n <= 0 || n == 443 {
		return 0
	}
	return n
}
