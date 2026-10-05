// Command framebeam-hub ist der FrameBeam Hub.
//
//	framebeam-hub [flags]                      Server starten (API + Info-Endpunkt)
//	framebeam-hub setup-admin -username <name> ersten Admin anlegen (Passwort von stdin)
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
	"strings"
	"syscall"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/httpapi"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/store"
	"github.com/phabioo/framebeam/server/internal/tlsutil"
	"github.com/phabioo/framebeam/server/internal/version"
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
		fmt.Fprintln(os.Stderr, "Fehler:", err)
		os.Exit(1)
	}
}

// openService öffnet Datenbank und Service im Datenverzeichnis.
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
	username := fs.String("username", "", "Benutzername des Admins")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if *username == "" {
		return errors.New("-username fehlt")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	line, err := bufio.NewReader(in).ReadString('\n')
	if err != nil && !(errors.Is(err, io.EOF) && line != "") {
		return errors.New("Passwort von stdin lesen: eine Zeile erwartet")
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
	fmt.Fprintf(out, "Admin %q angelegt.\n", u.Username)
	return nil
}

func runServer(args []string) error {
	fs := flag.NewFlagSet("framebeam-hub", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	showVersion := fs.Bool("version", false, "Version ausgeben und beenden")
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
		log.Warn("kein Admin vorhanden: 'framebeam-hub setup-admin -username <name>' ausführen (Web-Setup folgt)")
	}

	mux := http.NewServeMux()
	httpapi.Register(mux, svc, log)
	// Platz für das Webinterface auf "/" (Paket 3).

	srv := &http.Server{
		Handler:           httpapi.LogRequests(log, mux),
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       120 * time.Second,
		ErrorLog:          slog.NewLogLogger(log.Handler(), slog.LevelWarn),
	}
	if cfg.UseTLS() {
		var cert tls.Certificate
		if cfg.TLSCert != "" {
			cert, err = tlsutil.Load(cfg.TLSCert, cfg.TLSKey)
		} else {
			cert, err = tlsutil.EnsureSelfSigned(cfg.DataDir)
		}
		if err != nil {
			return fmt.Errorf("TLS-Zertifikat: %w", err)
		}
		srv.TLSConfig = &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS12}
		log.Info("TLS-Zertifikat", "sha256_fingerprint", tlsutil.Fingerprint(cert))
	} else {
		log.Warn("Entwicklungsmodus: HTTP ohne TLS", "loopback", cfg.ListenIsLoopback())
	}

	ln, err := net.Listen("tcp", cfg.Listen)
	if err != nil {
		return err
	}
	info := svc.Info()
	log.Info("FrameBeam Hub gestartet", "version", version.String(), "hub_id", info.HubID, "name", info.Name,
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
	log.Info("Beende FrameBeam Hub")
	shutCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := srv.Shutdown(shutCtx); err != nil {
		srv.Close()
	}
	return nil
}
