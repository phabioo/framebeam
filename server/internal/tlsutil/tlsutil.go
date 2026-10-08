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
	"log/slog"
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
// An interrupted earlier write or renewal is repaired first (see loadCurrent).
func EnsureSelfSignedRenewing(dataDir string, now time.Time) (tls.Certificate, *Renewal, error) {
	c, err := loadCurrent(dataDir)
	if errors.Is(err, os.ErrNotExist) {
		c, err := create(dataDir, now)
		return c, nil, err
	}
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
}

// RenewSelfSigned replaces the self-generated certificate unconditionally (admin command).
// It fails when no self-generated certificate exists yet.
func RenewSelfSigned(dataDir string, now time.Time) (tls.Certificate, *Renewal, error) {
	c, err := loadCurrent(dataDir)
	if err != nil {
		return tls.Certificate{}, nil, fmt.Errorf("load current certificate: %w", err)
	}
	leaf, err := x509.ParseCertificate(c.Certificate[0])
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	return renew(dataDir, c, leaf, now)
}

// pair holds the cert/key paths of one generation (current, ".new" or ".prev").
func pair(certFile, keyFile, suffix string) (string, string) {
	return certFile + suffix, keyFile + suffix
}

func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// loadCurrent loads the self-generated pair and repairs interrupted writes. If the current
// pair is missing, unloadable or mismatched: a complete, loadable ".new" pair finishes the
// interrupted swap; otherwise a loadable ".prev" pair is restored. It returns an error
// wrapping os.ErrNotExist when there is nothing at all (first start). Own certificates
// (-tls-cert/-tls-key) never go through here.
func loadCurrent(dataDir string) (tls.Certificate, error) {
	dir, certFile, keyFile := selfSignedPaths(dataDir)
	c, curErr := Load(certFile, keyFile)
	if curErr == nil {
		return c, nil
	}
	newCert, newKey := pair(certFile, keyFile, ".new")
	prevCert, prevKey := pair(certFile, keyFile, ".prev")
	present := exists(certFile) || exists(keyFile) || exists(newCert) || exists(newKey) || exists(prevCert) || exists(prevKey)
	if !present {
		return tls.Certificate{}, os.ErrNotExist
	}
	log := slog.Default()
	if nc, err := Load(newCert, newKey); err == nil {
		log.Warn("TLS: current certificate unusable, finishing interrupted renewal from staged .new pair",
			"error", curErr, "sha256_fingerprint", Fingerprint(nc))
		if err := swapIn(dir, newKey, keyFile, newCert, certFile); err != nil {
			return tls.Certificate{}, fmt.Errorf("finish interrupted tls renewal: %w", err)
		}
		return Load(certFile, keyFile)
	}
	if pc, err := Load(prevCert, prevKey); err == nil {
		log.Warn("TLS: current certificate unusable, restoring previous .prev pair",
			"error", curErr, "sha256_fingerprint", Fingerprint(pc))
		if err := swapIn(dir, prevKey, keyFile, prevCert, certFile); err != nil {
			return tls.Certificate{}, fmt.Errorf("restore previous tls certificate: %w", err)
		}
		return Load(certFile, keyFile)
	}
	if !exists(certFile) && !exists(keyFile) {
		// Only unusable leftovers (e.g. a lone key from an interrupted first start).
		return tls.Certificate{}, os.ErrNotExist
	}
	return tls.Certificate{}, fmt.Errorf("tls certificate in %s is unusable and no complete .new or .prev pair can replace it: %w", dir, curErr)
}

// swapIn renames the key and cert sources into place and syncs the directory.
func swapIn(dir, srcKey, dstKey, srcCert, dstCert string) error {
	if err := os.Rename(srcKey, dstKey); err != nil {
		return err
	}
	if err := os.Rename(srcCert, dstCert); err != nil {
		return err
	}
	return syncDir(dir)
}

