package diag

import (
	"bytes"
	"log/slog"
	"strings"
	"testing"
	"time"
)

func TestCaptureHandlerKeepsWarnAndError(t *testing.T) {
	ring := NewErrorRing(3)
	var out bytes.Buffer
	log := slog.New(NewCaptureHandler(slog.NewTextHandler(&out, nil), ring)).With("component", "web")
	log.Info("started")
	log.Warn("slow", "ms", 1200)
	log.WithGroup("g").Error("failed", "err", "boom")
	if !strings.Contains(out.String(), "started") {
		t.Fatal("records still reach the wrapped handler")
	}
	got := ring.Since(time.Now().Add(-time.Hour))
	if len(got) != 2 {
		t.Fatalf("entries = %d, want 2 (info is not kept)", len(got))
	}
	if got[0].Level != slog.LevelError || got[0].Message != "failed component=web g.err=boom" {
		t.Fatalf("newest first: %+v", got[0])
	}
	if got[1].Message != "slow component=web ms=1200" {
		t.Fatalf("second: %q", got[1].Message)
	}
	e, w := ring.Totals()
	if e != 1 || w != 1 {
		t.Fatalf("totals %d/%d", e, w)
	}
	for i := 0; i < 5; i++ {
		log.Error("again")
	}
	if n := len(ring.Since(time.Time{})); n != 3 {
		t.Fatalf("ring is bounded: %d", n)
	}
	if e, _ := ring.Totals(); e != 6 {
		t.Fatalf("totals keep counting: %d", e)
	}
}

func TestErrorRingSince(t *testing.T) {
	ring := NewErrorRing(10)
	now := time.Now()
	ring.Add(Entry{Time: now.Add(-30 * time.Hour), Level: slog.LevelError, Message: "old"})
	ring.Add(Entry{Time: now.Add(-time.Hour), Level: slog.LevelError, Message: "new"})
	got := ring.Since(now.Add(-24 * time.Hour))
	if len(got) != 1 || got[0].Message != "new" {
		t.Fatalf("%+v", got)
	}
}
