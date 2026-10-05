// Package auth enthält Passwort-Hashing (Argon2id) und Token-Hilfen.
// Tokens werden nur als SHA-256-Hash gespeichert; Vergleiche laufen in konstanter Zeit.
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

// Token-Präfixe (ADR 0002).
const (
	PrefixAccess     = "fba_"
	PrefixDevice     = "fbd_"
	PrefixPoll       = "fbp_"
	tokenRandomBytes = 32
)

// Params sind die Argon2id-Parameter.
type Params struct {
	Time      uint32
	MemoryKiB uint32
	Threads   uint8
	KeyLen    uint32
	SaltLen   uint32
}

// DefaultParams: 64 MiB, 3 Durchläufe.
var DefaultParams = Params{Time: 3, MemoryKiB: 64 * 1024, Threads: 2, KeyLen: 32, SaltLen: 16}

// HashPassword liefert einen PHC-String ($argon2id$v=19$m=..,t=..,p=..$salt$hash).
func HashPassword(password string, p Params) (string, error) {
	salt := make([]byte, p.SaltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key := argon2.IDKey([]byte(password), salt, p.Time, p.MemoryKiB, p.Threads, p.KeyLen)
	return fmt.Sprintf("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s", argon2.Version, p.MemoryKiB, p.Time, p.Threads,
		base64.RawStdEncoding.EncodeToString(salt), base64.RawStdEncoding.EncodeToString(key)), nil
}

// ErrInvalidHash meldet ein nicht lesbares Hash-Format.
var ErrInvalidHash = errors.New("auth: ungültiges Passwort-Hash-Format")

// VerifyPassword prüft password gegen einen PHC-String (konstante Zeit beim Schlüsselvergleich).
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
	if m == 0 || m > 1<<20 || t == 0 || t > 20 || p == 0 { // Schutz vor absurden Parametern
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

// NewToken erzeugt prefix + base64url(32 Zufallsbytes).
func NewToken(prefix string) (string, error) {
	b := make([]byte, tokenRandomBytes)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return prefix + base64.RawURLEncoding.EncodeToString(b), nil
}

// HashToken liefert den hex-kodierten SHA-256 eines Tokens (Speicherform).
func HashToken(token string) string {
	sum := sha256.Sum256([]byte(token))
	return hex.EncodeToString(sum[:])
}

// EqualHash vergleicht zwei Hash-Strings in konstanter Zeit.
func EqualHash(a, b string) bool {
	return subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1
}
