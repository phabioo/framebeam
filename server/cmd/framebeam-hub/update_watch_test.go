package main

import (
	"bytes"
	"context"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/updates"
)

func TestWatchUpdatesAppliesRequest(t *testing.T) {
	setVersion(t, "0.3.0-beta.5", "beta", "")
	f := newCLIFeed(t, "0.3.0-beta.6")
	data, req := t.TempDir(), t.TempDir()
	var out bytes.Buffer
	if err := runUpdate(append([]string{"stage", "-channel", "beta"}, f.flags(data, req)...), &out); err != nil {
		t.Fatal(err) // stage also wrote the request file
	}
	cfg := &config.Config{DataDir: data, UpdateRequestDir: req}
	keys, err := corepkg.TrustedKeys([]string{f.pubB64})
	if err != nil {
		t.Fatal(err)
	}
	var mu sync.Mutex
	var calls [][]string
	run := func(_ context.Context, name string, args ...string) ([]byte, error) {
		mu.Lock()
		defer mu.Unlock()
		calls = append(calls, append([]string{name}, args...))
		return nil, nil
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		defer close(done)
		watchUpdates(ctx, cfg, keys, run, t.TempDir(), slog.New(slog.NewTextHandler(io.Discard, nil)))
	}()
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		if r, _ := updates.ReadResult(data); r != nil {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	cancel()
	<-done
	r, _ := updates.ReadResult(data)
	mu.Lock()
	defer mu.Unlock()
	if r == nil || !r.OK || r.Version != "0.3.0-beta.6" || len(calls) != 1 || updates.RequestPending(req) {
		t.Fatalf("result=%+v calls=%v pending=%v", r, calls, updates.RequestPending(req))
	}
}

func TestRotatingWriter(t *testing.T) {
	p := filepath.Join(t.TempDir(), "logs", "hub.log")
	w, err := newRotatingWriter(p, 100)
	if err != nil {
		t.Fatal(err)
	}
	line := strings.Repeat("a", 59) + "\n" // 60 bytes: the second write exceeds 100
	for i := 0; i < 2; i++ {
		if _, err := w.Write([]byte(line)); err != nil {
			t.Fatal(err)
		}
	}
	if b, _ := os.ReadFile(p + ".1"); string(b) != line {
		t.Fatalf("rotated file: %q", b)
	}
	if b, _ := os.ReadFile(p); string(b) != line {
		t.Fatalf("current file: %q", b)
	}
	// A third rotation replaces hub.log.1; only two files exist.
	w.Write([]byte(line))
	w.Write([]byte(line))
	w.Close()
	if entries, _ := os.ReadDir(filepath.Dir(p)); len(entries) != 2 {
		t.Fatalf("%v", entries)
	}
	if _, err := w.Write([]byte("x")); err == nil {
		t.Fatal("write after Close")
	}
	// Reopening continues the existing file (size is taken from disk).
	w2, err := newRotatingWriter(p, 100)
	if err != nil {
		t.Fatal(err)
	}
	defer w2.Close()
	if w2.size != int64(len(line)) {
		t.Fatalf("size %d", w2.size)
	}
}

func TestServeLoopRestartMode(t *testing.T) {
	// Where the restart is not in-process, errRestart reaches main (which re-executes); with in-process restart
	// serveLoop never returns it. Only the decision is checked here, the lifecycle is covered by the E2E scripts.
	if inProcessRestart != (osIsWindows()) {
		t.Fatal("in-process restart is Windows only")
	}
}

func osIsWindows() bool { return filepath.Separator == '\\' }
