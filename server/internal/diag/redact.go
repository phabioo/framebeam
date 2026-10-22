// Package diag builds the diagnostics bundle: a zip with the Hub's logs and a hub-info.json, with secrets removed.
// Private IP addresses and host names are kept on purpose (they are needed for debugging connections).
package diag

import "regexp"

const redacted = "[redacted]"

// sensitiveKey matches key names whose value is a secret (case-insensitive, also as part of a longer name like
// turn_secret or access_token). "username" and "credential" are the TURN long-term credential pair.
const sensitiveKey = `(?:password|passwd|passphrase|secret|token|credential|username|api[_-]?key|invite(?:[_-]?code)?|` +
	`session[_-]?id|csrf|signing[_-]?key|private[_-]?key|cookie|nonce)`

var redactors = []struct {
	re   *regexp.Regexp
	repl string
}{
	// Authorization / Cookie style headers: everything up to the end of the line.
	{regexp.MustCompile(`(?i)\b((?:proxy-)?authorization|set-cookie|cookie)\b(["']?\s*[:=]\s*)[^\r\n]*`), "${1}${2}" + redacted},
	// "Bearer <token>" anywhere (log messages, error texts).
	{regexp.MustCompile(`(?i)\b(bearer)\s+[A-Za-z0-9._~+/=-]{8,}`), "${1} " + redacted},
	// key=value, key: value, "key":"value" with a sensitive key name; quoted or bare value.
	{regexp.MustCompile(`(?i)(\b[a-z0-9_.-]*` + sensitiveKey + `[a-z0-9_.-]*["']?\s*[:=]\s*)("(?:[^"\\]|\\.)*"|'[^']*'|[^\s,;&"'}\]]+)`), "${1}" + redacted},
	// A bare "code" key (invite and pairing codes); longer names like status_code are not touched.
	{regexp.MustCompile(`(?i)((?:^|[?&\s"'])code["']?\s*[:=]\s*)("(?:[^"\\]|\\.)*"|'[^']*'|[^\s,;&"'}\]]+)`), "${1}" + redacted},
	// Hub cookie names and token prefixes (fba_ access, fbd_ device, fbp_ poll, fbw_ web session, fbm_ metrics).
	{regexp.MustCompile(`((?:__Host-)?fb_(?:session|csrf))=[^\s;,"']+`), "${1}=" + redacted},
	{regexp.MustCompile(`\bfb[adpwm]_[A-Za-z0-9_-]{8,}`), redacted},
	// Invite links carry the code in the URL fragment.
	{regexp.MustCompile(`(/invite#)[^\s"'<>]+`), "${1}" + redacted},
	// user:password@host in URLs and turn(s):user:password@host.
	{regexp.MustCompile(`(?i)(://[^/\s:@]+:)[^@\s/]+@`), "${1}" + redacted + "@"},
	{regexp.MustCompile(`(?i)\b(turns?:)[^:@\s?/]+:[^@\s?/]+@`), "${1}" + redacted + "@"},
}

// Redact removes secrets from text: TURN credentials and secrets, tokens, passwords, invite codes, Authorization
// headers and session cookies. Host names and IP addresses stay.
func Redact(b []byte) []byte {
	for _, r := range redactors {
		b = r.re.ReplaceAll(b, []byte(r.repl))
	}
	return b
}

// RedactString is Redact for strings.
func RedactString(s string) string { return string(Redact([]byte(s))) }
