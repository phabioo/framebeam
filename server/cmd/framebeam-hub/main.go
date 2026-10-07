// Command framebeam-hub is the FrameBeam Hub.
//
//	framebeam-hub [flags]                      start the server (API + info endpoint)
//	framebeam-hub setup-admin -username <name> create the first admin (password from stdin)
//	framebeam-hub renew-cert                   renew the self-generated TLS certificate now and exit
//	framebeam-hub import-cores <dir>           import signed core packages from a directory (offline) and exit
//	framebeam-hub version [--json]             print the version (JSON: product, version, channel, commit, protocol versions)
//	framebeam-hub update check [-channel c]    print the update selection as JSON
//	framebeam-hub update stage [-channel c]    check, download, verify and stage the update, create the request file
//	framebeam-hub update apply-staged          root helper of the update unit: verify and install the staged .deb
package main

import (
	"bufio"
	"context"
	"crypto/tls"
	"crypto/x509"
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
	"strings"
	"syscall"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/httpapi"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/store"
	"github.com/phabioo/framebeam/server/internal/tlsutil"
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
		CoreIndexURL: cfg.CoreIndexURL, CoreTrustKeys: keys,
		UpdateIndexURL: cfg.UpdateIndexURL, UpdateRequestDir: cfg.UpdateRequestDir, UpdateChannel: version.Channel})
	if err != nil {
		db.Close()
		return nil, nil, err
	}
	return svc, func() { db.Close() }, nil
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

// runImportCores verifies <dir>/cores-index.json (+ .sig) against the trusted keys like a sync and copies the
// core files found in dir into the Hub cache (offline fallback). Restart is not needed.
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
	fmt.Fprintf(out, "Imported core packages from %s: %d package(s), %d file(s) copied, %d already cached, %d not found in the directory, %d rejected",
		dir, sum.Packages, sum.Copied, sum.AlreadyCached, sum.Missing, sum.Rejected)
	if sum.Skipped > 0 {
		fmt.Fprintf(out, ", %d invalid package(s) skipped", sum.Skipped)
	}
	fmt.Fprintln(out)
	for _, p := range sum.Problems {
		fmt.Fprintln(out, "Rejected:", p)
	}
	if sum.Rejected > 0 {
		return fmt.Errorf("%d file(s) did not match the signed index", sum.Rejected)
	}
	return nil
}

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
	log := slog.New(slog.NewTextHandler(os.Stderr, nil))
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	svc, closeFn, err := openService(ctx, cfg)
	if err != nil {
		return err
	}
	defer closeFn()
	if has, err := svc.HasAdmin(ctx); err == nil && !has {
		log.Warn("no admin present: run 'framebeam-hub setup-admin -username <name>' or open /setup in a browser on this machine")
	}

	webCfg := web.Config{Listen: cfg.Listen, UseTLS: cfg.UseTLS()}

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
		tlsConf = &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS12}
		webCfg.CertFingerprint = tlsutil.Fingerprint(cert)
		webCfg.CertSource = "Self-generated"
		if cfg.TLSCert != "" {
			webCfg.CertSource = "Own cert/key"
		}
		if leaf, err := x509.ParseCertificate(cert.Certificate[0]); err == nil {
			webCfg.CertNotAfter = leaf.NotAfter
		}
		log.Info("TLS certificate", "sha256_fingerprint", webCfg.CertFingerprint)
	} else {
		log.Warn("Development mode: HTTP without TLS", "loopback", cfg.ListenIsLoopback())
	}

	webSrv, err := web.New(svc, webCfg, log)
	if err != nil {
		return err
	}
	mux := http.NewServeMux()
	httpapi.Register(mux, svc, log)
	webSrv.Register(mux)

	srv := &http.Server{
		Handler:           httpapi.LogRequests(log, mux),
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       120 * time.Second,
		ErrorLog:          slog.NewLogLogger(log.Handler(), slog.LevelWarn),
		TLSConfig:         tlsConf,
	}
	ln, err := net.Listen("tcp", cfg.Listen)
	if err != nil {
		return err
	}
	info := svc.Info()
	log.Info("FrameBeam Hub started", "version", version.String(), "hub_id", info.HubID, "name", info.Name,
		"listen", ln.Addr().String(), "tls", cfg.UseTLS(), "protocol_version", info.ProtocolVersion)

	go svc.RunCleanup(ctx, time.Minute, func(err error) { log.Error("cleanup", "err", err) })
	// Core source sync: in the background, never blocks or fails startup; ends with ctx on shutdown.
	syncDone := make(chan struct{})
	go func() {
		defer close(syncDone)
		svc.RunCoreSync(ctx, 24*time.Hour, func(r hub.CoreSyncReport, err error) {
			if err != nil {
				log.Warn("core source sync failed", "err", err)
				return
			}
			log.Info("core source synced", "packages", r.Packages, "skipped", r.Skipped, "downloaded", r.Downloaded)
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
			errc <- srv.ServeTLS(ln, "", "")
		} else {
			errc <- srv.Serve(ln)
		}
	}()
	select {
	case err := <-errc:
		return err
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
	return nil
}
