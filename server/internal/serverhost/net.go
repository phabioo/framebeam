package serverhost

import (
	"context"
	"fmt"
	"net"
	"net/url"
	"strconv"
	"strings"

	"github.com/phabioo/uzume/server/internal/adminhost"
	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/turnsrv"
)

// netRaw turns the posted form of one setting into the raw value to store. cur is the candidate configuration.
func netRaw(key string, form url.Values, cur *config.Config) (string, error) {
	val := strings.TrimSpace(form.Get("value"))
	switch key {
	case config.NetTURN, config.NetSharing:
		if form.Get("value") == "1" {
			return "true", nil
		}
		return "false", nil
	case config.NetRelayPorts:
		min, max := strings.TrimSpace(form.Get("min")), strings.TrimSpace(form.Get("max"))
		if _, err := strconv.Atoi(min); err != nil {
			return "", &config.NetError{Msg: "Enter the first relay port as a number"}
		}
		if _, err := strconv.Atoi(max); err != nil {
			return "", &config.NetError{Msg: "Enter the last relay port as a number"}
		}
		return min + "-" + max, nil
	case config.NetICEServers:
		u := strings.TrimSpace(form.Get("url"))
		list := config.CloneICE(cur.ICEServers)
		switch form.Get("op") {
		case "none":
			list = []string{}
		case "remove":
			out := []string{}
			for _, x := range list {
				if x != u {
					out = append(out, x)
				}
			}
			list = out
			if len(list) == 0 {
				list = nil // last custom entry removed: the default applies; only the "none" button stores none
			}
		default:
			if err := config.ValidateSTUNURL(u); err != nil {
				return "", err
			}
			for _, x := range list {
				if x == u {
					return "", &config.NetError{Msg: "This server is already in the list"}
				}
			}
			list = append(list, u)
		}
		return config.EncodeICEServers(list), nil
	case config.NetListenPort, config.NetTURNPort, config.NetKeepRecent, config.NetKeepDaily, config.NetKeepWeekly:
		if _, err := strconv.Atoi(val); err != nil {
			return "", &config.NetError{Msg: "Enter a whole number"}
		}
	}
	return val, nil
}

func (s *Server) testBind(addr string) error {
	if s.cfg.Net.TestBind != nil {
		return s.cfg.Net.TestBind(addr)
	}
	return config.TestBind(addr)
}

// applyLiveNet applies the settings that need no restart.
func (s *Server) applyLiveNet(ctx context.Context) error {
	desired, _, err := s.netDesired(ctx)
	if err != nil {
		return err
	}
	s.svc.SetSaveRetention(desired.SaveKeepRecent, desired.SaveKeepDaily, desired.SaveKeepWeekly)
	s.svc.SetICEServers(desired.ICEServers)
	return nil
}

func validNetKey(key string) bool {
	for _, k := range config.NetKeys {
		if k == key {
			return true
		}
	}
	return false
}

// netSave validates and stores one network setting, then applies it live if it can be. A refused value is a
// *adminhost.ValidationError.
func (s *Server) netSave(ctx context.Context, key string, form url.Values) error {
	if !validNetKey(key) {
		return hub.ErrNotFound
	}
	cand, _, err := s.netDesired(ctx)
	if err != nil {
		return err
	}
	raw, err := netRaw(key, form, &cand)
	if err != nil {
		return &adminhost.ValidationError{Msg: err.Error()}
	}
	if err := cand.SetNet(key, raw); err != nil {
		return &adminhost.ValidationError{Msg: err.Error()}
	}
	if err := cand.ValidateNet(key); err != nil {
		return &adminhost.ValidationError{Msg: err.Error()}
	}
	if err := s.checkListenPort(key, cand); err != nil {
		return err
	}
	if err := s.svc.SetNetOverride(ctx, key, cand.NetValue(key)); err != nil {
		return err
	}
	return s.applyLiveNet(ctx)
}

// checkListenPort tests that a changed listen port can be bound.
func (s *Server) checkListenPort(key string, cand config.Config) error {
	if key == config.NetListenPort && cand.ListenPort() != s.cfg.Net.Running.ListenPort() {
		addr := config.ListenWithPort(s.cfg.Net.Running.Listen, cand.ListenPort())
		if err := s.testBind(addr); err != nil {
			s.log.Info("listen port test bind failed", "addr", addr, "err", err)
			return &adminhost.ValidationError{Msg: fmt.Sprintf("Port %d is in use", cand.ListenPort())}
		}
	}
	return nil
}

// netReset deletes the saved value (server.env/flags apply again) after checking the configuration that results.
func (s *Server) netReset(ctx context.Context, key string) error {
	if !validNetKey(key) {
		return hub.ErrNotFound
	}
	_, stored, err := s.netDesired(ctx)
	if err != nil {
		return err
	}
	// Validate the configuration that results from the reset, like a save would.
	rest := make(map[string]string, len(stored))
	for k, v := range stored {
		if k != key {
			rest[k] = v
		}
	}
	cand := s.cfg.Net.Base
	cand.ICEServers = config.CloneICE(s.cfg.Net.Base.ICEServers)
	cand.ApplyNet(rest)
	if err := cand.ValidateNet(key); err != nil {
		return &adminhost.ValidationError{Msg: err.Error()}
	}
	if err := s.checkListenPort(key, cand); err != nil {
		return err
	}
	if err := s.svc.ResetNetOverride(ctx, key); err != nil {
		return err
	}
	return s.applyLiveNet(ctx)
}

// turnForwards lists the router port forwards a public server needs (ADR 0012 D1).
func turnForwards(listen string, st turnsrv.Status) []string {
	hubPort := "8443"
	if _, p, err := net.SplitHostPort(listen); err == nil && p != "" {
		hubPort = p
	}
	return []string{
		"TCP " + hubPort + " (server, HTTPS/WSS)",
		fmt.Sprintf("UDP and TCP %d (STUN/TURN)", st.Port),
		fmt.Sprintf("UDP %d-%d (relay)", st.RelayMin, st.RelayMax),
	}
}
