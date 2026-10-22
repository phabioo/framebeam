package diag

import "github.com/phabioo/framebeam/server/internal/redact"

// Redact removes secrets from text: TURN credentials and secrets, tokens, passwords, invite codes, Authorization
// headers and session cookies. Host names and IP addresses stay. The rules live in internal/redact.
func Redact(b []byte) []byte { return redact.Redact(b) }

// RedactString is Redact for strings.
func RedactString(s string) string { return redact.RedactString(s) }
