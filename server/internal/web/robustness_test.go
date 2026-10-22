package web

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Package 7 (repo scan 2026-10-19): web robustness fixes.

// deadlineWriter records the write deadlines a handler sets on the connection.
type deadlineWriter struct {
	*httptest.ResponseRecorder
	mu        sync.Mutex
	deadlines []time.Time
}

func (d *deadlineWriter) SetWriteDeadline(t time.Time) error {
	d.mu.Lock()
	d.deadlines = append(d.deadlines, t)
	d.mu.Unlock()
	return nil
}

func (d *deadlineWriter) snapshot() []time.Time {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]time.Time(nil), d.deadlines...)
}

// HA-13: web downloads renew a write deadline while they stream and clear it afterwards.
func TestWebDownloadsHaveWriteDeadline(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	admin, err := e.svc.VerifyPassword(bg, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := e.svc.AddROM(bg, bytes.NewReader(bytes.Repeat([]byte("rom"), 5000)), "dl.nds", "Download", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	r := httptest.NewRequest("GET", "/library/download/"+g.ID, nil)
	r.RemoteAddr = c.remote
	for k, v := range c.cookies {
		r.AddCookie(&http.Cookie{Name: k, Value: v})
	}
	w := &deadlineWriter{ResponseRecorder: httptest.NewRecorder()}
	e.mux.ServeHTTP(w, r)
	if w.Code != 200 || w.Body.Len() != 15000 {
		t.Fatalf("download: %d, %d bytes", w.Code, w.Body.Len())
	}
	dl := w.snapshot()
	if len(dl) < 2 || dl[0].Before(time.Now()) || !dl[len(dl)-1].IsZero() {
		t.Fatalf("deadlines %v: want a future deadline first and a cleared one last", dl)
	}
}

// HA-13: the event stream sets a deadline for each write and clears it when it ends.
func TestEventStreamHasWriteDeadlines(t *testing.T) {
	e := newEnv(t, true, nil)
	w, err := New(e.svc, e.cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	dw := &deadlineWriter{ResponseRecorder: httptest.NewRecorder()}
	r := httptest.NewRequest("GET", "/events", nil).WithContext(ctx)
	done := make(chan struct{})
	go func() { w.serveEvents(dw, r, 20*time.Millisecond, time.Second); close(done) }()
	time.Sleep(150 * time.Millisecond) // several keepalives
	cancel()
	<-done
	dl := dw.snapshot()
	if len(dl) < 3 {
		t.Fatalf("%d deadlines set for a stream with keepalives", len(dl))
	}
	for _, d := range dl[:len(dl)-1] {
		if d.IsZero() {
			t.Fatal("deadline cleared while the stream was running")
		}
	}
	if !dl[len(dl)-1].IsZero() {
		t.Fatal("deadline not cleared when the stream ended")
	}
}

// R-H23: below 600 px the Saves slots and versions are cards; the desktop rules stay outside that media query.
func TestSavesCardsBelow600px(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	// Windows checkouts may convert the embedded file to CRLF.
	css := strings.ReplaceAll(c.get("/static/app.css", nil).Body.String(), "\r\n", "\n")
	i := strings.Index(css, "@media (max-width: 599px) {\n  .save-list")
	if i < 0 {
		t.Fatal("no card block for the Saves page below 600 px")
	}
	block := css[i:]
	for _, want := range []string{".timeline li {", "border: 1px solid var(--border)", ".tab {", ".timeline li::before, .timeline .node { display: none; }"} {
		if !strings.Contains(block, want) {
			t.Fatalf("card block lacks %q", want)
		}
	}
	// The uploader sends the file hash and a streaming SHA-256 is part of the script (HA-4).
	js := c.get("/static/libupload.js", nil).Body.String()
	for _, want := range []string{"Sha256.prototype.update", "sha256: sha"} {
		if !strings.Contains(js, want) {
			t.Fatalf("libupload.js lacks %q", want)
		}
	}
}

// HA-5: a database error before the export starts is an HTTP 500, not a truncated file.
func TestRunExportDatabaseErrorIs500(t *testing.T) {
	f := newRunsFixture(t)
	if _, err := f.db.Exec(`ALTER TABLE run_events RENAME TO run_events_gone`); err != nil {
		t.Fatal(err)
	}
	rec := f.c.get("/runs/"+f.run.ID+"/export", nil)
	if rec.Code != 500 || strings.Contains(rec.Body.String(), `"events"`) {
		t.Fatalf("%d: %s", rec.Code, rec.Body.String())
	}
}

// HA-5: an export that cannot be completed after it started says so in the document (an "error" member).
func TestRunExportIncompleteIsMarked(t *testing.T) {
	f := newRunsFixture(t)
	for i := 0; i < 4; i++ {
		if _, _, err := f.svc.AppendRunEvent(bg, hub.AppendRunEventInput{UserID: f.admin.ID, DeviceID: "d", RunID: f.run.ID,
			ClientID: "0b8e4f6a-1c2d-4e3f-9a4b-5c6d7e8f9a0" + string(rune('a'+i)), PluginID: "net.example.board", Source: "manual", Type: "tick", Text: "t"}); err != nil {
			t.Fatal(err)
		}
	}
	// Events vanish while last_seq stays: the log ends before the last sequence number.
	if _, err := f.db.Exec(`DELETE FROM run_events WHERE run_id = ? AND seq > 3`, f.run.ID); err != nil {
		t.Fatal(err)
	}
	rec := f.c.get("/runs/"+f.run.ID+"/export", nil)
	status(t, rec, 200)
	var doc struct {
		LastSeq int64             `json:"last_seq"`
		Events  []json.RawMessage `json:"events"`
		Error   string            `json:"error"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &doc); err != nil {
		t.Fatalf("not valid JSON: %v: %s", err, rec.Body.String())
	}
	if doc.Error == "" || !strings.Contains(doc.Error, "incomplete") || len(doc.Events) != 3 || doc.LastSeq <= 3 {
		t.Fatalf("%+v", doc)
	}
	// A complete export has no error member.
	g := newRunsFixture(t)
	rec = g.c.get("/runs/"+g.run.ID+"/export", nil)
	if strings.Contains(rec.Body.String(), `"error"`) {
		t.Fatalf("complete export carries an error member: %s", rec.Body.String())
	}
}
