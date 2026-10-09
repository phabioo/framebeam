package turnsrv

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/netip"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/turn/v4"
)

// DefaultResolveInterval is how often the public host's A record is re-resolved (DynDNS).
const DefaultResolveInterval = 5 * time.Minute

// MaxAllocationsPerDevice is the number of concurrent relay allocations one device may hold.
const MaxAllocationsPerDevice = 10

// MaxTCPConns is the number of TURN TCP connections open at once; further ones are closed right after Accept.
const MaxTCPConns = 64

// Config configures the embedded TURN server.
type Config struct {
	PublicHost string
	Port       int // UDP and TCP; 0 picks a free UDP port (tests)
	RelayMin   int
	RelayMax   int
	// RelayIP is an optional fixed public IPv4; otherwise PublicHost is resolved.
	RelayIP net.IP
	// Secret is the credential secret (32 random bytes).
	Secret []byte
	// DeviceOK reports whether the device is known and not revoked.
	DeviceOK func(ctx context.Context, deviceID string) bool
	// Resolver returns the IPs of host (default: system resolver). Only IPv4 results are used.
	Resolver func(ctx context.Context, host string) ([]net.IP, error)
	Now      func() time.Time
	// ResolveInterval defaults to DefaultResolveInterval.
	ResolveInterval time.Duration
	// ListenHost restricts the listeners (default: all addresses; tests: 127.0.0.1).
	ListenHost string
	// LANPeerOK reports whether an internal (private) peer IP may be relayed to, i.e. it is the remote IP of a
	// connected Player. nil: no internal peers are allowed.
	LANPeerOK func(ip netip.Addr) bool
	Log       *slog.Logger
}

// Status is the state shown on the Settings page.
type Status struct {
	PublicHost  string
	Port        int
	RelayMin    int
	RelayMax    int
	RelayIP     string // empty: not resolved
	ResolvedAt  time.Time
	Fixed       bool // RelayIP configured, no DNS
	Allocations int
}

// Server is the running STUN/TURN server.
type Server struct {
	cfg  Config
	log  *slog.Logger
	now  func() time.Time
	port int

	relay atomic.Pointer[net.IP]

	allocMu sync.Mutex
	allocs  map[string]int // device ID -> live allocations

	logMu      sync.Mutex
	refusedLog map[netip.Addr]time.Time

	mu         sync.Mutex
	resolvedAt time.Time
	lastState  string

	srv  *turn.Server
	stop context.CancelFunc
	wg   sync.WaitGroup
}

// Validate checks the static configuration.
func (c *Config) validate() error {
	if c.PublicHost == "" && c.RelayIP == nil {
		return errors.New("turnsrv: public host is required")
	}
	if c.Port < 0 || c.Port > 65535 {
		return errors.New("turnsrv: invalid port")
	}
	if c.RelayMin < 1 || c.RelayMax > 65535 || c.RelayMin > c.RelayMax {
		return errors.New("turnsrv: invalid relay port range")
	}
	if len(c.Secret) < 16 {
		return errors.New("turnsrv: secret too short")
	}
	if c.DeviceOK == nil {
		return errors.New("turnsrv: DeviceOK is required")
	}
	return nil
}

