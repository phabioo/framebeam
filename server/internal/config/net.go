package config

import (
	"encoding/json"
	"fmt"
	"net"
	"strconv"
	"strings"
)

// Network settings that can be changed on the web interface (Settings > Network). A value saved there wins over
// hub.env and flags, which only provide the initial value. The Hub stores each value as a raw string under one of
// these keys; this file knows how to apply, compare and validate them.

// Keys of the network settings.
const (
	NetListenPort = "listen_port"
	NetPublicHost = "public_host"
	NetTURN       = "turn"
	NetTURNPort   = "turn_port"
	NetRelayPorts = "turn_relay_ports"
	NetRelayIP    = "turn_relay_ip"
	NetICEServers = "ice_servers"
	NetKeepRecent = "save_keep_recent"
	NetKeepDaily  = "save_keep_daily"
	NetKeepWeekly = "save_keep_weekly"
)

// NetKeys lists all network settings in display order.
var NetKeys = []string{NetListenPort, NetPublicHost, NetTURN, NetTURNPort, NetRelayPorts, NetRelayIP, NetICEServers,
	NetKeepRecent, NetKeepDaily, NetKeepWeekly}

// NetNeedsRestart reports whether a change of the setting only takes effect after a restart of the Hub.
// Retention and external STUN servers apply live.
func NetNeedsRestart(key string) bool {
	switch key {
	case NetKeepRecent, NetKeepDaily, NetKeepWeekly, NetICEServers:
		return false
	}
	return true
}

// NetLabel is the human readable name of a setting (used in the "Restart required" card).
func NetLabel(key string) string {
	switch key {
	case NetListenPort:
		return "Listen port"
	case NetPublicHost:
		return "Public host"
	case NetTURN:
		return "Built-in TURN relay"
	case NetTURNPort:
		return "TURN port"
	case NetRelayPorts:
		return "Relay port range"
	case NetRelayIP:
		return "Relay IP"
	case NetICEServers:
		return "External STUN servers"
	case NetKeepRecent:
		return "Keep newest versions"
	case NetKeepDaily:
		return "Keep daily versions"
	case NetKeepWeekly:
		return "Keep weekly versions"
	}
	return key
}

// Port limits for values entered on the web interface. Ports below 1024 need a capability that the systemd unit
// only gets through the drop-in of install-hub.sh --port.
const (
	MinWebPort        = 1024
	MaxWebPort        = 65535
	MaxRelayPorts     = 1000
	maxICEServers     = 10
	maxRetentionValue = 100000
)

// DefaultListenPort is used when the listen address carries no usable port.
const DefaultListenPort = 8443

// ListenPort returns the port of the listen address (DefaultListenPort if it has none).
func (c *Config) ListenPort() int {
	if _, p, err := net.SplitHostPort(c.Listen); err == nil {
		if n, err := strconv.Atoi(p); err == nil && n > 0 {
			return n
		}
	}
	return DefaultListenPort
}

// ListenWithPort returns listen with its port replaced; the host part is kept (":8443" -> ":9000").
func ListenWithPort(listen string, port int) string {
	host, _, err := net.SplitHostPort(listen)
	if err != nil {
		host = ""
	}
	return net.JoinHostPort(host, strconv.Itoa(port))
}

// NetValue returns the raw string form of a setting as stored and compared.
func (c *Config) NetValue(key string) string {
	switch key {
	case NetListenPort:
		return strconv.Itoa(c.ListenPort())
	case NetPublicHost:
		return c.PublicHost
	case NetTURN:
		return strconv.FormatBool(c.TURN)
	case NetTURNPort:
		return strconv.Itoa(c.TURNPort)
	case NetRelayPorts:
		return strings.TrimSpace(c.TURNRelayPorts)
	case NetRelayIP:
		return c.TURNRelayIP
	case NetICEServers:
		return EncodeICEServers(c.ICEServers)
	case NetKeepRecent:
		return strconv.Itoa(c.SaveKeepRecent)
	case NetKeepDaily:
		return strconv.Itoa(c.SaveKeepDaily)
	case NetKeepWeekly:
		return strconv.Itoa(c.SaveKeepWeekly)
	}
	return ""
}

