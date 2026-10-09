// Command framebeam-hub is the FrameBeam Hub.
//
//	framebeam-hub [flags]                      start the server (API + info endpoint)
//	framebeam-hub setup-admin -username <name> create the first admin (password from stdin)
//	framebeam-hub renew-cert                   renew the self-generated TLS certificate now and exit
//	framebeam-hub import-cores <dir>           import libretro buildbot core zips from a directory (offline) and exit
//	framebeam-hub version [--json]             print the version (JSON: product, version, channel, commit, protocol versions)
//	framebeam-hub update check [-channel c]    print the update selection as JSON
//	framebeam-hub update stage [-channel c]    check, download, verify and stage the update, create the request file
//	framebeam-hub update apply-staged          root helper of the update unit: verify and install the staged .deb (Windows: .msi)
//	framebeam-hub update watch                 Windows: SYSTEM helper service, applies a staged update when the Hub requests it
package main

import (
	"bufio"
	"context"
	"crypto/tls"
	"errors"
	"flag"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/httpapi"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/store"
	"github.com/phabioo/framebeam/server/internal/tlsutil"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
	"github.com/phabioo/framebeam/server/internal/version"
	"github.com/phabioo/framebeam/server/internal/web"
)

func main() {
	args := os.Args[1:]
	var err error
	if len(args) > 0 && args[0] == "setup-admin" {
		err = runSetupAdmin(args[1:], os.Stdin, os.Stdout)
	} else if len(args) > 0 && args[0] == "renew-cert" {
		err = runRenewCert(args[1:], os.Stdout)
	} else if len(args) > 0 && args[0] == "import-cores" {
		err = runImportCores(args[1:], os.Stdout)
	} else if len(args) > 0 && args[0] == "version" {
		err = runVersion(args[1:], os.Stdout)
	} else if len(args) > 0 && args[0] == "update" {
		err = runUpdate(args[1:], os.Stdout)
	} else {
		err = runServer(args)
		if errors.Is(err, errRestart) {
			// runServer returned: the server, TURN and the database are closed. Replace this process.
			err = reexec()
		}
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}
}

// openService opens the database and service in the data directory.
func openService(ctx context.Context, cfg *config.Config) (*hub.Service, func(), error) {
	if err := os.MkdirAll(cfg.DataDir, 0o750); err != nil {
		return nil, nil, err
	}
	db, err := store.Open(filepath.Join(cfg.DataDir, "framebeam.db"))
	if err != nil {
		return nil, nil, err
	}
	keys, err := cfg.TrustedCoreKeys()
	if err != nil {
		db.Close()
		return nil, nil, err
	}
	svc, err := hub.Open(ctx, db, hub.Options{DataDir: cfg.DataDir, Name: cfg.Name, HubVersion: version.String(), ICEServers: cfg.ICEServers,
		CoreBuildbotURL: cfg.CoreBuildbotURL, CoreInfoURL: cfg.CoreInfoURL, CoreTrustKeys: keys,
		SaveKeepRecent: cfg.SaveKeepRecent, SaveKeepDaily: cfg.SaveKeepDaily, SaveKeepWeekly: cfg.SaveKeepWeekly,
		UpdateIndexURL: cfg.UpdateIndexURL, UpdateRequestDir: cfg.UpdateRequestDir, UpdateChannel: version.Channel})
	if err != nil {
		db.Close()
		return nil, nil, err
	}
	return svc, func() { db.Close() }, nil
}

// startTURN starts the embedded STUN/TURN server (ADR 0012); it stops with Close on Hub shutdown.
func startTURN(ctx context.Context, cfg *config.Config, svc *hub.Service, log *slog.Logger) (*turnsrv.Server, error) {
	secret, err := svc.TURNSecret(ctx)
	if err != nil {
		return nil, fmt.Errorf("TURN secret: %w", err)
	}
	lo, hi, err := cfg.TURNRelayRange()
	if err != nil {
		return nil, err
	}
	ts, err := turnsrv.Start(ctx, turnsrv.Config{PublicHost: cfg.PublicHost, Port: cfg.TURNPort, RelayMin: lo, RelayMax: hi,
		RelayIP: net.ParseIP(cfg.TURNRelayIP), Secret: secret, DeviceOK: svc.DeviceActive, LANPeerOK: svc.ConnectedPlayerIP, Log: log})
	if err != nil {
		return nil, err
	}
	log.Info("TURN server started", "public_host", cfg.PublicHost, "port", ts.Port(), "relay_ports", cfg.TURNRelayPorts)
	return ts, nil
}