// Start resolves the relay address once and starts the UDP and TCP listeners. Close stops everything.
func Start(ctx context.Context, cfg Config) (*Server, error) {
	if err := cfg.validate(); err != nil {
		return nil, err
	}
	s := &Server{cfg: cfg, log: cfg.Log, now: cfg.Now, allocs: map[string]int{}, refusedLog: map[netip.Addr]time.Time{}}
	if s.log == nil {
		s.log = slog.Default()
	}
	if s.now == nil {
		s.now = time.Now
	}
	if cfg.Resolver == nil {
		s.cfg.Resolver = func(ctx context.Context, host string) ([]net.IP, error) {
			return net.DefaultResolver.LookupIP(ctx, "ip4", host)
		}
	}
	if s.cfg.ResolveInterval <= 0 {
		s.cfg.ResolveInterval = DefaultResolveInterval
	}
	if ip := cfg.RelayIP.To4(); ip != nil {
		s.setRelay(ip)
	} else {
		s.Resolve(ctx)
	}

	host := cfg.ListenHost
	pc, err := net.ListenPacket("udp", net.JoinHostPort(host, strconv.Itoa(cfg.Port)))
	if err != nil {
		return nil, fmt.Errorf("turn udp listener: %w", err)
	}
	s.port = pc.LocalAddr().(*net.UDPAddr).Port
	ln, err := net.Listen("tcp", net.JoinHostPort(host, strconv.Itoa(s.port)))
	if err != nil {
		pc.Close()
		return nil, fmt.Errorf("turn tcp listener: %w", err)
	}
	ln = &limitListener{Listener: ln, sem: make(chan struct{}, MaxTCPConns)}
	gen := &relayGen{s: s}
	lf := logging.NewDefaultLoggerFactory()
	lf.DefaultLogLevel = logging.LogLevelError
	srv, err := turn.NewServer(turn.ServerConfig{
		Realm:        Realm,
		AuthHandler:  s.auth,
		QuotaHandler: s.quota,
		EventHandler: turn.EventHandler{
			OnAllocationCreated: func(_, _ net.Addr, _, username, _ string, _ net.Addr, _ int) { s.trackAlloc(username, 1) },
			OnAllocationDeleted: func(_, _ net.Addr, _, username, _ string) { s.trackAlloc(username, -1) },
		},
		LoggerFactory:     lf,
		PacketConnConfigs: []turn.PacketConnConfig{{PacketConn: pc, RelayAddressGenerator: gen, PermissionHandler: s.permission}},
		ListenerConfigs:   []turn.ListenerConfig{{Listener: ln, RelayAddressGenerator: gen, PermissionHandler: s.permission}},
	})
	if err != nil {
		pc.Close()
		ln.Close()
		return nil, err
	}
	s.srv = srv
	if cfg.RelayIP == nil {
		rctx, cancel := context.WithCancel(context.Background())
		s.stop = cancel
		s.wg.Add(1)
		go s.resolveLoop(rctx)
	}
	return s, nil
}

// Port is the listening port (UDP and TCP).
func (s *Server) Port() int { return s.port }

// Close stops the server and the resolver loop.
func (s *Server) Close() error {
	if s.stop != nil {
		s.stop()
	}
	s.wg.Wait()
	return s.srv.Close()
}

// PublicHost is the configured public host.
func (s *Server) PublicHost() string { return s.cfg.PublicHost }

// Status returns the current state.
func (s *Server) Status() Status {
	st := Status{PublicHost: s.cfg.PublicHost, Port: s.port, RelayMin: s.cfg.RelayMin, RelayMax: s.cfg.RelayMax,
		Fixed: s.cfg.RelayIP != nil, Allocations: s.srv.AllocationCount()}
	if ip := s.relayIP(); ip != nil {
		st.RelayIP = ip.String()
	}
	s.mu.Lock()
	st.ResolvedAt = s.resolvedAt
	s.mu.Unlock()
	return st
}

// STUNURL is the stun: URL for a request host.
func (s *Server) STUNURL(reqHost string) string {
	return STUNURL(HostFor(reqHost, s.cfg.PublicHost), s.port)
}

// Credentials issues fresh credentials and URLs for deviceID.
func (s *Server) Credentials(reqHost, deviceID string) Credentials {
	u, p, exp := Issue(s.cfg.Secret, deviceID, s.now())
	return Credentials{URLs: URLs(HostFor(reqHost, s.cfg.PublicHost), s.port), Username: u, Credential: p, ExpiresAt: exp}
}

func (s *Server) relayIP() net.IP {
	if p := s.relay.Load(); p != nil {
		return *p
	}
	return nil
}

func (s *Server) setRelay(ip net.IP) {
	s.relay.Store(&ip)
	s.mu.Lock()
	s.resolvedAt = s.now()
	s.mu.Unlock()
}

// Resolve resolves the A record of the public host now and updates the relay address. Without an IPv4 result
// the previous address is dropped (allocations then fail) and a warning is logged once per change.
func (s *Server) Resolve(ctx context.Context) {
	if s.cfg.RelayIP != nil {
		return
	}
	var ip net.IP
	var rerr error
	if lit := net.ParseIP(s.cfg.PublicHost); lit != nil {
		ip = lit.To4()
	} else {
		rctx, cancel := context.WithTimeout(ctx, 10*time.Second)
		defer cancel()
		var ips []net.IP
		ips, rerr = s.cfg.Resolver(rctx, s.cfg.PublicHost)
		for _, c := range ips {
			if v4 := c.To4(); v4 != nil {
				ip = v4
				break
			}
		}
	}
	state := "none"
	if ip != nil {
		state = ip.String()
	}
	s.mu.Lock()
	changed := state != s.lastState
	s.lastState = state
	s.mu.Unlock()
	if ip == nil {
		s.relay.Store(nil)
		if changed {
			s.log.Warn("TURN: public host has no IPv4 address; relay allocations fail until it resolves", "host", s.cfg.PublicHost, "err", rerr)
		}
		return
	}
	s.setRelay(ip)
	if changed {
		s.log.Info("TURN relay address", "host", s.cfg.PublicHost, "ipv4", state)
	}
}