// EncodeICEServers is the stored form of the external STUN server list (a JSON array).
func EncodeICEServers(urls []string) string {
	if urls == nil {
		urls = []string{}
	}
	b, _ := json.Marshal(urls)
	return string(b)
}

// DecodeICEServers parses the stored form.
func DecodeICEServers(raw string) ([]string, error) {
	var out []string
	if err := json.Unmarshal([]byte(raw), &out); err != nil {
		return nil, fmt.Errorf("external STUN servers: %w", err)
	}
	return out, nil
}

// SetNet parses a raw value and stores it in the config. It does not validate cross-field rules (ValidateNet).
func (c *Config) SetNet(key, raw string) error {
	switch key {
	case NetListenPort:
		n, err := strconv.Atoi(strings.TrimSpace(raw))
		if err != nil || n < 1 || n > 65535 {
			return netErr("The listen port %q is not a port number", raw)
		}
		c.Listen = ListenWithPort(c.Listen, n)
	case NetPublicHost:
		c.PublicHost = strings.TrimSpace(raw)
	case NetTURN:
		b, err := strconv.ParseBool(raw)
		if err != nil {
			return netErr("%q is not on or off", raw)
		}
		c.TURN = b
	case NetTURNPort:
		n, err := strconv.Atoi(strings.TrimSpace(raw))
		if err != nil || n < 1 || n > 65535 {
			return netErr("The TURN port %q is not a port number", raw)
		}
		c.TURNPort = n
	case NetRelayPorts:
		old := c.TURNRelayPorts
		c.TURNRelayPorts = strings.TrimSpace(raw)
		if _, _, err := c.TURNRelayRange(); err != nil {
			c.TURNRelayPorts = old
			return netErr("The relay range must look like 49160-49199, first port not above the last")
		}
	case NetRelayIP:
		c.TURNRelayIP = strings.TrimSpace(raw)
	case NetICEServers:
		l, err := DecodeICEServers(raw)
		if err != nil {
			return err
		}
		c.ICEServers = l
	case NetKeepRecent, NetKeepDaily, NetKeepWeekly:
		n, err := strconv.Atoi(strings.TrimSpace(raw))
		if err != nil || n < 0 {
			return netErr("%q is not a number of 0 or more", raw)
		}
		switch key {
		case NetKeepRecent:
			c.SaveKeepRecent = n
		case NetKeepDaily:
			c.SaveKeepDaily = n
		default:
			c.SaveKeepWeekly = n
		}
	default:
		return fmt.Errorf("unknown network setting %q", key)
	}
	return nil
}

// ApplyNet applies stored overrides (key -> raw value) to the config. Unknown keys are ignored; a value that does
// not parse is skipped and reported in the returned map (key -> error), the other overrides still apply.
func (c *Config) ApplyNet(vals map[string]string) map[string]error {
	var bad map[string]error
	for _, k := range NetKeys {
		raw, ok := vals[k]
		if !ok {
			continue
		}
		if err := c.SetNet(k, raw); err != nil {
			if bad == nil {
				bad = map[string]error{}
			}
			bad[k] = err
		}
	}
	return bad
}

