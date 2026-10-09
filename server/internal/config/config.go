// Package config reads the hub configuration from flags and FRAMEBEAM_* environment variables.
package config

import (
	"crypto/ed25519"
	"errors"
	"flag"
	"fmt"
	"net"
	"net/url"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// Config is the runtime configuration of the FrameBeam Hub.
type Config struct {
	goos          string // platform the defaults were chosen for
	requestDirSet bool   // -update-request-dir was given
	DataDir       string
	Listen        string
	// Name is the display name on first start; afterwards the name stored in the database applies.
	Name    string
	TLSCert string
	TLSKey  string
	// Dev allows HTTP instead of HTTPS (development and tests only).
	Dev bool
	// ICEServers are stun: URLs delivered to Players for Sessions (default empty: host candidates suffice on a LAN).
	ICEServers []string
	// CoreBuildbotURL is the libretro buildbot nightly base URL (ADR 0020 D1), https only.
	CoreBuildbotURL string
	// CoreInfoURL is the libretro core info archive (info.zip), https only.
	CoreInfoURL string
	// CoreIndexURL is deprecated and ignored (the signed core index was retired by ADR 0020); a non-empty value
	// only produces a log warning.
	CoreIndexURL string
	// CoreTrustKeys are additional trusted signing keys (base64 Ed25519 public keys) besides the compiled-in ones.
	// They apply to the updates index only (cores are no longer signed).
	CoreTrustKeys []string
	// UpdateIndexURL is the signed updates index (default updates.DefaultIndexURL; https or file://).
	UpdateIndexURL string
	// UpdateRequestDir is where the update request file is created (default /run/framebeam; Windows <data-dir>\update-request).
	UpdateRequestDir string
	// SaveKeepRecent, SaveKeepDaily, SaveKeepWeekly are the save history retention rules (ADR 0012 D7):
	// newest versions kept, days and weeks of which the newest version is kept. 0 = unlimited for that rule.
	SaveKeepRecent, SaveKeepDaily, SaveKeepWeekly int
	// LibraryImportDir is the folder "Rescan folder" imports ROMs from (default <data-dir>/library-import; files are only read).
	LibraryImportDir string
	// TURN switches on the embedded STUN/TURN server (ADR 0012 D2); it needs PublicHost.
	TURN bool
	// PublicHost is the DNS name (or IPv4) of the Hub's public address.
	PublicHost string
	// TURNPort is the UDP and TCP port of the TURN server (default 3478).
	TURNPort int
	// TURNRelayPorts is the UDP relay range "min-max" (default 49160-49199).
	TURNRelayPorts string
	// TURNRelayIP is an optional fixed public IPv4 for relayed addresses.
	TURNRelayIP string
}

// Defaults of the core source (ADR 0020 D1).
const (
	DefaultCoreBuildbotURL = "https://buildbot.libretro.com/nightly"
	DefaultCoreInfoURL     = "https://buildbot.libretro.com/assets/frontend/info.zip"
)

// Defaults of the TURN options.
const (
	DefaultTURNPort       = 3478
	DefaultTURNRelayPorts = "49160-49199"
)

// TURNRelayRange parses TURNRelayPorts.
func (c *Config) TURNRelayRange() (min, max int, err error) {
	a, b, ok := strings.Cut(strings.TrimSpace(c.TURNRelayPorts), "-")
	if ok {
		min, err = strconv.Atoi(strings.TrimSpace(a))
		if err == nil {
			max, err = strconv.Atoi(strings.TrimSpace(b))
		}
	}
	if !ok || err != nil || min < 1 || max > 65535 || min > max {
		return 0, 0, fmt.Errorf("turn-relay-ports: %q is not a range like 49160-49199", c.TURNRelayPorts)
	}
	return min, max, nil
}

// repeatList is a repeatable flag.Value: the first use replaces the environment default, further uses append.
type repeatList struct {
	v       *[]string
	touched *bool
}

func (l repeatList) String() string {
	if l.v == nil {
		return ""
	}
	return strings.Join(*l.v, ",")
}

func (l repeatList) Set(s string) error {
	if !*l.touched {
		*l.v, *l.touched = nil, true
	}
	if s = strings.TrimSpace(s); s != "" {
		*l.v = append(*l.v, s)
	}
	return nil
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

// DefaultDataDir is the built-in data directory: /var/lib/framebeam, on Windows %ProgramData%\FrameBeam\Hub.
func DefaultDataDir(goos string, getenv func(string) string) string {
	if goos != "windows" {
		return "/var/lib/framebeam"
	}
	pd := getenv("ProgramData")
	if pd == "" {
		pd = `C:\ProgramData`
	}
	return filepath.Join(pd, "FrameBeam", "Hub")
}

// DefaultUpdateRequestDir is the built-in update request directory: /run/framebeam (systemd RuntimeDirectory), on
// Windows the directory <data>\update-request that the installer creates (writable for the Hub service account,
// readable for the updater service).
func DefaultUpdateRequestDir(goos, dataDir string) string {
	if goos == "windows" {
		return filepath.Join(dataDir, "update-request")
	}
	return updates.DefaultRequestDir
}

// trackedString is a string flag that remembers whether it was set explicitly.
type trackedString struct {
	p   *string
	set *bool
}

func (t trackedString) String() string {
	if t.p == nil {
		return ""
	}
	return *t.p
}

func (t trackedString) Set(s string) error { *t.p, *t.set = s, true; return nil }

// Register registers the configuration flags on fs. Defaults come from the environment (FRAMEBEAM_*), otherwise built-in defaults.
// An explicitly set flag overrides the environment.
func Register(fs *flag.FlagSet, getenv func(string) string) *Config {
	return register(fs, getenv, runtime.GOOS)
}

func register(fs *flag.FlagSet, getenv func(string) string, goos string) *Config {
	c := &Config{goos: goos}
	env := func(key, def string) string {
		if v := getenv("FRAMEBEAM_" + key); v != "" {
			return v
		}
		return def
	}
	dev, _ := strconv.ParseBool(getenv("FRAMEBEAM_DEV"))
	fs.StringVar(&c.DataDir, "data-dir", env("DATA_DIR", DefaultDataDir(goos, getenv)), "data directory (FRAMEBEAM_DATA_DIR)")
	fs.StringVar(&c.LibraryImportDir, "library-import-dir", env("LIBRARY_IMPORT_DIR", ""), "folder the Library's \"Rescan folder\" imports ROMs from, default <data-dir>/library-import (FRAMEBEAM_LIBRARY_IMPORT_DIR)")
	fs.StringVar(&c.Listen, "listen", env("LISTEN", ":8443"), "listen address (FRAMEBEAM_LISTEN)")
	fs.StringVar(&c.Name, "name", env("NAME", ""), "hub name on first start (FRAMEBEAM_NAME)")
	fs.StringVar(&c.TLSCert, "tls-cert", env("TLS_CERT", ""), "TLS certificate (PEM) (FRAMEBEAM_TLS_CERT)")
	fs.StringVar(&c.TLSKey, "tls-key", env("TLS_KEY", ""), "TLS key (PEM) (FRAMEBEAM_TLS_KEY)")
	fs.BoolVar(&c.Dev, "dev", dev, "development mode: HTTP instead of HTTPS (FRAMEBEAM_DEV)")
	c.ICEServers = splitCSV(getenv("FRAMEBEAM_ICE_SERVERS"))
	fs.Var(csvList{&c.ICEServers}, "ice-servers", "comma-separated stun: URLs for Sessions, default none (FRAMEBEAM_ICE_SERVERS)")
	fs.StringVar(&c.CoreBuildbotURL, "core-buildbot-url", func() string {
		if v := getenv("FRAMEBEAM_HUB_CORE_BUILDBOT_URL"); v != "" {
			return v
		}
		return DefaultCoreBuildbotURL
	}(), "base URL of the libretro buildbot nightly builds, https only (FRAMEBEAM_HUB_CORE_BUILDBOT_URL)")
	fs.StringVar(&c.CoreInfoURL, "core-info-url", func() string {
		if v := getenv("FRAMEBEAM_HUB_CORE_INFO_URL"); v != "" {
			return v
		}
		return DefaultCoreInfoURL
	}(), "URL of the libretro core info archive info.zip, https only (FRAMEBEAM_HUB_CORE_INFO_URL)")
	fs.StringVar(&c.CoreIndexURL, "core-index-url", getenv("FRAMEBEAM_HUB_CORE_INDEX_URL"),
		"deprecated and ignored: the signed core index was retired (ADR 0020), see -core-buildbot-url (FRAMEBEAM_HUB_CORE_INDEX_URL)")
	c.CoreTrustKeys = splitCSV(getenv("FRAMEBEAM_HUB_CORE_TRUST_KEYS"))
	fs.Var(repeatList{&c.CoreTrustKeys, new(bool)}, "core-trust-key",
		"additional trusted signing key for the updates index (cores are not signed), base64 Ed25519 public key; repeatable (FRAMEBEAM_HUB_CORE_TRUST_KEYS, comma-separated)")
	fs.StringVar(&c.UpdateIndexURL, "update-index-url", func() string {
		if v := getenv("FRAMEBEAM_HUB_UPDATE_INDEX_URL"); v != "" {
			return v
		}
		return updates.DefaultIndexURL
	}(), "URL of the signed updates index, https or file:// (FRAMEBEAM_HUB_UPDATE_INDEX_URL)")
	intEnv := func(key string, def int) int {
		if n, err := strconv.Atoi(strings.TrimSpace(getenv("FRAMEBEAM_" + key))); err == nil {
			return n
		}
		return def
	}
	fs.IntVar(&c.SaveKeepRecent, "save-keep-recent", intEnv("SAVE_KEEP_RECENT", 20), "save history: newest versions per slot to keep, 0 = unlimited (FRAMEBEAM_SAVE_KEEP_RECENT)")
	fs.IntVar(&c.SaveKeepDaily, "save-keep-daily", intEnv("SAVE_KEEP_DAILY", 30), "save history: days of which the newest version is kept, 0 = unlimited (FRAMEBEAM_SAVE_KEEP_DAILY)")
	fs.IntVar(&c.SaveKeepWeekly, "save-keep-weekly", intEnv("SAVE_KEEP_WEEKLY", 26), "save history: weeks of which the newest version is kept, 0 = unlimited (FRAMEBEAM_SAVE_KEEP_WEEKLY)")
	c.UpdateRequestDir = func() string {
		if v := getenv("FRAMEBEAM_HUB_UPDATE_REQUEST_DIR"); v != "" {
			return v
		}
		if goos == "windows" {
			return "" // follows -data-dir, resolved in Validate unless set explicitly
		}
		return updates.DefaultRequestDir
	}()
	fs.Var(trackedString{&c.UpdateRequestDir, &c.requestDirSet}, "update-request-dir", "directory for the update request file read by the root helper (FRAMEBEAM_HUB_UPDATE_REQUEST_DIR)")
	turn, _ := strconv.ParseBool(getenv("FRAMEBEAM_TURN"))
	fs.BoolVar(&c.TURN, "turn", turn, "embedded STUN/TURN server for Sessions over the internet, needs -public-host (FRAMEBEAM_TURN)")
	fs.StringVar(&c.PublicHost, "public-host", env("PUBLIC_HOST", ""), "DNS name or IPv4 of the Hub's public address, required with -turn (FRAMEBEAM_PUBLIC_HOST)")
	turnPort := DefaultTURNPort
	if v, err := strconv.Atoi(getenv("FRAMEBEAM_TURN_PORT")); err == nil {
		turnPort = v
	}
	fs.IntVar(&c.TURNPort, "turn-port", turnPort, "TURN UDP and TCP port (FRAMEBEAM_TURN_PORT)")
	fs.StringVar(&c.TURNRelayPorts, "turn-relay-ports", env("TURN_RELAY_PORTS", DefaultTURNRelayPorts), "TURN UDP relay port range min-max (FRAMEBEAM_TURN_RELAY_PORTS)")
	fs.StringVar(&c.TURNRelayIP, "turn-relay-ip", env("TURN_RELAY_IP", ""), "fixed public IPv4 for relayed addresses instead of resolving -public-host (FRAMEBEAM_TURN_RELAY_IP)")
	return c
}

// ImportDir returns the library import folder: the configured one, else <data-dir>/library-import.
func (c *Config) ImportDir() string {
	if c.LibraryImportDir != "" {
		return c.LibraryImportDir
	}
	return filepath.Join(c.DataDir, "library-import")
}

// TrustedCoreKeys returns the compiled-in trusted keys plus the configured ones (updates index).
func (c *Config) TrustedCoreKeys() ([]ed25519.PublicKey, error) {
	return corepkg.TrustedKeys(c.CoreTrustKeys)
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
	if c.TURN {
		if c.PublicHost == "" {
			return errors.New("-turn needs -public-host")
		}
		if strings.ContainsAny(c.PublicHost, "/ ") {
			return errors.New("public-host must be a host name or IP address")
		}
		if c.TURNPort < 1 || c.TURNPort > 65535 {
			return errors.New("turn-port must be between 1 and 65535")
		}
		if _, _, err := c.TURNRelayRange(); err != nil {
			return err
		}
		if c.TURNRelayIP != "" {
			if ip := net.ParseIP(c.TURNRelayIP); ip == nil || ip.To4() == nil {
				return fmt.Errorf("turn-relay-ip: %q is not an IPv4 address", c.TURNRelayIP)
			}
		}
	}
	for name, v := range map[string]string{"core-buildbot-url": c.CoreBuildbotURL, "core-info-url": c.CoreInfoURL} {
		if u, err := url.Parse(v); err != nil || u.Scheme != "https" || u.Host == "" {
			return fmt.Errorf("%s must be an https URL", name)
		}
	}
	if err := updates.ValidateIndexURL(c.UpdateIndexURL); err != nil {
		return fmt.Errorf("update-index-url: %w", err)
	}
	if c.SaveKeepRecent < 0 || c.SaveKeepDaily < 0 || c.SaveKeepWeekly < 0 {
		return errors.New("save-keep-recent, save-keep-daily and save-keep-weekly must not be negative")
	}
	if c.UpdateRequestDir == "" && !c.requestDirSet {
		c.UpdateRequestDir = DefaultUpdateRequestDir(c.goos, c.DataDir)
	}
	if c.UpdateRequestDir == "" {
		return errors.New("update-request-dir must not be empty")
	}
	if _, err := c.TrustedCoreKeys(); err != nil {
		return fmt.Errorf("core-trust-key: %w", err)
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