// renew stages a new pair as *.new, keeps the old pair as *.prev and moves the new one in.
func renew(dataDir string, old tls.Certificate, leaf *x509.Certificate, now time.Time) (tls.Certificate, *Renewal, error) {
	dir, certFile, keyFile := selfSignedPaths(dataDir)
	newCert, newKey := pair(certFile, keyFile, ".new")
	keyPEM, certPEM, err := generate(now)
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	if err := writeFileAtomic(newKey, keyPEM, 0o600); err != nil {
		return tls.Certificate{}, nil, fmt.Errorf("stage tls key: %w", err)
	}
	if err := writeFileAtomic(newCert, certPEM, 0o644); err != nil {
		_ = os.Remove(newKey)
		return tls.Certificate{}, nil, fmt.Errorf("stage tls cert: %w", err)
	}
	c, err := Load(newCert, newKey)
	if err != nil {
		_ = os.Remove(newKey)
		_ = os.Remove(newCert)
		return tls.Certificate{}, nil, fmt.Errorf("staged tls pair invalid: %w", err)
	}
	if err := os.Rename(keyFile, keyFile+".prev"); err != nil {
		return tls.Certificate{}, nil, fmt.Errorf("back up tls key: %w", err)
	}
	if err := os.Rename(certFile, certFile+".prev"); err != nil {
		_ = os.Rename(keyFile+".prev", keyFile)
		return tls.Certificate{}, nil, fmt.Errorf("back up tls cert: %w", err)
	}
	// A crash from here on is repaired by loadCurrent (finishes the swap from .new).
	if err := swapIn(dir, newKey, keyFile, newCert, certFile); err != nil {
		return tls.Certificate{}, nil, fmt.Errorf("activate new tls pair: %w", err)
	}
	return c, &Renewal{OldFingerprint: Fingerprint(old), NewFingerprint: Fingerprint(c), OldNotAfter: leaf.NotAfter}, nil
}

// create generates a new self-signed certificate and writes it atomically (first start).
func create(dataDir string, now time.Time) (tls.Certificate, error) {
	dir, certFile, keyFile := selfSignedPaths(dataDir)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return tls.Certificate{}, err
	}
	keyPEM, certPEM, err := generate(now)
	if err != nil {
		return tls.Certificate{}, err
	}
	if err := writeFileAtomic(keyFile, keyPEM, 0o600); err != nil {
		return tls.Certificate{}, fmt.Errorf("write tls key: %w", err)
	}
	if err := writeFileAtomic(certFile, certPEM, 0o644); err != nil {
		return tls.Certificate{}, fmt.Errorf("write tls cert: %w", err)
	}
	return Load(certFile, keyFile)
}

// generate creates a self-signed ECDSA P-256 key/cert pair (PEM) valid from now-1h.
func generate(now time.Time) (keyPEM, certPEM []byte, err error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 127))
	if err != nil {
		return nil, nil, err
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
		return nil, nil, err
	}
	keyDER, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		return nil, nil, err
	}
	return pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDER}),
		pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), nil
}

// writeFileAtomic writes data to a temp file in the same directory (created with perm),
// fsyncs it and renames it over path.
func writeFileAtomic(path string, data []byte, perm os.FileMode) error {
	dir := filepath.Dir(path)
	f, err := os.CreateTemp(dir, filepath.Base(path)+".tmp-*")
	if err != nil {
		return err
	}
	tmp := f.Name()
	cleanup := func(err error) error {
		_ = f.Close()
		_ = os.Remove(tmp)
		return err
	}
	if err := f.Chmod(perm); err != nil {
		return cleanup(err)
	}
	if _, err := f.Write(data); err != nil {
		return cleanup(err)
	}
	if err := f.Sync(); err != nil {
		return cleanup(err)
	}
	if err := f.Close(); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return syncDir(dir)
}

// syncDir fsyncs a directory so renames are durable (best effort where unsupported).
func syncDir(dir string) error {
	d, err := os.Open(dir)
	if err != nil {
		return err
	}
	defer d.Close()
	if err := d.Sync(); err != nil && !errors.Is(err, os.ErrInvalid) {
		return err
	}
	return nil
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
