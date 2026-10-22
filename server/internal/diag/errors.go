package diag

import (
	"context"
	"fmt"
	"log/slog"
	"strings"
	"sync"
	"time"
)

// Entry is one captured warning or error record.
type Entry struct {
	Time    time.Time
	Level   slog.Level
	Message string // the message followed by key=value attributes; redact before showing or exporting
}

// ErrorRing keeps the most recent WARN and ERROR log records (bounded) and counts all of them since start. It feeds
// the Status page ("Last errors") and the metrics endpoint.
type ErrorRing struct {
	mu      sync.Mutex
	max     int
	entries []Entry
	errors  uint64
	warns   uint64
}

// RecentErrors receives the WARN and ERROR records of the server's logger (see newLogger in cmd/uzume-server).
var RecentErrors = NewErrorRing(200)

// NewErrorRing creates a ring holding at most max records.
func NewErrorRing(max int) *ErrorRing { return &ErrorRing{max: max} }

// Add stores a record (levels below WARN are ignored).
func (r *ErrorRing) Add(e Entry) {
	if e.Level < slog.LevelWarn {
		return
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if e.Level >= slog.LevelError {
		r.errors++
	} else {
		r.warns++
	}
	r.entries = append(r.entries, e)
	if len(r.entries) > r.max {
		r.entries = append([]Entry(nil), r.entries[len(r.entries)-r.max:]...)
	}
}

// Since returns the records at or after t, newest first.
func (r *ErrorRing) Since(t time.Time) []Entry {
	r.mu.Lock()
	defer r.mu.Unlock()
	var out []Entry
	for i := len(r.entries) - 1; i >= 0; i-- {
		if r.entries[i].Time.Before(t) {
			break
		}
		out = append(out, r.entries[i])
	}
	return out
}

// Totals returns how many ERROR and WARN records were logged since start.
func (r *ErrorRing) Totals() (errors, warns uint64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.errors, r.warns
}

// NewCaptureHandler wraps next: every record is passed on and WARN/ERROR records are also stored in ring.
func NewCaptureHandler(next slog.Handler, ring *ErrorRing) slog.Handler {
	return &captureHandler{next: next, ring: ring}
}

type captureHandler struct {
	next  slog.Handler
	ring  *ErrorRing
	attrs []slog.Attr // from WithAttrs, already prefixed with the group path
	group string
}

func (h *captureHandler) Enabled(ctx context.Context, l slog.Level) bool {
	return h.next.Enabled(ctx, l)
}

func (h *captureHandler) Handle(ctx context.Context, r slog.Record) error {
	if r.Level >= slog.LevelWarn {
		var b strings.Builder
		b.WriteString(r.Message)
		for _, a := range h.attrs {
			writeAttr(&b, "", a)
		}
		r.Attrs(func(a slog.Attr) bool {
			writeAttr(&b, h.group, a)
			return true
		})
		t := r.Time
		if t.IsZero() {
			t = time.Now()
		}
		h.ring.Add(Entry{Time: t, Level: r.Level, Message: b.String()})
	}
	return h.next.Handle(ctx, r)
}

func (h *captureHandler) WithAttrs(attrs []slog.Attr) slog.Handler {
	c := *h
	for _, a := range attrs {
		if h.group != "" {
			a.Key = h.group + a.Key
		}
		c.attrs = append(append([]slog.Attr(nil), c.attrs...), a)
	}
	c.next = h.next.WithAttrs(attrs)
	return &c
}

func (h *captureHandler) WithGroup(name string) slog.Handler {
	c := *h
	if name != "" {
		c.group = h.group + name + "."
	}
	c.next = h.next.WithGroup(name)
	return &c
}

func writeAttr(b *strings.Builder, group string, a slog.Attr) {
	a.Value = a.Value.Resolve()
	if a.Equal(slog.Attr{}) {
		return
	}
	if a.Value.Kind() == slog.KindGroup {
		for _, g := range a.Value.Group() {
			writeAttr(b, group+a.Key+".", g)
		}
		return
	}
	fmt.Fprintf(b, " %s%s=%v", group, a.Key, a.Value)
}
