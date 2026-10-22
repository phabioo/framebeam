package web

import (
	"archive/zip"
	"bytes"
	"io"
	"net/url"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

type testFiles struct {
	names, bodies []string
	i             int
}

func (f *testFiles) Next() (string, io.Reader, error) {
	if f.i >= len(f.names) {
		return "", nil, io.EOF
	}
	f.i++
	return f.names[f.i-1], strings.NewReader(f.bodies[f.i-1]), nil
}

func seedReport(t *testing.T, e *env, device, desc string, names ...string) int64 {
	t.Helper()
	p := hub.Principal{Device: hub.Device{ID: device, Name: device}, User: hub.User{ID: "u-" + device, DisplayName: "Max"}}
	bodies := make([]string, len(names))
	for i := range names {
		bodies[i] = "content of " + names[i]
	}
	id, err := e.svc.CreateReport(bg, p, hub.ReportInput{Description: desc, EventTitle: "Session ended unexpectedly",
		EventTime: ptrTime(time.Date(2026, 10, 22, 18, 41, 0, 0, time.UTC)), PlayerVersion: "0.16.4", OS: "Windows 11"},
		&testFiles{names: names, bodies: bodies})
	if err != nil {
		t.Fatal(err)
	}
	return id
}

func ptrTime(t time.Time) *time.Time { return &t }

func TestReportsPageAdminOnlyEmptyAndBadge(t *testing.T) {
	e := newEnv(t, true, nil)
	anon := e.client()
	if rec := anon.get("/reports", nil); rec.Code != 303 || location(rec) != "/login" {
		t.Fatalf("anonymous: %d %q", rec.Code, location(rec))
	}
	c := e.client()
	c.login()
	rec := c.get("/reports", nil)
	status(t, rec, 200)
	contains(t, rec, "No reports yet", "Settings → Diagnostics → Report a problem", `href="/reports"`)
	notContains(t, rec, " new</span>") // no badge without new reports

	seedReport(t, e, "d1", "It froze.", "player-logs.txt")
	seedReport(t, e, "d2", "It crashed.")
	rec = c.get("/library", nil)
	contains(t, rec, `<span class="badge info">2 new</span>`)
}

func TestReportsListDetailSeenDelete(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	csrf := c.login()
	id1 := seedReport(t, e, "d1", "Session froze after 4 hours\nsecond line", "player-logs.txt", "connection-check.json")
	id2 := seedReport(t, e, "d2", "Second problem")

	// The newest report is selected by default; opening does not mark anything seen.
	rec := c.get("/reports", nil)
	status(t, rec, 200)
	contains(t, rec, "All · 2", "New · 2", "Seen · 0", "Second problem", "Session froze after 4 hours", "Mark as seen", "Delete report…")
	rec = c.get("/reports/"+rid(id1), nil)
	status(t, rec, 200)
	contains(t, rec, "Session froze after 4 hours", "Max · d1", "0.16.4 · Windows 11", "Session ended unexpectedly",
		"player-logs.txt", "connection-check.json", "/reports/"+rid(id1)+"/files/player-logs.txt", "Download all (.zip)",
		"Opening a report does not mark it seen")
	if n, _ := e.svc.NewReportCount(bg); n != 2 {
		t.Fatalf("opening marked a report seen: %d new", n)
	}

	// Files and the archive.
	rec = c.get("/reports/"+rid(id1)+"/files/player-logs.txt", nil)
	status(t, rec, 200)
	if rec.Body.String() != "content of player-logs.txt" || !strings.Contains(rec.Header().Get("Content-Disposition"), `filename="player-logs.txt"`) {
		t.Fatalf("file %q %v", rec.Body.String(), rec.Header())
	}
	status(t, c.get("/reports/"+rid(id1)+"/files/missing.txt", nil), 404)
	status(t, c.get("/reports/"+rid(id1)+"/files/..%2Fx", nil), 404)
	rec = c.get("/reports/"+rid(id1)+"/archive", nil)
	status(t, rec, 200)
	zr, err := zip.NewReader(bytes.NewReader(rec.Body.Bytes()), int64(rec.Body.Len()))
	if err != nil || len(zr.File) != 3 {
		t.Fatalf("zip %v %v", err, zr)
	}

	// Mark as seen needs the CSRF token.
	status(t, c.postForm("/reports/"+rid(id1)+"/seen", url.Values{}, nil), 403)
	rec = c.postForm("/reports/"+rid(id1)+"/seen", url.Values{"_csrf": {csrf}}, nil)
	status(t, rec, 303)
	if n, _ := e.svc.NewReportCount(bg); n != 1 {
		t.Fatalf("%d new, want 1", n)
	}
	rec = c.get("/reports?filter=new", nil)
	contains(t, rec, "Second problem", "New · 1", "Seen · 1")
	notContains(t, rec, "Session froze after 4 hours")
	rec = c.get("/reports?filter=seen", nil)
	contains(t, rec, "Session froze after 4 hours")
	notContains(t, rec, "Second problem")

	// Delete: confirmation first, then POST with the token.
	rec = c.get("/reports/"+rid(id2)+"/delete", nil)
	status(t, rec, 200)
	contains(t, rec, "Delete report #"+rid(id2)+"?", "Second problem")
	status(t, c.postForm("/reports/"+rid(id2)+"/delete", url.Values{}, nil), 403)
	status(t, c.postForm("/reports/"+rid(id2)+"/delete", url.Values{"_csrf": {csrf}}, nil), 303)
	status(t, c.get("/reports/"+rid(id2), nil), 303)
	_, counts, _ := e.svc.ListReports(bg, "")
	if counts.All != 1 {
		t.Fatalf("counts %+v", counts)
	}
}

func rid(n int64) string { return strconv.FormatInt(n, 10) }
