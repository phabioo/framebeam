package tlsutil

import (
	"crypto/sha256"
	"crypto/tls"
	"errors"
	"os"
	"strings"
)

// Crockford is the base32 alphabet of the security code and the setup code (no I, L, O, U).
const Crockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

// SecurityCode returns the security code of the certificate a server serves: the first 60 bits of the SHA-256 of the
// leaf certificate (DER) as 12 Crockford base32 characters in groups of four, for example "K7Q4-9WMP-2HXD".
// The app computes the same value from the certificate it pinned (docs/reference/protocol.md, "Security code").
// It is empty when cert has no certificate.
func SecurityCode(cert tls.Certificate) string {
	if len(cert.Certificate) == 0 {
		return ""
	}
	return SecurityCodeDER(cert.Certificate[0])
}

// SecurityCodeDER is SecurityCode for a leaf certificate in DER form.
func SecurityCodeDER(der []byte) string {
	sum := sha256.Sum256(der)
	out := make([]byte, 0, 14)
	// 12 symbols x 5 bits = 60 bits, taken from the start of the digest, most significant bit first.
	for i := 0; i < 12; i++ {
		if i > 0 && i%4 == 0 {
			out = append(out, '-')
		}
		bit := i * 5
		// The 5 bits may span two bytes.
		v := uint16(sum[bit/8]) << 8
		if bit/8+1 < len(sum) {
			v |= uint16(sum[bit/8+1])
		}
		out = append(out, Crockford[(v>>(11-uint(bit%8)))&31])
	}
	return string(out)
}

// NormalizeCode removes spaces and hyphens and upper-cases a typed security or setup code.
func NormalizeCode(s string) string {
	return strings.ToUpper(strings.Map(func(r rune) rune {
		if r == ' ' || r == '-' || r == '\t' {
			return -1
		}
		return r
	}, s))
}

// SecurityCodesMatch compares two security codes ignoring spaces, hyphens and case.
func SecurityCodesMatch(a, b string) bool {
	na, nb := NormalizeCode(a), NormalizeCode(b)
	return na != "" && na == nb
}

// LoadSelfSigned loads the self-generated certificate of dataDir without creating, renewing or repairing anything
// (the CLI commands run next to the server and possibly as another user). It wraps os.ErrNotExist when the server
// has not created one yet.
func LoadSelfSigned(dataDir string) (tls.Certificate, error) {
	_, certFile, keyFile := selfSignedPaths(dataDir)
	c, err := Load(certFile, keyFile)
	if err != nil {
		if _, serr := os.Stat(certFile); errors.Is(serr, os.ErrNotExist) {
			return tls.Certificate{}, os.ErrNotExist
		}
		return tls.Certificate{}, err
	}
	return c, nil
}
