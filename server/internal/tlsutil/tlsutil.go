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

// EnsureSelfSigned loads <dataDir>/tls/cert.pem and key.pem or, on first start, creates a
// self-signed ECDSA P-256 certificate (SANs: hostname, localhost, 127.0.0.1, ::1, local IPs).
func EnsureSelfSigned(dataDir string) (tls.Certificate, error) {
	dir := filepath.Join(dataDir, "tls")
	certFile, keyFile := filepath.Join(dir, "cert.pem"), filepath.Join(dir, "key.pem")
	if _, err := os.Stat(certFile); err == nil {
		return Load(certFile, keyFile)
	} else if !errors.Is(err, os.ErrNotExist) {
		return tls.Certificate{}, err
	}
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
	now := time.Now()
	tpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: cn, Organization: []string{"FrameBeam Hub"}},
		NotBefore:             now.Add(-time.Hour),
		NotAfter:              now.AddDate(10, 0, 0),
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