func runSetupAdmin(args []string, in io.Reader, out io.Writer) error {
	fs := flag.NewFlagSet("setup-admin", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	username := fs.String("username", "", "username of the admin")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *username == "" {
		return errors.New("-username is missing")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	line, err := bufio.NewReader(in).ReadString('\n')
	if err != nil && !(errors.Is(err, io.EOF) && line != "") {
		return errors.New("read password from stdin: expected one line")
	}
	password := strings.TrimRight(line, "\r\n")
	ctx := context.Background()
	svc, closeFn, err := openService(ctx, cfg)
	if err != nil {
		return err
	}
	defer closeFn()
	u, err := svc.CreateAdmin(ctx, *username, password)
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "Admin %q created.\n", u.Username)
	return nil
}

// runRenewCert replaces the self-generated certificate (the old pair is kept as *.prev).
// It refuses when an own certificate/key is configured. Restart the Hub afterwards.
func runRenewCert(args []string, out io.Writer) error {
	fs := flag.NewFlagSet("renew-cert", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	if cfg.TLSCert != "" || cfg.TLSKey != "" {
		return errors.New("an own TLS certificate/key is configured; the Hub never renews it, renew it where it comes from")
	}
	_, ren, err := tlsutil.RenewSelfSigned(cfg.DataDir, time.Now())
	if err != nil {
		return err
	}
	absDir, absErr := filepath.Abs(filepath.Join(cfg.DataDir, "tls"))
	if absErr != nil {
		absDir = filepath.Join(cfg.DataDir, "tls")
	}
	fmt.Fprintf(out, "Renewed certificate in %s\nOld SHA-256 fingerprint: %s\nNew SHA-256 fingerprint: %s\n", absDir, ren.OldFingerprint, ren.NewFingerprint)
	fmt.Fprintln(out, "The previous certificate and key were kept as *.prev next to them. Restart the Hub; Players must confirm the new fingerprint.")
	return nil
}

// runImportCores installs libretro buildbot core zips from a directory (<dir>/<platform>/<core>_libretro.<suffix>.zip,
// optional <dir>/info.zip) into the Hub (offline fallback, ADR 0020 D8). Restart is not needed.
func runImportCores(args []string, out io.Writer) error {
	// The directory may come before or after the flags.
	dir := ""
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		dir, args = args[0], args[1:]
	}
	fs := flag.NewFlagSet("import-cores", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if dir == "" && fs.NArg() == 1 {
		dir = fs.Arg(0)
	} else if fs.NArg() > 0 {
		return errors.New("usage: framebeam-hub import-cores <dir> [flags]")
	}
	if dir == "" {
		return errors.New("usage: framebeam-hub import-cores <dir> [flags]")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	ctx := context.Background()
	svc, closeFn, err := openService(ctx, cfg)
	if err != nil {
		return err
	}
	defer closeFn()
	sum, err := svc.ImportCores(ctx, dir)
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "Imported cores from %s: %d installed, %d updated, %d unchanged, %d problem(s)\n",
		dir, sum.Installed, sum.Updated, sum.Unchanged, len(sum.Problems))
	for _, p := range sum.Problems {
		fmt.Fprintln(out, "Rejected:", p)
	}
	if len(sum.Problems) > 0 {
		return fmt.Errorf("%d core(s) could not be imported", len(sum.Problems))
	}
	return nil
}

// inProcessRestart makes a restart requested on the web interface run the server again inside this process
// instead of re-executing the binary (Windows has no exec; the service must keep its handle to the SCM).
var inProcessRestart = runtime.GOOS == "windows"

func runServer(args []string) error {
	fs := flag.NewFlagSet("framebeam-hub", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	showVersion := fs.Bool("version", false, "print version and exit")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *showVersion {
		fmt.Println(version.String())
		return nil
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	return runMaybeService(hubServiceName, func(ctx context.Context) error {
		log, closeLog := newLogger(filepath.Join(cfg.DataDir, "logs", "hub.log"))
		defer closeLog()
		return serveLoop(ctx, cfg, log)
	})
}

// runMaybeService runs fn under the Windows service manager when started by it, else with a context that ends on
// Ctrl-C/SIGTERM.
func runMaybeService(name string, fn func(ctx context.Context) error) error {
	if isWindowsService() {
		return runAsService(name, fn)
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	return fn(ctx)
}

// serveLoop runs the server; a restart requested on the web interface runs it again when the platform restarts
// in-process, else it returns errRestart for main to re-execute the binary.
func serveLoop(ctx context.Context, cfg *config.Config, log *slog.Logger) error {
	for {
		err := serve(ctx, cfg, log)
		if !errors.Is(err, errRestart) || !inProcessRestart {
			return err
		}
		if ctx.Err() != nil {
			return nil
		}
		log.Info("Restarting FrameBeam Hub")
	}
}

// serve is the server lifecycle: it starts everything, runs until ctx ends, the listener fails or a restart is
// requested, and shuts down gracefully. A requested restart returns errRestart after everything is closed.
func serve(ctx context.Context, cfg *config.Config, log *slog.Logger) error {
	if cfg.CoreIndexURL != "" {
		log.Warn("core-index-url is deprecated and ignored: cores come from the libretro buildbot (core-buildbot-url, ADR 0020)")
	}
	ctx, cancelAll := context.WithCancel(ctx)
	defer cancelAll()

	// Values from hub.env and flags; the values saved on the web interface (Settings > Network) win over them.
	base := *cfg
	base.ICEServers = append([]string(nil), cfg.ICEServers...)

	svc, closeFn, err := openService(ctx, cfg)
	if err != nil {
		return err
	}
	defer closeFn()
	importDir := cfg.ImportDir()
	if err := os.MkdirAll(importDir, 0o750); err != nil {
		return fmt.Errorf("create library import folder: %w", err)
	}
	stored, err := svc.NetOverrides(ctx)
	if err != nil {
		return fmt.Errorf("read network settings: %w", err)
	}
	eff, issues := effectiveConfig(&base, stored, log)
	svc.SetSaveRetention(eff.SaveKeepRecent, eff.SaveKeepDaily, eff.SaveKeepWeekly)
	svc.SetICEServers(eff.ICEServers)
	if has, err := svc.HasAdmin(ctx); err == nil && !has {
		log.Warn("no admin present: run 'framebeam-hub setup-admin -username <name>' or open /setup in a browser on this machine")
	}

	webCfg := web.Config{UseTLS: eff.UseTLS()}
	if eff.TURN {
		ts, err := startTURN(ctx, &eff, svc, log)
		switch {
		case err == nil:
			defer ts.Close()
			svc.SetTURN(ts)
			defer svc.SetTURN(nil)
			webCfg.TURN = ts
		case hasTURNOverride(stored):
			// Saved values must never keep the hub from starting: run without TURN and say so in Settings.
			log.Error("built-in TURN relay could not be started with the saved settings; starting without it", "err", err)
			issues = append(issues, web.NetIssue{Key: config.NetTURN, Value: web.NetSignature(&eff, config.NetTURN),
				Message: fmt.Sprintf("The built-in TURN relay could not be started with the saved settings: %v. The hub runs without it until you change them.", err)})
			eff.TURN = false
		default:
			return err
		}
	}

	var tlsConf *tls.Config
	if cfg.UseTLS() {
		var cert tls.Certificate
		if cfg.TLSCert != "" {
			cert, err = tlsutil.Load(cfg.TLSCert, cfg.TLSKey)
		} else {
			var ren *tlsutil.Renewal
			cert, ren, err = tlsutil.EnsureSelfSignedRenewing(cfg.DataDir, time.Now())
			if err == nil && ren != nil {
				log.Warn("Self-generated TLS certificate was expired or about to expire and has been renewed; Players must confirm the new fingerprint",
					"old_sha256_fingerprint", ren.OldFingerprint, "new_sha256_fingerprint", ren.NewFingerprint,
					"old_not_after", ren.OldNotAfter.Format(time.RFC3339), "previous_files", "tls/cert.pem.prev, tls/key.pem.prev")
			}
		}
		if err != nil {
			return fmt.Errorf("TLS certificate: %w", err)
		}
		holder := newCertHolder(cert)
		tlsConf = &tls.Config{GetCertificate: holder.getCertificate, MinVersion: tls.VersionTLS12}
		webCfg.CertSource = "Self-generated"
		if cfg.TLSCert != "" {
			webCfg.CertSource = "Own cert/key"
		} else {
			webCfg.RenewCert = func() (string, time.Time, error) { return holder.renew(cfg.DataDir, time.Now()) }
		}
		webCfg.CertState = holder.state
		log.Info("TLS certificate", "sha256_fingerprint", tlsutil.Fingerprint(cert))
	} else {
		log.Warn("Development mode: HTTP without TLS", "loopback", cfg.ListenIsLoopback())
	}

	// Listener: the effective (possibly saved) address, else the one from hub.env/flags.
	ln, listenIssue, err := chooseListen(eff.Listen, base.Listen, func(addr string) (net.Listener, error) { return net.Listen("tcp", addr) })
	if err != nil {
		return err
	}
	if listenIssue != nil {
		log.Error("saved listen port could not be bound; using the address from hub.env/flags", "err", listenIssue.cause, "fallback", base.Listen)
		issues = append(issues, listenIssue.issue(eff, base.Listen))
		eff.Listen = base.Listen
	}
	webCfg.Listen = eff.Listen
	webCfg.ImportDir = importDir
	webCfg.PublicHost, webCfg.PublicPort = eff.PublicHost, publicPort(eff.Listen)
	webCfg.Net = web.NetConfig{Base: base, Running: eff, Issues: issues, RequestRestart: requestRestart}

	webSrv, err := web.New(svc, webCfg, log)
	if err != nil {
		ln.Close()
		return err
	}
	mux := http.NewServeMux()
	httpapi.Register(mux, svc, log)
	webSrv.Register(mux)

	srv := &http.Server{
		Handler:           withReadDeadline(httpapi.LogRequests(log, mux)),
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       120 * time.Second,
		ErrorLog:          slog.NewLogLogger(log.Handler(), slog.LevelWarn),
		TLSConfig:         tlsConf,
	}
	srv.RegisterOnShutdown(webSrv.Shutdown) // end SSE streams so Shutdown does not wait for them
	info := svc.Info()
	log.Info("FrameBeam Hub started", "version", version.String(), "hub_id", info.HubID, "name", info.Name,
		"listen", ln.Addr().String(), "tls", cfg.UseTLS(), "protocol_version", info.ProtocolVersion)

	go svc.RunCleanup(ctx, time.Minute, func(err error) { log.Error("cleanup", "err", err) })
	go svc.RunSaveSweep(ctx, 24*time.Hour, func(err error) { log.Error("save history sweep", "err", err) })
	// Core source sync: in the background, never blocks or fails startup; ends with ctx on shutdown.
	syncDone := make(chan struct{})
	go func() {
		defer close(syncDone)
		svc.RunCoreSync(ctx, 24*time.Hour, func(r hub.CoreSyncReport, err error) {
			if err != nil {
				log.Warn("core catalog refresh failed", "err", err)
				return
			}
			log.Info("core catalog refreshed", "cores", r.Cores)
		})
	}()

	// Update check: in the background like the core sync, never blocks or fails startup.
	updDone := make(chan struct{})
	go func() {
		defer close(updDone)
		svc.RunUpdateChecks(ctx, func(r hub.UpdateCheckReport, err error) {
			switch {
			case err != nil:
				log.Warn("update check failed", "err", err)
			case r.Staged:
				log.Info("update staged, installation requested", "version", r.Available.Version)
			case r.Available != nil:
				log.Info("update available", "version", r.Available.Version, "channel", r.Available.Channel, "breaking_for_players", r.Available.Breaking)
			}
		})
	}()

	errc := make(chan error, 1)
	go func() {
		if cfg.UseTLS() {
			errc <- srv.ServeTLS(limitListener(ln, maxConns), "", "")
		} else {
			errc <- srv.Serve(limitListener(ln, maxConns))
		}
	}()
	restarting := false
	select {
	case err := <-errc:
		return err
	case <-restartCh:
		// Requested on the web interface: shut down like on SIGTERM, then main re-executes the binary.
		restarting = true
		cancelAll()
		log.Info("Restart requested")
	case <-ctx.Done():
	}
	log.Info("Shutting down FrameBeam Hub")
	shutCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := srv.Shutdown(shutCtx); err != nil {
		srv.Close()
	}
	select { // the sync loop stops with ctx; downloads abort with it
	case <-syncDone:
	case <-time.After(5 * time.Second):
	}
	select {
	case <-updDone:
	case <-time.After(5 * time.Second):
	}
	if restarting {
		return errRestart
	}
	return nil
}
