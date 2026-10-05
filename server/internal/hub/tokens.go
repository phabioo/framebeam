package hub

import (
	"crypto/subtle"

	"github.com/phabioo/framebeam/server/internal/auth"
)

// TokenEqual vergleicht zwei Tokens (z. B. CSRF) in konstanter Zeit.
func TokenEqual(a, b string) bool {
	return a != "" && subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1
}

// RandomToken erzeugt ein zufälliges base64url-Token ohne Präfix (z. B. für CSRF).
func RandomToken() (string, error) { return auth.NewToken("") }
