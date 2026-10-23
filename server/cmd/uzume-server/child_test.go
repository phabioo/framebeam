package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
)

type lockedBuf struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (l *lockedBuf) Write(p []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.b.Write(p)
}
func (l *lockedBuf) String() string { l.mu.Lock(); defer l.mu.Unlock(); return l.b.String() }

// readyPorts returns the ports of the UZUME_READY lines printed so far.
func readyPorts(t *testing.T, out string) []int {
	t.Helper()
	var ports []int
	for _, l := range strings.Split(strings.TrimSpace(out), "\n") {
		if l == "" {
			continue
		}
		var p int
		if n, err := fmt.Sscanf(l, "UZUME_READY %d", &p); err != nil || n != 1 {
			t.Fatalf("unexpected stdout line %q", l)
		}
		ports = append(ports, p)
	}
	return ports
}

func childConfig(t *testing.T, dir, listen string) *config.Config {
	t.Helper()
	fl := flag.NewFlagSet("test", flag.ContinueOnError)
	cfg := config.Register(fl, func(string) string { return "" })
	err := fl.Parse([]string{"-child", "-dev", "-data-dir", dir, "-listen", listen,
		"-update-index-url", "file:///nonexistent/index.json", "-plugin-index-url", "none",
		"-core-buildbot-url", "https://127.0.0.1:1/nightly", "-core-info-url", "https://127.0.0.1:1/info.zip"})
	if err != nil {
		t.Fatal(err)
	}
	if err := cfg.Validate(); err != nil {
		t.Fatal(err)
	}
	return cfg
}

func captureStdout(t *testing.T) *lockedBuf {
	t.Helper()
	buf := &lockedBuf{}
	old := stdout
	stdout = buf
	t.Cleanup(func() { stdout = old })
	return buf
}

func waitFor(t *testing.T, what string, ok func() bool) {
	t.Helper()
	for i := 0; i < 200; i++ {
		if ok() {
			return
		}
		time.Sleep(25 * time.Millisecond)
	}
	t.Fatalf("timeout waiting for %s", what)
}

func TestChildServe(t *testing.T) {
	dir := newServerData(t, false)
	// network_sharing=true is stored (as by a service that shared this data); the child must not use it.
	svc, closeFn := testService(t, dir)
	if err := svc.SetNetOverride(t.Context(), config.NetSharing, "true"); err != nil {
		t.Fatal(err)
	}
	closeFn()
	out := captureStdout(t)
	cfg := childConfig(t, dir, "127.0.0.1:0")
	ctx, cancel := context.WithCancel(t.Context())
	defer cancel()
	log := slog.New(slog.NewTextHandler(io.Discard, nil))
	done := make(chan error, 1)
	go func() { done <- serveLoop(ctx, cfg, log) }()

	waitFor(t, "UZUME_READY", func() bool { return len(readyPorts(t, out.String())) >= 1 })
	port := readyPorts(t, out.String())[0]
	if port == 0 {
		t.Fatal("port 0 announced")
	}
	status := func(port int) map[string]any {
		resp, err := http.Get(fmt.Sprintf("http://127.0.0.1:%d/api/v1/local/status", port))
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		var m map[string]any
		if err := json.NewDecoder(resp.Body).Decode(&m); err != nil {
			t.Fatal(err)
		}
		return m
	}
	if st := status(port); st["network_sharing"] != false || st["child"] != true {
		t.Fatalf("%v", st)
	}
	// Loopback only: the machine's other addresses refuse the connection.
	if addrs, _ := net.InterfaceAddrs(); len(addrs) > 0 {
		for _, a := range addrs {
			if ipn, ok := a.(*net.IPNet); ok && !ipn.IP.IsLoopback() && ipn.IP.To4() != nil {
				if c, err := net.DialTimeout("tcp", net.JoinHostPort(ipn.IP.String(), fmt.Sprint(port)), time.Second); err == nil {
					c.Close()
					t.Fatalf("reachable on %s", ipn.IP)
				}
				break
			}
		}
	}

	// A restart runs in-process (the binary is never re-executed) and announces the port again.
	requestRestart()
	waitFor(t, "second UZUME_READY", func() bool { return len(readyPorts(t, out.String())) >= 2 })
	if st := status(readyPorts(t, out.String())[1]); st["child"] != true {
		t.Fatalf("%v", st)
	}
	select {
	case err := <-done:
		t.Fatalf("serveLoop ended after restart: %v", err)
	default:
	}

	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(15 * time.Second):
		t.Fatal("no graceful shutdown")
	}
	// The stored value was not touched.
	svc, closeFn = testService(t, dir)
	defer closeFn()
	stored, _ := svc.NetOverrides(t.Context())
	if stored[config.NetSharing] != "true" {
		t.Fatalf("stored sharing changed: %v", stored)
	}
}

func TestChildPortInUse(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	_, port, _ := net.SplitHostPort(ln.Addr().String())
	out := captureStdout(t)
	cfg := childConfig(t, newServerData(t, false), "127.0.0.1:"+port)
	err = serveLoop(t.Context(), cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err == nil || !strings.Contains(err.Error(), "address in use") {
		t.Fatalf("%v", err)
	}
	if out.String() != "" {
		t.Fatalf("stdout %q", out.String())
	}
}

func TestWatchParentReturnsOnEOF(t *testing.T) {
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() { watchParent(r); close(done) }()
	w.WriteString("ignored")
	select {
	case <-done:
		t.Fatal("returned before EOF")
	case <-time.After(100 * time.Millisecond):
	}
	w.Close()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("no return on EOF")
	}
}

func TestChildNoSelfUpdate(t *testing.T) {
	// packaged() is false in child mode, so the check never stages anything and install is refused.
	dir := newServerData(t, false)
	fl := flag.NewFlagSet("t", flag.ContinueOnError)
	cfg := config.Register(fl, func(string) string { return "" })
	if err := fl.Parse([]string{"-child", "-data-dir", dir}); err != nil {
		t.Fatal(err)
	}
	svc, closeFn, err := openService(t.Context(), cfg, false)
	if err != nil {
		t.Fatal(err)
	}
	defer closeFn()
	st, err := svc.UpdateStatus(t.Context())
	if err != nil || st.Packaged {
		t.Fatalf("packaged=%v err=%v", st.Packaged, err)
	}
	if _, err := svc.InstallUpdate(t.Context(), true); err == nil {
		t.Fatal("install accepted in child mode")
	}
}
