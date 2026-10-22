package config

import (
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// External TURN servers (-turn-external). One flag value describes one server as semicolon-separated key=value
// fields:
//
//	name=<label>          optional, default external-<n>
//	mode=secret|static    credential mode (default secret)
//	urls=<url>,<url>      turn: and turns: URLs, optional ?transport=udp|tcp
//	username=<user>       static mode only
//	secret-file=<path>    secret mode: file holding the shared secret (alternative: UZUME_TURN_EXTERNAL_<n>_SECRET)
//	password-file=<path>  static mode: file holding the password (alternative: UZUME_TURN_EXTERNAL_<n>_PASSWORD)
//
// <n> is the 1-based position of the server. The flag can be repeated; in UZUME_TURN_EXTERNAL the servers are
// separated by newlines. Secrets and passwords are never part of the flag value, the Config or any log.

// External TURN credential modes.
const (
	ExternalModeSecret = "secret"
	ExternalModeStatic = "static"
)

// ExternalTURNSpec is one parsed -turn-external value. It holds no secret, only where to read it from.
type ExternalTURNSpec struct {
	Name         string
	Mode         string
	URLs         []string
	Username     string // static
	SecretFile   string // secret
	PasswordFile string // static
}

// externalList is the repeatable -turn-external flag. The environment provides the initial value; the first
// explicit flag replaces it.
type externalList struct {
	v   *[]string
	set bool
}

func (l *externalList) String() string {
	if l == nil || l.v == nil {
		return ""
	}
	return strings.Join(*l.v, "\n")
}

func (l *externalList) Set(s string) error {
	if !l.set {
		*l.v, l.set = nil, true
	}
	*l.v = append(*l.v, s)
	return nil
}

func splitLines(s string) []string {
	var out []string
	for _, p := range strings.Split(s, "\n") {
		if p = strings.TrimSpace(p); p != "" {
			out = append(out, p)
		}
	}
	return out
}

// ExternalTURNSpecs parses the configured -turn-external values.
func (c *Config) ExternalTURNSpecs() ([]ExternalTURNSpec, error) {
	var out []ExternalTURNSpec
	names := map[string]bool{}
	for i, raw := range c.TURNExternal {
		sp, err := parseExternalTURN(raw, i+1)
		if err != nil {
			return nil, fmt.Errorf("turn-external #%d: %w", i+1, err)
		}
		if names[sp.Name] {
			return nil, fmt.Errorf("turn-external #%d: name %q is used twice", i+1, sp.Name)
		}
		names[sp.Name] = true
		out = append(out, sp)
	}
	return out, nil
}

func parseExternalTURN(raw string, n int) (ExternalTURNSpec, error) {
	sp := ExternalTURNSpec{Name: "external-" + strconv.Itoa(n), Mode: ExternalModeSecret}
	for _, f := range strings.Split(raw, ";") {
		f = strings.TrimSpace(f)
		if f == "" {
			continue
		}
		k, v, ok := strings.Cut(f, "=")
		k, v = strings.ToLower(strings.TrimSpace(k)), strings.TrimSpace(v)
		if !ok || v == "" {
			return sp, fmt.Errorf("field %q needs the form key=value", k)
		}
		switch k {
		case "name":
			if err := ValidateExternalTURNName(v); err != nil {
				return sp, err
			}
			sp.Name = v
		case "mode":
			sp.Mode = strings.ToLower(v)
		case "urls":
			sp.URLs = splitCSV(v)
		case "username":
			sp.Username = v
		case "secret-file":
			sp.SecretFile = v
		case "password-file":
			sp.PasswordFile = v
		default:
			return sp, fmt.Errorf("unknown field %q (name, mode, urls, username, secret-file, password-file)", k)
		}
	}
	if len(sp.URLs) == 0 {
		return sp, errors.New("urls is missing")
	}
	for _, u := range sp.URLs {
		if err := checkTURNURL(u); err != nil {
			return sp, err
		}
	}
	switch sp.Mode {
	case ExternalModeSecret:
		if sp.Username != "" || sp.PasswordFile != "" {
			return sp, errors.New("mode=secret takes secret-file, not username or password-file")
		}
	case ExternalModeStatic:
		if sp.Username == "" {
			return sp, errors.New("mode=static needs username")
		}
		if sp.SecretFile != "" {
			return sp, errors.New("mode=static takes password-file, not secret-file")
		}
	default:
		return sp, fmt.Errorf("mode %q is not secret or static", sp.Mode)
	}
	return sp, nil
}

// ValidateExternalTURNName checks the label of an external TURN server (flag and web interface).
func ValidateExternalTURNName(v string) error {
	if v == "" || len(v) > 64 {
		return errors.New("name must be 1 to 64 characters")
	}
	for _, r := range v {
		if (r < 'a' || r > 'z') && (r < 'A' || r > 'Z') && (r < '0' || r > '9') && r != '-' && r != '_' && r != '.' {
			return errors.New("name may only contain letters, digits, '-', '_' and '.'")
		}
	}
	return nil
}

// ValidateTURNURL checks one external TURN URL (flag and web interface).
func ValidateTURNURL(u string) error { return checkTURNURL(u) }

// checkTURNURL accepts turn:/turns: host[:port] with an optional ?transport=udp|tcp.
func checkTURNURL(u string) error {
	rest, ok := strings.CutPrefix(u, "turn:")
	if !ok {
		rest, ok = strings.CutPrefix(u, "turns:")
	}
	if !ok {
		return fmt.Errorf("%q is not a turn: or turns: URL", u)
	}
	hostport, query, hasQuery := strings.Cut(rest, "?")
	if hostport == "" || strings.ContainsAny(hostport, " \t/@\\#") || strings.Contains(rest, "..") {
		return fmt.Errorf("%q is not a valid TURN URL", u)
	}
	if hasQuery && query != "transport=udp" && query != "transport=tcp" {
		return fmt.Errorf("%q: only ?transport=udp or ?transport=tcp is allowed", u)
	}
	return nil
}

// ExternalTURNSecret returns the shared secret (mode secret) or password (mode static) of server n (1-based): the
// environment variable UZUME_TURN_EXTERNAL_<n>_SECRET or _PASSWORD wins over the file. Errors never contain
// the value.
func (c *Config) ExternalTURNSecret(sp ExternalTURNSpec, n int, getenv func(string) string) (string, error) {
	kind, file := "SECRET", sp.SecretFile
	if sp.Mode == ExternalModeStatic {
		kind, file = "PASSWORD", sp.PasswordFile
	}
	envName := "UZUME_TURN_EXTERNAL_" + strconv.Itoa(n) + "_" + kind
	if v := strings.TrimSpace(getenv(envName)); v != "" {
		return v, nil
	}
	if file == "" {
		return "", fmt.Errorf("external TURN %q needs its %s in %s or a %s-file", sp.Name, strings.ToLower(kind), envName, strings.ToLower(kind))
	}
	b, err := os.ReadFile(file)
	if err != nil {
		return "", fmt.Errorf("external TURN %q: %s-file: %w", sp.Name, strings.ToLower(kind), err)
	}
	if v := strings.TrimSpace(string(b)); v != "" {
		return v, nil
	}
	return "", fmt.Errorf("external TURN %q: %s-file is empty", sp.Name, strings.ToLower(kind))
}
