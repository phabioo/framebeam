package main

import (
	"errors"
	"io"
	"log/slog"
	"net"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/config"
)

func quietLog() *slog.Logger { return slog.New(slog.NewTextHandler(io.Discard, nil)) }

func TestChooseListenFallsBackWhenSavedAddressFails(t *testing.T) {
	var tried []string
	listen := func(fail ...string) func(string) (net.Listener, error) {
		return func(addr string) (net.Listener, error) {
			tried = append(tried, addr)
			for _, f := range fail {
				if addr == f {
					return nil, errors.New("address already in use")
				}
			}
			return net.Listen("tcp", "127.0.0.1:0")
		}
	}
	// Saved address works: no issue.
	ln, issue, err := chooseListen(":9000", ":8443", listen())
	if err != nil || issue != nil || ln == nil {
		t.Fatalf("%v %v", issue, err)
	}
	ln.Close()
	// Saved address busy: fallback to the hub.env/flag address plus an issue.
	tried = nil
	ln, issue, err = chooseListen(":9000", ":8443", listen(":9000"))
	if err != nil || issue == nil || ln == nil {
		t.Fatalf("%v %v", issue, err)
	}
	ln.Close()
	if strings.Join(tried, ",") != ":9000,:8443" {
		t.Fatalf("%v", tried)
	}
	msg := issue.issue(config.Config{Listen: ":9000"}, ":8443")
	if msg.Key != config.NetListenPort || msg.Value != "9000" ||
		msg.Message != "Saved port 9000 could not be used: address already in use. The hub uses :8443 until you change it." {
		t.Fatalf("%+v", msg)
	}
	// Nothing saved (same address) and busy: plain error as before.
	if _, issue, err = chooseListen(":8443", ":8443", listen(":8443")); err == nil || issue != nil {
		t.Fatalf("%v %v", issue, err)
	}
	// Both fail: error mentions both.
	if _, _, err = chooseListen(":9000", ":8443", listen(":9000", ":8443")); err == nil || !strings.Contains(err.Error(), ":9000") || !strings.Contains(err.Error(), ":8443") {
		t.Fatal(err)
	}
}

func TestEffectiveConfigPrecedence(t *testing.T) {
	base := config.Config{Listen: ":8443", TURNPort: 3478, TURNRelayPorts: "49160-49199", SaveKeepRecent: 20, SaveKeepDaily: 30, SaveKeepWeekly: 26,
		DataDir: "/x", CoreBuildbotURL: "https://example.org/n", CoreInfoURL: "https://example.org/i.zip", UpdateIndexURL: "https://example.org/u.json", UpdateRequestDir: "/run/framebeam",
		ICEServers: []string{"stun:env.example.com:3478"}}
	eff, issues := effectiveConfig(&base, map[string]string{config.NetListenPort: "9000", config.NetKeepRecent: "3"}, quietLog())
	if len(issues) != 0 || eff.Listen != ":9000" || eff.SaveKeepRecent != 3 || eff.SaveKeepDaily != 30 || len(eff.ICEServers) != 1 {
		t.Fatalf("%+v %v", eff, issues)
	}
	if base.Listen != ":8443" || base.SaveKeepRecent != 20 {
		t.Fatal("base modified")
	}
	// An unreadable saved value is skipped and reported; the hub.env value stays.
	eff, issues = effectiveConfig(&base, map[string]string{config.NetTURNPort: "x"}, quietLog())
	if len(issues) != 1 || issues[0].Key != config.NetTURNPort || eff.TURNPort != 3478 {
		t.Fatalf("%+v %v", eff, issues)
	}
	// Contradictory saved settings (TURN without public host): the hub.env configuration is used.
	eff, issues = effectiveConfig(&base, map[string]string{config.NetTURN: "true"}, quietLog())
	if len(issues) != 1 || eff.TURN || !strings.Contains(issues[0].Message, "contradictory") {
		t.Fatalf("%+v %v", eff, issues)
	}
	if !hasTURNOverride(map[string]string{config.NetTURN: "true"}) || hasTURNOverride(map[string]string{config.NetListenPort: "1"}) {
		t.Fatal("hasTURNOverride")
	}
}

func TestRequestRestartNeverBlocks(t *testing.T) {
	for len(restartCh) > 0 {
		<-restartCh
	}
	requestRestart()
	requestRestart() // second call while one is pending must not block
	if len(restartCh) != 1 {
		t.Fatal(len(restartCh))
	}
	<-restartCh
}
