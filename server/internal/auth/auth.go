// Package auth provides password hashing (Argon2id) and token helpers.
// Tokens are stored only as a SHA-256 hash; comparisons run in constant time.
package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"

	"golang.org/x/crypto/argon2"
)

// Token prefixes (ADR 0002).
const (
	PrefixAccess     = "fba_"
	PrefixDevice     = "fbd_"
	PrefixPoll       = "fbp_"
	tokenRandomBytes = 32
)

// Params are the Argon2id parameters.
type Params struct {
	Time      uint32
	MemoryKiB uint32
	Threads   uint8
	KeyLen    uint32
	SaltLen   uint32
}

// DefaultParams: 64 MiB, 3 iterations.
var DefaultParams = Params{Time: 3, MemoryKiB: 64 * 1024, Threads: 2, KeyLen: 32, SaltLen: 16}

// HashPassword returns a PHC string ($argon2id$v=19$m=..,t=..,p=..$salt$hash).
func HashPassword(password string, p Params) (string, error) {
	salt := make([]byte, p.SaltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key := argon2.IDKey([]byte(password), salt, p.Time, p.MemoryKiB, p.Threads, p.KeyLen)
	return fmt.Sprintf("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s", argon2.Version, p.MemoryKiB, p.Time, p.Threads,
		base64.RawStdEncoding.EncodeToString(salt), base64.RawStdEncoding.EncodeToString(key)), nil
}

// ErrInvalidHash reports an unreadable hash format.
var ErrInvalidHash = errors.New("auth: invalid password hash format")

// VerifyPassword checks password against a PHC string (constant-time key comparison).
func VerifyPassword(password, encoded string) (bool, error) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 6 || parts[1] != "argon2id" {
		return false, ErrInvalidHash
	}
	var version int
	if _, err := fmt.Sscanf(parts[2], "v=%d", &version); err != nil || version != argon2.Version {
		return false, ErrInvalidHash
	}
	var m, t uint32
	var p uint8
	if _, err := fmt.Sscanf(parts[3], "m=%d,t=%d,p=%d", &m, &t, &p); err != nil {
		return false, ErrInvalidHash
	}
	if m == 0 || m > 1<<20 || t == 0 || t > 20 || p == 0 { // guard against absurd parameters
		return false, ErrInvalidHash
	}
	salt, err := base64.RawStdEncoding.DecodeString(parts[4])
	if err != nil {
		return false, ErrInvalidHash
	}
	want, err := base64.RawStdEncoding.DecodeString(parts[5])
	if err != nil || len(want) == 0 {
		return false, ErrInvalidHash
	}
	got := argon2.IDKey([]byte(password), salt, t, m, p, uint32(len(want)))
	return subtle.ConstantTimeCompare(got, want) == 1, nil
}

// NewToken creates prefix + base64url(32 random bytes).
func NewToken(prefix string) (string, error) {
	b := make([]byte, tokenRandomBytes)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return prefix + base64.RawURLEncoding.EncodeToString(b), nil
}

// HashToken returns the hex-encoded SHA-256 of a token (storage form).
func HashToken(token string) string {
	sum := sha256.Sum256([]byte(token))
	return hex.EncodeToString(sum[:])
}

// EqualHash compares two hash strings in constant time.
func EqualHash(a, b string) bool {
	return subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1
}
