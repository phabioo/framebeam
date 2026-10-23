package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
)

// Sharing on Windows (ADR 0030 D6): "Stop sharing" and "Share again" in the app run
//
//	uzume-server sharing stop --grant-user DOMAIN\name [--result FILE] [-data-dir DIR]
//	uzume-server sharing start [--result FILE]
//
// elevated. stop makes the server service rest (stopped, start type disabled, still registered) and lets the user run
// the server as the app's child process on the service's data folder; start undoes that. The Windows calls sit behind
// sharingSystem so that the logic is tested with a fake.

// errSharingUnsupported: the sharing commands exist on Windows only; main exits with status 2.
var errSharingUnsupported = errors.New("not supported")

// sharingWait is how long a service gets to stop or to be running.
const sharingWait = 30 * time.Second

type serviceStart int

const (
	startAutomatic serviceStart = iota
	startDisabled
)

// sharingSystem is what the commands need from the machine.
type sharingSystem interface {
	// ServiceExists reports whether the service is registered.
	ServiceExists(name string) (bool, error)
	// StopService stops a service and waits until it is stopped (nothing to do if it is).
	StopService(name string, wait time.Duration) error
	// StartService starts a service and waits until it is running (nothing to do if it is).
	StartService(name string, wait time.Duration) error
	SetStartType(name string, start serviceStart) error
	// GrantModify adds an inherited allow entry for Modify on dir (and everything below) for the account.
	GrantModify(dir, account string) error
	// ServerPort is the port the installer recorded for the server service (0 = unknown).
	ServerPort() int
}

// sharingResult is the JSON the commands write to --result for the app.
type sharingResult struct {
	OK      bool   `json:"ok"`
	Error   string `json:"error"`
	DataDir string `json:"data_dir"`
	Port    int    `json:"port"`
}

func runSharing(args []string, out io.Writer, sys sharingSystem) error {
	if sys == nil {
		return errSharingUnsupported
	}
	if len(args) == 0 || (args[0] != "stop" && args[0] != "start") {
		return &usageError{msg: "usage: uzume-server sharing stop --grant-user DOMAIN\\name [--result FILE] [-data-dir DIR]\n       uzume-server sharing start [--result FILE]"}
	}
	sub := args[0]
	fl := flag.NewFlagSet("sharing "+sub, flag.ContinueOnError)
	cfg := config.Register(fl, os.Getenv)
	resultFile := fl.String("result", "", "write the outcome as JSON to this file")
	user := fl.String("grant-user", "", "stop: the Windows account (DOMAIN\\name) that gets Modify access to the data folder")
	if err := fl.Parse(args[1:]); err != nil {
		return err
	}
	if fl.NArg() > 0 {
		return &usageError{msg: "sharing " + sub + ": unexpected argument " + fl.Arg(0)}
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	res := sharingResult{DataDir: cfg.DataDir, Port: sys.ServerPort()}
	var err error
	if sub == "stop" {
		err = sharingStop(sys, cfg.DataDir, *user)
	} else {
		err = sharingStart(sys)
	}
	res.OK = err == nil
	if err != nil {
		res.Error = err.Error()
	}
	if *resultFile != "" {
		if werr := writeJSONFile(*resultFile, res); werr != nil {
			return errors.Join(err, fmt.Errorf("write result file: %w", werr))
		}
	}
	if err != nil {
		return err
	}
	if sub == "stop" {
		fmt.Fprintf(out, "Sharing stopped. The server service is stopped and disabled; %s may use %s.\n", *user, cfg.DataDir)
	} else {
		fmt.Fprintln(out, "Sharing started. The server service is running again.")
	}
	return nil
}

// sharingStop: the user gets Modify access first (idempotent, nothing else depends on it), then the services stop
// and are disabled. Everything is safe to repeat.
func sharingStop(sys sharingSystem, dataDir, user string) error {
	if user == "" {
		return errors.New("--grant-user is missing")
	}
	hub, err := sys.ServiceExists(hubServiceName)
	if err != nil {
		return err
	}
	if !hub {
		return fmt.Errorf("the service %s is not installed", hubServiceName)
	}
	upd, err := sys.ServiceExists(updaterServiceName)
	if err != nil {
		return err
	}
	if st, serr := os.Stat(dataDir); serr != nil || !st.IsDir() {
		return fmt.Errorf("the data folder %s does not exist", dataDir)
	}
	if err := sys.GrantModify(dataDir, user); err != nil {
		return fmt.Errorf("give %s access to %s: %w", user, dataDir, err)
	}
	if err := sys.StopService(hubServiceName, sharingWait); err != nil {
		return fmt.Errorf("stop %s: %w", hubServiceName, err)
	}
	if upd {
		if err := sys.StopService(updaterServiceName, sharingWait); err != nil {
			return fmt.Errorf("stop %s: %w", updaterServiceName, err)
		}
	}
	if err := sys.SetStartType(hubServiceName, startDisabled); err != nil {
		return fmt.Errorf("disable %s: %w", hubServiceName, err)
	}
	if upd {
		if err := sys.SetStartType(updaterServiceName, startDisabled); err != nil {
			return fmt.Errorf("disable %s: %w", updaterServiceName, err)
		}
	}
	return nil
}

func sharingStart(sys sharingSystem) error {
	hub, err := sys.ServiceExists(hubServiceName)
	if err != nil {
		return err
	}
	if !hub {
		return fmt.Errorf("the service %s is not installed", hubServiceName)
	}
	upd, err := sys.ServiceExists(updaterServiceName)
	if err != nil {
		return err
	}
	if err := sys.SetStartType(hubServiceName, startAutomatic); err != nil {
		return fmt.Errorf("enable %s: %w", hubServiceName, err)
	}
	if upd {
		if err := sys.SetStartType(updaterServiceName, startAutomatic); err != nil {
			return fmt.Errorf("enable %s: %w", updaterServiceName, err)
		}
	}
	if err := sys.StartService(hubServiceName, sharingWait); err != nil {
		return fmt.Errorf("start %s: %w", hubServiceName, err)
	}
	if upd {
		// The updater only waits for update requests; the server is what matters, so this is best effort.
		_ = sys.StartService(updaterServiceName, sharingWait)
	}
	return nil
}

func writeJSONFile(path string, v any) error {
	b, err := json.Marshal(v)
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(b, '\n'), 0o644)
}
