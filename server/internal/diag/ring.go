package diag

import (
	"bytes"
	"sync"
)

// Ring keeps the most recent log lines in memory (bounded by line count and bytes), so a bundle has logs even when
// there is no log file and the journal is not readable. Lines are stored as written, redaction happens on export.
type Ring struct {
	mu       sync.Mutex
	lines    [][]byte
	bytes    int
	maxLines int
	maxBytes int
}

// Recent receives the lines of the server's logger (see newLogger in cmd/uzume-server).
var Recent = NewRing(5000, 4<<20)

// NewRing creates a ring holding at most maxLines lines and maxBytes bytes.
func NewRing(maxLines, maxBytes int) *Ring { return &Ring{maxLines: maxLines, maxBytes: maxBytes} }

// Write stores p (one log record per call) and never fails.
func (r *Ring) Write(p []byte) (int, error) {
	if len(p) > r.maxBytes {
		p = p[len(p)-r.maxBytes:]
	}
	line := bytes.Clone(p)
	r.mu.Lock()
	defer r.mu.Unlock()
	r.lines = append(r.lines, line)
	r.bytes += len(line)
	for len(r.lines) > r.maxLines || r.bytes > r.maxBytes {
		r.bytes -= len(r.lines[0])
		r.lines[0] = nil
		r.lines = r.lines[1:]
	}
	return len(p), nil
}

// Snapshot returns the stored lines, oldest first.
func (r *Ring) Snapshot() []byte {
	r.mu.Lock()
	defer r.mu.Unlock()
	var b bytes.Buffer
	for _, l := range r.lines {
		b.Write(l)
		if len(l) == 0 || l[len(l)-1] != '\n' {
			b.WriteByte('\n')
		}
	}
	return b.Bytes()
}
