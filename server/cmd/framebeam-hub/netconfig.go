package main

import (
	"errors"
	"fmt"
	"log/slog"
	"net"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/web"
)

// Network settings saved on the web interface win over hub.env and flags (ADR 0015). The pieces below build the
// effective configuration at startup and implement the restart the web interface can request.

// restartCh carries the restart request from the web interface to runServer.
var restartCh = make(chan struct{}, 1)

// errRestart is returned by runServer after a graceful shutdown that was requested on the web interface.
var errRestart = errors.New("restart requested")

// requestRestart asks runServer to shut down gracefully and main to re-execute the binary. It never blocks.
func requestRestart() {
	select {
	case restartCh <- struct{}{}:
	default:
	}
}

// effectiveConfig applies the saved network settings to a copy of base. A value that cannot be read is skipped
// (the hub.env value stays) and reported as an issue; if the result is contradictory the whole base is used.
func effectiveConfig(base *config.Config, stored map[string]string, log *slog.Logger) (config.Config, []web.NetIssue) {
	eff := *base
	eff.ICEServers = append([]string(nil), base.ICEServers...)
	var issues []web.NetIssue
	for k, err := range eff.ApplyNet(stored) {
		log.Error("saved network setting ignored", "setting", k, "err", err)
		issues = append(issues, web.NetIssue{Key: k, Value: stored[k],
			Message: fmt.Sprintf("The saved value of %q could not be read (%v). The value from hub.env is used.", config.NetLabel(k), err)})
	}
	if err := eff.Validate(); err != nil {
		log.Error("saved network settings are contradictory; using hub.env and flags only", "err", err)
		issues = append(issues, web.NetIssue{Key: "", Value: "",
			Message: fmt.Sprintf("The saved network settings are contradictory (%v). The hub uses the values from hub.env until you change them.", err)})
		eff = *base
		eff.ICEServers = append([]string(nil), base.ICEServers...)
	}
	return eff, issues
}

// hasTURNOverride reports whether a TURN related setting was saved on the web interface.
func hasTURNOverride(stored map[string]string) bool {
	for _, k := range []string{config.NetTURN, config.NetPublicHost, config.NetTURNPort, config.NetRelayPorts, config.NetRelayIP} {
		if _, ok := stored[k]; ok {
			return true
		}
	}
	return false
}

// listenIssue describes a saved listen address that could not be bound.
type listenIssue struct {
	addr  string
	cause error
}

func (l *listenIssue) issue(eff config.Config, fallback string) web.NetIssue {
	port := eff.ListenPort()
	return web.NetIssue{Key: config.NetListenPort, Value: eff.NetValue(config.NetListenPort),
		Message: fmt.Sprintf("Saved port %d could not be used: %v. The hub uses %s until you change it.", port, l.cause, fallback)}
}

// chooseListen binds the effective listen address. If that fails and differs from the address given by
// hub.env/flags (so a saved port is involved), it binds the fallback instead and returns what went wrong.
// Without a differing fallback the error is returned as is.
func chooseListen(effective, fallback string, listen func(addr string) (net.Listener, error)) (net.Listener, *listenIssue, error) {
	ln, err := listen(effective)
	if err == nil {
		return ln, nil, nil
	}
	if effective == fallback {
		return nil, nil, err
	}
	ln, ferr := listen(fallback)
	if ferr != nil {
		return nil, nil, fmt.Errorf("listen on %s: %v; fallback %s: %w", effective, err, fallback, ferr)
	}
	return ln, &listenIssue{addr: effective, cause: err}, nil
}
