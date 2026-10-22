package sysmetrics

import (
	"runtime"
	"testing"
	"time"
)

func TestParseProcStat(t *testing.T) {
	busy, total, ok := parseProcStat("cpu  100 10 50 800 40 0 0 0 0 0\ncpu0 1 1 1 1 1 0 0 0 0 0\n")
	if !ok || total != 1000 || busy != 160 {
		t.Fatalf("busy=%d total=%d ok=%v", busy, total, ok)
	}
	if _, _, ok := parseProcStat("intr 1 2 3\n"); ok {
		t.Fatal("no cpu line must fail")
	}
	if _, _, ok := parseProcStat("cpu  a b c d e\n"); ok {
		t.Fatal("garbage must fail")
	}
}

func TestParseMeminfo(t *testing.T) {
	total, used, ok := parseMeminfo("MemTotal:        8000 kB\nMemFree: 1 kB\nMemAvailable:    2000 kB\n")
	if !ok || total != 8000*1024 || used != 6000*1024 {
		t.Fatalf("total=%d used=%d ok=%v", total, used, ok)
	}
	if _, _, ok := parseMeminfo("MemTotal: 5 kB\n"); ok {
		t.Fatal("missing MemAvailable must fail")
	}
}

func TestCPUSamplerDelta(t *testing.T) {
	var busy, total uint64
	now := time.Unix(1000, 0)
	c := &cpuSampler{read: func() (uint64, uint64, bool) { return busy, total, true }, now: func() time.Time { return now }}
	// The state after the priming sample of the first call.
	c.busy, c.total, c.at, c.have = 100, 1000, now, true
	busy, total = 150, 1100
	now = now.Add(time.Second)
	if p, ok := c.percentNow(); ok || p != 0 {
		t.Fatalf("inside the minimum gap there is no value yet: %v %v", p, ok)
	}
	now = now.Add(2 * time.Second)
	if p, ok := c.percentNow(); !ok || p != 50 {
		t.Fatalf("percent = %v %v, want 50", p, ok)
	}
	if p, _ := c.percentNow(); p != 50 {
		t.Fatalf("a call inside the gap keeps the value: %v", p)
	}
}

func TestDiskAndMemory(t *testing.T) {
	if UsedPercent(1000, 100) != 90 || UsedPercent(0, 0) != 0 || UsedPercent(10, 20) != 0 {
		t.Fatal("UsedPercent")
	}
	if runtime.GOOS == "linux" {
		total, free, ok := Disk(t.TempDir())
		if !ok || total == 0 || free > total {
			t.Fatalf("disk %d %d %v", total, free, ok)
		}
	}
	if Memory().Process == 0 {
		t.Fatal("process memory")
	}
}
