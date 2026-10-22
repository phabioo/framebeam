// Package sysmetrics reads the few host numbers the server status page and the metrics endpoint show: CPU load,
// memory and the free space of a directory. Everything is best effort: a platform without a reader reports
// ok == false and the pages show "n/a".
package sysmetrics

import (
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Started is the start time of the process (package initialization).
var Started = time.Now()

// Mem is the memory use. Process is what the Go runtime holds from the OS; the system numbers are 0 with SystemOK
// false where the platform has no reader.
type Mem struct {
	Process     uint64
	SystemTotal uint64
	SystemUsed  uint64
	SystemOK    bool
}

// Memory returns the process and, where available, the system memory.
func Memory() Mem {
	var ms runtime.MemStats
	runtime.ReadMemStats(&ms)
	m := Mem{Process: ms.Sys}
	m.SystemTotal, m.SystemUsed, m.SystemOK = systemMemory()
	return m
}

// Disk returns the size and the free space available to the server of the volume holding dir.
func Disk(dir string) (total, free uint64, ok bool) { return disk(dir) }

// UsedPercent is the used share of a volume in percent (0 for an unknown total).
func UsedPercent(total, free uint64) float64 {
	if total == 0 || free > total {
		return 0
	}
	return float64(total-free) * 100 / float64(total)
}

// ---- CPU ----

// cpuReader returns the cumulative busy and total CPU time of the machine in arbitrary units.
type cpuReader func() (busy, total uint64, ok bool)

type cpuSampler struct {
	read cpuReader
	now  func() time.Time
	wait time.Duration // first call: sampling window

	mu       sync.Mutex
	have     bool
	busy     uint64
	total    uint64
	at       time.Time
	percent  float64
	percentS bool
}

// minSampleGap is the shortest window a new sample covers; calls inside it return the previous value.
const minSampleGap = 2 * time.Second

var defaultCPU = &cpuSampler{read: readCPU, now: time.Now, wait: 250 * time.Millisecond}

// CPUPercent is the machine-wide CPU load in percent over the time since the previous sample (at least 2 s; the very
// first call measures for a short moment). ok is false where the platform has no reader.
func CPUPercent() (percent float64, ok bool) { return defaultCPU.percentNow() }

func (c *cpuSampler) percentNow() (float64, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	busy, total, ok := c.read()
	if !ok {
		return 0, false
	}
	now := c.now()
	if !c.have {
		c.busy, c.total, c.at, c.have = busy, total, now, true
		c.mu.Unlock()
		time.Sleep(c.wait)
		c.mu.Lock()
		if b2, t2, ok2 := c.read(); ok2 {
			c.apply(b2, t2, c.now())
		}
		return c.percent, c.percentS
	}
	if now.Sub(c.at) >= minSampleGap {
		c.apply(busy, total, now)
	}
	return c.percent, c.percentS
}

func (c *cpuSampler) apply(busy, total uint64, now time.Time) {
	if total > c.total && busy >= c.busy {
		p := float64(busy-c.busy) * 100 / float64(total-c.total)
		if p > 100 {
			p = 100
		}
		c.percent, c.percentS = p, true
	}
	c.busy, c.total, c.at = busy, total, now
}

// ---- parsers (Linux text formats, kept here so they are testable everywhere) ----

// parseProcStat reads the aggregate "cpu" line of /proc/stat.
func parseProcStat(s string) (busy, total uint64, ok bool) {
	for _, line := range strings.Split(s, "\n") {
		f := strings.Fields(line)
		if len(f) < 5 || f[0] != "cpu" {
			continue
		}
		var v [10]uint64
		for i := 1; i < len(f) && i <= 10; i++ {
			n, err := strconv.ParseUint(f[i], 10, 64)
			if err != nil {
				return 0, 0, false
			}
			v[i-1] = n
		}
		// user nice system idle iowait irq softirq steal (guest time is already inside user)
		idle := v[3] + v[4]
		total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7]
		return total - idle, total, true
	}
	return 0, 0, false
}

// parseMeminfo reads MemTotal and MemAvailable (kB) of /proc/meminfo.
func parseMeminfo(s string) (total, used uint64, ok bool) {
	var tot, avail uint64
	var haveT, haveA bool
	for _, line := range strings.Split(s, "\n") {
		k, v, found := strings.Cut(line, ":")
		if !found {
			continue
		}
		f := strings.Fields(v)
		if len(f) == 0 {
			continue
		}
		n, err := strconv.ParseUint(f[0], 10, 64)
		if err != nil {
			continue
		}
		switch k {
		case "MemTotal":
			tot, haveT = n*1024, true
		case "MemAvailable":
			avail, haveA = n*1024, true
		}
	}
	if !haveT || !haveA || avail > tot {
		return 0, 0, false
	}
	return tot, tot - avail, true
}
