package httpapi

import (
	"archive/zip"
	"bytes"
	"context"
	"fmt"
	"io"
	"mime/multipart"
	"net/textproto"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
)

type reportFile struct{ name, body string }

// multipartReport builds the multipart body of POST /reports: the meta part first, then the files.
func multipartReport(t *testing.T, meta string, files ...reportFile) (data []byte, ctype string) {
	t.Helper()
	var buf bytes.Buffer
	mw := multipart.NewWriter(&buf)
	if meta != "" {
		h := textproto.MIMEHeader{}
		h.Set("Content-Disposition", `form-data; name="meta"`)
		h.Set("Content-Type", "application/json")
		w, _ := mw.CreatePart(h)
		io.WriteString(w, meta)
	}
	for _, f := range files {
		h := textproto.MIMEHeader{}
		h.Set("Content-Disposition", fmt.Sprintf(`form-data; name="files"; filename=%q`, f.name))
		h.Set("Content-Type", "application/octet-stream")
		w, _ := mw.CreatePart(h)
		io.WriteString(w, f.body)
	}
	mw.Close()
	return buf.Bytes(), mw.FormDataContentType()
}

// reportEnv is a server with an admin and a member, each with a device token.
func reportEnv(t *testing.T) (e *env, adminTok, memberTok string) {
	t.Helper()
	e = newEnv(t, nil)
	adminTok = e.login(uuid.NewString())
	anna, err := e.svc.CreateUser(context.Background(), "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	admin := e.admin
	e.admin = anna
	memberTok = e.login(uuid.NewString())
	e.admin = admin
	return
}

const goodMeta = `{"description":"The Session froze after four hours.\nSecond line. password=hunter2hunter2","linked_event":{"title":"Session ended unexpectedly","time":"2026-10-22T18:41:00Z"},"player_version":"0.16.4","os":"Windows 11"}`

// reportDirs counts the report directories (and staging leftovers) in the data directory.
func reportDirs(e *env) int {
	es, _ := os.ReadDir(filepath.Join(e.svc.DataDir(), "reports"))
	return len(es)
}

func TestReportsLifecycle(t *testing.T) {
	e, admin, member := reportEnv(t)
	data, ct := multipartReport(t, goodMeta,
		reportFile{"player-logs.txt", "level=INFO msg=ok\nAuthorization: Bearer abcdefghijklmnop\n"},
		reportFile{"connection-check.json", `{"ok":true,"token":"fba_secretsecretsecret"}`},
		reportFile{"blob.bin", "token=raw-bytes-stay-as-sent"})
	rec := e.do("POST", "/api/v1/reports", nil, opt{token: member, data: data, ctype: ct})
	wantStatus(t, rec, 201, "")
	id := decode[struct{ ID int64 }](t, rec).ID
	if id < 1 {
		t.Fatalf("id %d", id)
	}

	// A member cannot list or read; without a token it is 401.
	for _, p := range []string{"/api/v1/admin/reports", fmt.Sprintf("/api/v1/admin/reports/%d", id), fmt.Sprintf("/api/v1/admin/reports/%d/archive", id)} {
		wantStatus(t, e.do("GET", p, nil, opt{token: member}), 403, "forbidden")
		wantStatus(t, e.do("GET", p, nil, opt{}), 401, "")
	}
	wantStatus(t, e.do("POST", fmt.Sprintf("/api/v1/admin/reports/%d/seen", id), nil, opt{token: member}), 403, "forbidden")
	wantStatus(t, e.do("DELETE", fmt.Sprintf("/api/v1/admin/reports/%d", id), nil, opt{token: member}), 403, "forbidden")
	wantStatus(t, e.do("POST", "/api/v1/reports", nil, opt{data: data, ctype: ct}), 401, "")

	// The admin sees it, new; the counts cover all reports.
	rec = e.do("GET", "/api/v1/admin/reports", nil, opt{token: admin})
	wantStatus(t, rec, 200, "")
	list := decode[struct {
		Reports []struct {
			ID                 int64
			Status, Title      string
			User, Device       string
			FileCount          int  `json:"file_count"`
			HasConnectionCheck bool `json:"has_connection_check"`
			Size               int64
		}
	}](t, rec)
	if len(list.Reports) != 1 || list.Reports[0].Status != "new" || list.Reports[0].User != "Anna" || list.Reports[0].FileCount != 3 ||
		!list.Reports[0].HasConnectionCheck || list.Reports[0].Title != "The Session froze after four hours." {
		t.Fatalf("list %+v", list)
	}

	// Opening does not mark it seen; the description is redacted.
	rec = e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d", id), nil, opt{token: admin})
	wantStatus(t, rec, 200, "")
	body := rec.Body.String()
	if !strings.Contains(body, `"status":"new"`) || strings.Contains(body, "hunter2hunter2") || !strings.Contains(body, "[redacted]") ||
		!strings.Contains(body, "Session ended unexpectedly") || !strings.Contains(body, "0.16.4") {
		t.Fatalf("report %s", body)
	}
	rec = e.do("GET", "/api/v1/admin/reports?status=new", nil, opt{token: admin})
	if !strings.Contains(rec.Body.String(), `"count_new":1`) {
		t.Fatalf("still new: %s", rec.Body.String())
	}

	// Text attachments are redacted again; other files are stored as sent.
	rec = e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d/files/player-logs.txt", id), nil, opt{token: admin})
	wantStatus(t, rec, 200, "")
	if s := rec.Body.String(); !strings.Contains(s, "msg=ok") || strings.Contains(s, "abcdefghijklmnop") {
		t.Fatalf("log %q", s)
	}
	if !strings.Contains(rec.Header().Get("Content-Disposition"), `filename="player-logs.txt"`) {
		t.Fatalf("headers %v", rec.Header())
	}
	rec = e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d/files/connection-check.json", id), nil, opt{token: admin})
	if strings.Contains(rec.Body.String(), "secretsecret") {
		t.Fatalf("json %q", rec.Body.String())
	}
	rec = e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d/files/blob.bin", id), nil, opt{token: admin})
	if rec.Body.String() != "token=raw-bytes-stay-as-sent" {
		t.Fatalf("bin %q", rec.Body.String())
	}
	wantStatus(t, e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d/files/..%%2F..%%2Fuzume.db", id), nil, opt{token: admin, raw: true}), 404, "")
	wantStatus(t, e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d/files/nope.txt", id), nil, opt{token: admin}), 404, "not_found")

	// The archive holds report.txt and all files.
	rec = e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d/archive", id), nil, opt{token: admin})
	wantStatus(t, rec, 200, "")
	zr, err := zip.NewReader(bytes.NewReader(rec.Body.Bytes()), int64(rec.Body.Len()))
	if err != nil {
		t.Fatal(err)
	}
	got := map[string]string{}
	for _, f := range zr.File {
		r, _ := f.Open()
		b, _ := io.ReadAll(r)
		r.Close()
		got[f.Name] = string(b)
	}
	if len(got) != 4 || !strings.Contains(got["report.txt"], "From: Anna") || !strings.Contains(got["report.txt"], "froze after four hours") ||
		got["blob.bin"] != "token=raw-bytes-stay-as-sent" {
		t.Fatalf("archive %v", got)
	}

	// Mark as seen, then delete.
	rec = e.do("POST", fmt.Sprintf("/api/v1/admin/reports/%d/seen", id), nil, opt{token: admin})
	wantStatus(t, rec, 200, "")
	if !strings.Contains(rec.Body.String(), `"status":"seen"`) || !strings.Contains(rec.Body.String(), `"seen_at"`) {
		t.Fatalf("seen %s", rec.Body.String())
	}
	rec = e.do("GET", "/api/v1/admin/reports?status=new", nil, opt{token: admin})
	if !strings.Contains(rec.Body.String(), `"count_seen":1`) || !strings.Contains(rec.Body.String(), `"reports":[]`) {
		t.Fatalf("filtered %s", rec.Body.String())
	}
	wantStatus(t, e.do("DELETE", fmt.Sprintf("/api/v1/admin/reports/%d", id), nil, opt{token: admin}), 204, "")
	wantStatus(t, e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d", id), nil, opt{token: admin}), 404, "not_found")
	wantStatus(t, e.do("DELETE", fmt.Sprintf("/api/v1/admin/reports/%d", id), nil, opt{token: admin}), 404, "not_found")
	if n := reportDirs(e); n != 0 {
		t.Fatalf("%d report directories left", n)
	}
}

func TestReportsRefuseBadInput(t *testing.T) {
	e, _, _ := reportEnv(t)
	// A device may send 5 reports per hour, refused ones count too: every bad input comes from a fresh device.
	bad := func(meta string, files ...reportFile) int {
		data, ct := multipartReport(t, meta, files...)
		e.clk.Advance(2 * time.Minute) // the pairing limit is per minute
		tok := e.login(uuid.NewString())
		return e.do("POST", "/api/v1/reports", nil, opt{token: tok, data: data, ctype: ct, raw: true}).Code
	}
	for name, c := range map[string]int{
		"no meta":        bad("", reportFile{"a.txt", "x"}),
		"bad json":       bad("{nope"),
		"empty text":     bad(`{"description":"   "}`),
		"long text":      bad(`{"description":"` + strings.Repeat("a", hub.ReportMaxDescription+1) + `"}`),
		"hidden name":    bad(goodMeta, reportFile{".htaccess", "x"}),
		"reserved name":  bad(goodMeta, reportFile{"report.txt", "x"}),
		"duplicate name": bad(goodMeta, reportFile{"a.txt", "x"}, reportFile{"A.txt", "y"}),
		"too many files": bad(goodMeta, many(hub.ReportMaxFiles+1)...),
	} {
		if c != 400 {
			t.Errorf("%s: status %d, want 400", name, c)
		}
	}
	// Nothing was stored by the refused ones (the clock moved on: sign in again).
	admin := e.login(uuid.NewString())
	rec := e.do("GET", "/api/v1/admin/reports", nil, opt{token: admin})
	if !strings.Contains(rec.Body.String(), `"count_all":0`) {
		t.Fatalf("stored: %s", rec.Body.String())
	}
	if n := reportDirs(e); n != 0 {
		t.Fatalf("%d leftover directories", n)
	}
}

func many(n int) []reportFile {
	var fs []reportFile
	for i := 0; i < n; i++ {
		fs = append(fs, reportFile{fmt.Sprintf("f%d.txt", i), "x"})
	}
	return fs
}

func TestReportsSizeLimit(t *testing.T) {
	e, admin, member := reportEnv(t)
	big := strings.Repeat("x", hub.ReportMaxBytes/2+1)
	data, ct := multipartReport(t, goodMeta, reportFile{"a.bin", big}, reportFile{"b.bin", big})
	rec := e.do("POST", "/api/v1/reports", nil, opt{token: member, data: data, ctype: ct, raw: true})
	wantStatus(t, rec, 413, "payload_too_large")
	// Exactly at the limit is accepted.
	half := strings.Repeat("y", hub.ReportMaxBytes/2)
	data, ct = multipartReport(t, goodMeta, reportFile{"a.bin", half}, reportFile{"b.bin", half})
	rec = e.do("POST", "/api/v1/reports", nil, opt{token: member, data: data, ctype: ct})
	wantStatus(t, rec, 201, "")
	rec = e.do("GET", "/api/v1/admin/reports", nil, opt{token: admin})
	if !strings.Contains(rec.Body.String(), `"count_all":1`) {
		t.Fatalf("only the accepted report is stored: %s", rec.Body.String())
	}
	if n := reportDirs(e); n != 1 {
		t.Fatalf("%d directories, want 1 (the refused upload leaves nothing)", n)
	}
}

func TestReportsRatePerDevice(t *testing.T) {
	e, _, member := reportEnv(t)
	data, ct := multipartReport(t, goodMeta)
	for i := 0; i < hub.ReportsPerDevicePerHr; i++ {
		wantStatus(t, e.do("POST", "/api/v1/reports", nil, opt{token: member, data: data, ctype: ct}), 201, "")
	}
	wantStatus(t, e.do("POST", "/api/v1/reports", nil, opt{token: member, data: data, ctype: ct}), 429, "rate_limited")
	// Another device is not affected.
	other := e.login(uuid.NewString())
	wantStatus(t, e.do("POST", "/api/v1/reports", nil, opt{token: other, data: data, ctype: ct}), 201, "")
}

// A client-supplied path is reduced to its base name; nothing is written outside the report directory.
func TestReportsFileNameIsReducedToBase(t *testing.T) {
	e, admin, member := reportEnv(t)
	data, ct := multipartReport(t, goodMeta, reportFile{"../../escape.db", "x"})
	rec := e.do("POST", "/api/v1/reports", nil, opt{token: member, data: data, ctype: ct})
	wantStatus(t, rec, 201, "")
	id := decode[struct{ ID int64 }](t, rec).ID
	rec = e.do("GET", fmt.Sprintf("/api/v1/admin/reports/%d", id), nil, opt{token: admin})
	if !strings.Contains(rec.Body.String(), `"name":"escape.db"`) {
		t.Fatalf("report %s", rec.Body.String())
	}
	if _, err := os.Stat(filepath.Join(e.svc.DataDir(), "escape.db")); err == nil {
		t.Fatal("a file escaped the report directory")
	}
}