// ValidateNet checks the change of one setting that is already applied to c (the candidate configuration). It
// validates the value itself and the rules that involve it; settings that were not touched are not re-checked, so
// an unusual value from hub.env (for example a privileged listen port) never blocks unrelated changes. The error
// text is meant for the web interface.
func (c *Config) ValidateNet(key string) error {
	listen := c.ListenPort()
	switch key {
	case NetListenPort:
		if err := webPort("Listen port", listen); err != nil {
			return err
		}
		if c.TURN && listen == c.TURNPort {
			return netErr("The listen port must differ from the TURN port (%d)", c.TURNPort)
		}
		return c.checkRelayVsPorts()
	case NetPublicHost:
		if err := checkHost(c.PublicHost); err != nil {
			return err
		}
		if c.TURN && c.PublicHost == "" {
			return errNeedsPublicHost
		}
	case NetTURN:
		if c.TURN {
			if c.PublicHost == "" {
				return errNeedsPublicHost
			}
			if err := c.ValidateNet(NetTURNPort); err != nil {
				return err
			}
			if err := c.ValidateNet(NetRelayPorts); err != nil {
				return err
			}
		}
	case NetTURNPort:
		if err := webPort("TURN port", c.TURNPort); err != nil {
			return err
		}
		if c.TURNPort == listen {
			return netErr("The TURN port must differ from the listen port (%d)", listen)
		}
		return c.checkRelayVsPorts()
	case NetRelayPorts:
		lo, hi, err := c.TURNRelayRange()
		if err != nil {
			return netErr("The relay range must look like 49160-49199")
		}
		if lo < MinWebPort || hi > MaxWebPort {
			return netErr("Relay ports must be between %d and %d", MinWebPort, MaxWebPort)
		}
		if hi-lo+1 > MaxRelayPorts {
			return netErr("The relay range may contain at most %d ports", MaxRelayPorts)
		}
		return c.checkRelayVsPorts()
	case NetRelayIP:
		if c.TURNRelayIP != "" {
			if ip := net.ParseIP(c.TURNRelayIP); ip == nil || ip.To4() == nil {
				return netErr("The relay IP must be an IPv4 address like 203.0.113.10")
			}
		}
	case NetICEServers:
		if len(c.ICEServers) > maxICEServers {
			return netErr("At most %d external servers", maxICEServers)
		}
		for _, u := range c.ICEServers {
			if err := ValidateSTUNURL(u); err != nil {
				return err
			}
		}
	case NetKeepRecent, NetKeepDaily, NetKeepWeekly:
		for _, n := range []int{c.SaveKeepRecent, c.SaveKeepDaily, c.SaveKeepWeekly} {
			if n < 0 || n > maxRetentionValue {
				return netErr("Retention values must be between 0 and %d", maxRetentionValue)
			}
		}
	default:
		return netErr("Unknown network setting")
	}
	return nil
}

var errNeedsPublicHost = netErr("The built-in TURN relay needs a public host. Enter the host name first")

// checkRelayVsPorts: the relay range must contain neither the TURN port nor the listen port.
func (c *Config) checkRelayVsPorts() error {
	lo, hi, err := c.TURNRelayRange()
	if err != nil {
		return nil // reported when the range itself is validated
	}
	if p := c.ListenPort(); p >= lo && p <= hi {
		return netErr("The relay range %d-%d must not contain the listen port %d", lo, hi, p)
	}
	if p := c.TURNPort; p >= lo && p <= hi {
		return netErr("The relay range %d-%d must not contain the TURN port %d", lo, hi, p)
	}
	return nil
}

func webPort(name string, p int) error {
	if p < MinWebPort {
		return netErr("%s: ports below %d are not allowed here because they need extra privileges. Use install-hub.sh --port to run the hub on a low port", name, MinWebPort)
	}
	if p > MaxWebPort {
		return netErr("%s must be between %d and %d", name, MinWebPort, MaxWebPort)
	}
	return nil
}

func checkHost(h string) error {
	if strings.ContainsAny(h, "/ :@?#") || len(h) > 253 {
		return netErr("The public host must be a host name or IPv4 address without port or scheme, like hub.example.com")
	}
	return nil
}

// ValidateSTUNURL checks one external STUN server URL.
func ValidateSTUNURL(u string) error {
	if !strings.HasPrefix(u, "stun:") || len(u) == len("stun:") || len(u) > 200 || strings.ContainsAny(u, " \t\r\n") {
		return netErr("%q is not a stun: URL like stun:stun.example.com:3478", u)
	}
	return nil
}

// TestBind opens and closes a TCP listener on addr: it reports whether the Hub could listen there.
func TestBind(addr string) error {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	return ln.Close()
}

// NetError is a validation error whose text is shown to the admin on the web interface as it is.
type NetError struct{ Msg string }

func (e *NetError) Error() string { return e.Msg }

func netErr(format string, a ...any) error { return &NetError{Msg: fmt.Sprintf(format, a...)} }
