package main

import (
	"context"
	"crypto/ed25519"
	"flag"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"runtime"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/updates"
	"github.com/phabioo/framebeam/server/internal/version"
)

// runUpdateWatch is the privileged helper on Windows (service FrameBeamHubUpdater, LocalSystem): it waits for the
// update request file the Hub writes and applies the staged update like `update apply-staged`. The Hub service
// itself runs unprivileged and can only stage. Interactive use (Ctrl-C ends it) is possible for testing.
func runUpdateWatch(args []string, _ io.Writer, run updates.Runner) error {
	fs := flag.NewFlagSet("update watch", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	keys, err := cfg.TrustedCoreKeys()
	if err != nil {
		return err
	}
	private := updates.DefaultPrivateDir(runtime.GOOS, os.Getenv)
	logPath := filepath.Join(private, "updater.log")
	if private == "" {
		logPath = filepath.Join(cfg.DataDir, "logs", "updater.log")
	}
	return runMaybeService(updaterServiceName, func(ctx context.Context) error {
		log, closeLog := newLogger(logPath)
		defer closeLog()
		watchUpdates(ctx, cfg, keys, run, private, log)
		return nil
	})
}

func watchUpdates(ctx context.Context, cfg *config.Config, keys []ed25519.PublicKey, run updates.Runner, privateDir string, log *slog.Logger) {
	reqFile := updates.RequestPath(cfg.UpdateRequestDir)
	log.Info("FrameBeam Hub updater started", "request_file", reqFile)
	updates.Watch(ctx, updates.WatchOptions{
		RequestFile: reqFile,
		Apply: func(ctx context.Context) error {
			log.Info("update requested")
			actx, cancel := context.WithTimeout(ctx, applyTimeout)
			defer cancel()
			res, err := updates.ApplyStaged(actx, updates.ApplyOptions{DataDir: cfg.DataDir, RequestFile: reqFile,
				Keys: keys, CurrentVersion: version.Version, Run: run, TempDir: privateDir})
			if err == nil {
				log.Info("update installed", "version", res.Version)
			}
			return err
		},
		Done: func(err error) {
			if err != nil {
				log.Error("update failed", "err", err)
			}
		},
	})
	log.Info("FrameBeam Hub updater stopped")
}
