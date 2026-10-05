package hub

import (
	"crypto/subtle"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// TokenEqual compares two tokens (e.g. CSRF) in constant time.
func TokenEqual(a, b string) bool {
	return a != "" && subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1
}

// RandomToken creates a random base64url token without a prefix (e.g. for CSRF).
func RandomToken() (string, error) { return auth.NewToken("") }
