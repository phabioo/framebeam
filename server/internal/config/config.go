// Package config reads the hub configuration from flags and FRAMEBEAM_* environment variables.
package config

import (
	"errors"
	"flag"
	"fmt"
	"net"
	"strconv"
	"strings"
)

// Config is the runtime configuration of the FrameBeam Hub.
type Config struct {
	DataDir string
	Listen  string
	// Name is the display name on first start; afterwards the name stored in the database applies.
	Name    string
	TLSCert string
	TLSKey  string
	// Dev allows HTTP instead of HTTPS (development and tests only).
	Dev bool
	// ICEServers are stun: URLs delivered to Players for Sessions (default empty: host candidates suffice on a LAN).
	ICEServers []string
}

// csvList is a flag.Value for comma-separated lists.
type csvList struct{ v *[]string }

func (l csvList) String() string {
	if l.v == nil {
		return ""
	}
	return strings.Join(*l.v, ",")
}

func (l csvList) Set(s string) error { *l.v = splitCSV(s); return nil }

func splitCSV(s string) []string {
	var out []string
	for _, p := range strings.Split(s, ",") {
		if p = strings.TrimSpace(p); p != "" {
			out = append(out, p)
		}
	}
	return out
}

// Register registers the configuration flags on fs. Defaults come from the environment (FRAMEBEAM_*), otherwise built-in defaults.
// An explicitly set flag overrides the environment.
func Register(fs *flag.FlagSet, getenv func(string) string) *Config {
	c := &Config{}
	env := func(key, def string) string {
		if v := getenv("FRAMEBEAM_" + key); v != "" {
			return v
		}
		return def
	}
	dev, _ := strconv.ParseBool(getenv("FRAMEBEAM_DEV"))
	fs.StringVar(&c.DataDir, "data-dir", env("DATA_DIR", "/var/lib/framebeam"), "data directory (FRAMEBEAM_DATA_DIR)")
	fs.StringVar(&c.Listen, "listen", env("LISTEN", ":8443"), "listen address (FRAMEBEAM_LISTEN)")
	fs.StringVar(&c.Name, "name", env("NAME", ""), "hub name on first start (FRAMEBEAM_NAME)")
	fs.StringVar(&c.TLSCert, "tls-cert", env("TLS_CERT", ""), "TLS certificate (PEM) (FRAMEBEAM_TLS_CERT)")
	fs.StringVar(&c.TLSKey, "tls-key", env("TLS_KEY", ""), "TLS key (PEM) (FRAMEBEAM_TLS_KEY)")
	fs.BoolVar(&c.Dev, "dev", dev, "development mode: HTTP instead of HTTPS (FRAMEBEAM_DEV)")
	c.ICEServers = splitCSV(getenv("FRAMEBEAM_ICE_SERVERS"))
	fs.Var(csvList{&c.ICEServers}, "ice-servers", "comma-separated stun: URLs for Sessions, default none (FRAMEBEAM_ICE_SERVERS)")
	return c
}

// Validate checks the configuration for contradictions.
func (c *Config) Validate() error {
	if c.DataDir == "" {
		return errors.New("data-dir must not be empty")
	}
	if (c.TLSCert == "") != (c.TLSKey == "") {
		return errors.New("specify tls-cert and tls-key together or not at all")
	}
	if c.Dev && c.TLSCert != "" {
		return errors.New("-dev (HTTP) and tls-cert/tls-key are mutually exclusive")
	}
	for _, u := range c.ICEServers {
		if !strings.HasPrefix(u, "stun:") || len(u) == len("stun:") {
			return fmt.Errorf("ice-servers: %q is not a stun: URL", u)
		}
	}
	return nil
}

// UseTLS reports whether the hub serves HTTPS. HTTP is available only with -dev.
func (c *Config) UseTLS() bool { return !c.Dev }

// ListenIsLoopback reports whether the listen address binds to loopback only.
func (c *Config) ListenIsLoopback() bool {
	host, _, err := net.SplitHostPort(c.Listen)
	if err != nil || host == "" {
		return false
	}
	if host == "localhost" {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}
