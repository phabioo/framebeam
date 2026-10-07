// Package tlsutil creates and loads the hub's TLS certificate.
package tlsutil

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Fingerprint returns the SHA-256 fingerprint of the leaf certificate (hex, colons, uppercase).
func Fingerprint(cert tls.Certificate) string {
	if len(cert.Certificate) == 0 {
		return ""
	}
	sum := sha256.Sum256(cert.Certificate[0])
	h := strings.ToUpper(hex.EncodeToString(sum[:]))
	parts := make([]string, 0, 32)
	for i := 0; i < len(h); i += 2 {
		parts = append(parts, h[i:i+2])
	}
	return strings.Join(parts, ":")
}

// Load loads a certificate/key pair (PEM).
func Load(certFile, keyFile string) (tls.Certificate, error) {
	return tls.LoadX509KeyPair(certFile, keyFile)
}

// RenewBefore is the remaining validity below which a self-generated certificate is renewed.
const RenewBefore = 30 * 24 * time.Hour

// selfSignedYears is the validity of a self-generated certificate.
const selfSignedYears = 10

// Renewal describes a replaced self-generated certificate.
type Renewal struct {
	OldFingerprint, NewFingerprint string
	// OldNotAfter is the expiry of the replaced certificate.
	OldNotAfter time.Time
}

// NeedsRenewal reports whether a certificate is expired or expires within RenewBefore of now.
func NeedsRenewal(leaf *x509.Certificate, now time.Time) bool {
	return ExpiresSoon(leaf.NotAfter, now)
}

// ExpiresSoon reports whether notAfter lies within RenewBefore of now (or in the past).
func ExpiresSoon(notAfter, now time.Time) bool {
	return !now.Add(RenewBefore).Before(notAfter)
}

func selfSignedPaths(dataDir string) (dir, certFile, keyFile string) {
	dir = filepath.Join(dataDir, "tls")
	return dir, filepath.Join(dir, "cert.pem"), filepath.Join(dir, "key.pem")
}

// EnsureSelfSigned loads <dataDir>/tls/cert.pem and key.pem or, on first start, creates a
// self-signed ECDSA P-256 certificate (SANs: hostname, localhost, 127.0.0.1, ::1, local IPs).
// It also renews an expired or soon-expiring certificate (see EnsureSelfSignedRenewing).
func EnsureSelfSigned(dataDir string) (tls.Certificate, error) {
	c, _, err := EnsureSelfSignedRenewing(dataDir, time.Now())
	return c, err
}

// EnsureSelfSignedRenewing is EnsureSelfSigned with an explicit clock. When an existing
// certificate is expired or expires within RenewBefore, it is replaced and the returned
// Renewal is non-nil; the previous files are kept as cert.pem.prev / key.pem.prev.
func EnsureSelfSignedRenewing(dataDir string, now time.Time) (tls.Certificate, *Renewal, error) {
	_, certFile, keyFile := selfSignedPaths(dataDir)
	if _, err := os.Stat(certFile); err == nil {
		c, err := Load(certFile, keyFile)
		if err != nil {
			return tls.Certificate{}, nil, err
		}
		leaf, err := x509.ParseCertificate(c.Certificate[0])
		if err != nil {
			return tls.Certificate{}, nil, err
		}
		if !NeedsRenewal(leaf, now) {
			return c, nil, nil
		}
		return renew(dataDir, c, leaf, now)
	} else if !errors.Is(err, os.ErrNotExist) {
		return tls.Certificate{}, nil, err
	}
	c, err := create(dataDir, now)
	return c, nil, err
}

// RenewSelfSigned replaces the self-generated certificate unconditionally (admin command).
// It fails when no self-generated certificate exists yet.
func RenewSelfSigned(dataDir string, now time.Time) (tls.Certificate, *Renewal, error) {
	_, certFile, keyFile := selfSignedPaths(dataDir)
	c, err := Load(certFile, keyFile)
	if err != nil {
		return tls.Certificate{}, nil, fmt.Errorf("load current certificate: %w", err)
	}
	leaf, err := x509.ParseCertificate(c.Certificate[0])
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	return renew(dataDir, c, leaf, now)
}

// renew keeps the old pair as *.prev and creates a new certificate.
func renew(dataDir string, old tls.Certificate, leaf *x509.Certificate, now time.Time) (tls.Certificate, *Renewal, error) {
	_, certFile, keyFile := selfSignedPaths(dataDir)
	if err := os.Rename(keyFile, keyFile+".prev"); err != nil {
		return tls.Certificate{}, nil, fmt.Errorf("back up tls key: %w", err)
	}
	if err := os.Rename(certFile, certFile+".prev"); err != nil {
		_ = os.Rename(keyFile+".prev", keyFile)
		return tls.Certificate{}, nil, fmt.Errorf("back up tls cert: %w", err)
	}
	c, err := create(dataDir, now)
	if err != nil {
		// Restore the previous pair so the Hub keeps a usable certificate.
		_ = os.Rename(keyFile+".prev", keyFile)
		_ = os.Rename(certFile+".prev", certFile)
		return tls.Certificate{}, nil, err
	}
	return c, &Renewal{OldFingerprint: Fingerprint(old), NewFingerprint: Fingerprint(c), OldNotAfter: leaf.NotAfter}, nil
}

// create generates and writes a new self-signed certificate valid from now-1h.
func create(dataDir string, now time.Time) (tls.Certificate, error) {
	dir, certFile, keyFile := selfSignedPaths(dataDir)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return tls.Certificate{}, err
	}
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 127))
	if err != nil {
		return tls.Certificate{}, err
	}
	host, _ := os.Hostname()
	dns, ips := sans(host)
	cn := host
	if cn == "" {
		cn = "framebeam-hub"
	}
	tpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: cn, Organization: []string{"FrameBeam Hub"}},
		NotBefore:             now.Add(-time.Hour),
		NotAfter:              now.AddDate(selfSignedYears, 0, 0),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              dns,
		IPAddresses:           ips,
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}
	keyDER, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		return tls.Certificate{}, err
	}
	if err := os.WriteFile(keyFile, pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDER}), 0o600); err != nil {
		return tls.Certificate{}, fmt.Errorf("write tls key: %w", err)
	}
	if err := os.WriteFile(certFile, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), 0o644); err != nil {
		return tls.Certificate{}, fmt.Errorf("write tls cert: %w", err)
	}
	return Load(certFile, keyFile)
}

func sans(host string) ([]string, []net.IP) {
	dns := []string{"localhost"}
	if host != "" && host != "localhost" {
		dns = append(dns, host)
	}
	ips := []net.IP{net.ParseIP("127.0.0.1"), net.IPv6loopback}
	if addrs, err := net.InterfaceAddrs(); err == nil {
		for _, a := range addrs {
			if ipn, ok := a.(*net.IPNet); ok && !ipn.IP.IsLoopback() && !ipn.IP.IsLinkLocalMulticast() {
				ips = append(ips, ipn.IP)
			}
		}
	}
	return dns, ips
}
