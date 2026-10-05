// Command framebeam-hub is the FrameBeam Hub.
//
//	framebeam-hub [flags]                      start the server (API + info endpoint)
//	framebeam-hub setup-admin -username <name> create the first admin (password from stdin)
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
	svc, err := hub.Open(ctx, db, hub.Options{DataDir: cfg.DataDir, Name: cfg.Name, HubVersion: version.String()})
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
			cert, err = tlsutil.EnsureSelfSigned(cfg.DataDir)
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
	return nil
}