func (s *Server) resolveLoop(ctx context.Context) {
	defer s.wg.Done()
	t := time.NewTicker(s.cfg.ResolveInterval)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			s.Resolve(ctx)
		}
	}
}

func (s *Server) auth(username, _ string, _ net.Addr) ([]byte, bool) {
	exp, id, ok := ParseUsername(username)
	now := s.now()
	if !ok || !now.Before(exp) || exp.After(now.Add(CredentialTTL+time.Minute)) {
		return nil, false
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if !s.cfg.DeviceOK(ctx, id) {
		return nil, false
	}
	return turn.GenerateAuthKey(username, Realm, password(s.cfg.Secret, username)), true
}

func (s *Server) permission(_ net.Addr, peer net.IP) bool {
	if PeerAllowed(s.relayIP(), peer, s.cfg.LANPeerOK) {
		return true
	}
	if a, ok := netip.AddrFromSlice(peer); ok {
		s.logRefused(a.Unmap())
	}
	return false
}

// logRefused logs a refused peer at most once per peer IP per minute.
func (s *Server) logRefused(ip netip.Addr) {
	now := s.now()
	s.logMu.Lock()
	last, seen := s.refusedLog[ip]
	if seen && now.Sub(last) < time.Minute {
		s.logMu.Unlock()
		return
	}
	if len(s.refusedLog) > 1024 {
		for k, t := range s.refusedLog {
			if now.Sub(t) >= time.Minute {
				delete(s.refusedLog, k)
			}
		}
	}
	s.refusedLog[ip] = now
	s.logMu.Unlock()
	s.log.Info("TURN: relay peer refused (internal or reserved address)", "peer", ip.String())
}

func (s *Server) quota(username, _ string, _ net.Addr) bool {
	_, id, ok := ParseUsername(username)
	if !ok {
		return false
	}
	s.allocMu.Lock()
	defer s.allocMu.Unlock()
	return s.allocs[id] < MaxAllocationsPerDevice
}

func (s *Server) trackAlloc(username string, delta int) {
	_, id, ok := ParseUsername(username)
	if !ok {
		return
	}
	s.allocMu.Lock()
	defer s.allocMu.Unlock()
	if n := s.allocs[id] + delta; n > 0 {
		s.allocs[id] = n
	} else {
		delete(s.allocs, id)
	}
}

// limitListener closes connections beyond the cap right after Accept (never blocks Accept).
type limitListener struct {
	net.Listener
	sem chan struct{}
}

func (l *limitListener) Accept() (net.Conn, error) {
	for {
		c, err := l.Listener.Accept()
		if err != nil {
			return nil, err
		}
		select {
		case l.sem <- struct{}{}:
			return &limitConn{Conn: c, release: func() { <-l.sem }}, nil
		default:
			c.Close()
		}
	}
}

type limitConn struct {
	net.Conn
	once    sync.Once
	release func()
}

func (c *limitConn) Close() error {
	c.once.Do(c.release)
	return c.Conn.Close()
}

// relayGen allocates UDP relay sockets in the configured range and reports the current public IPv4.
type relayGen struct{ s *Server }

func (g *relayGen) Validate() error { return nil }

func (g *relayGen) AllocatePacketConn(network string, requested int) (net.PacketConn, net.Addr, error) {
	ip := g.s.relayIP()
	if ip == nil {
		return nil, nil, errors.New("no public IPv4 resolved for the relay")
	}
	listen := "0.0.0.0"
	if ip.IsLoopback() {
		listen = "127.0.0.1"
	}
	try := func(port int) (net.PacketConn, net.Addr, error) {
		c, err := net.ListenPacket("udp4", net.JoinHostPort(listen, strconv.Itoa(port)))
		if err != nil {
			return nil, nil, err
		}
		a := c.LocalAddr().(*net.UDPAddr)
		return c, &net.UDPAddr{IP: ip, Port: a.Port}, nil
	}
	if requested != 0 {
		if requested < g.s.cfg.RelayMin || requested > g.s.cfg.RelayMax {
			return nil, nil, errors.New("requested port outside the relay range")
		}
		return try(requested)
	}
	n := g.s.cfg.RelayMax - g.s.cfg.RelayMin + 1
	start := int(time.Now().UnixNano() % int64(n))
	for i := 0; i < n; i++ {
		if c, a, err := try(g.s.cfg.RelayMin + (start+i)%n); err == nil {
			return c, a, nil
		}
	}
	return nil, nil, errors.New("relay port range exhausted")
}

func (g *relayGen) AllocateConn(string, int) (net.Conn, net.Addr, error) {
	return nil, nil, errors.New("TCP allocations are not supported")
}
