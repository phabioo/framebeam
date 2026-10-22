package web

import (
	"github.com/phabioo/uzume/server/internal/health"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

func waitBackupIdle(t *testing.T, e *env) {
	t.Helper()
	for i := 0; i < 400; i++ {
		if e.svc.BackupStatus(bg).Running == nil {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("backup still running")
}

func TestBackupsPageAdminOnlyAndCSRF(t *testing.T) {
	e := newEnv(t, true, nil)
	anon := e.client()
	status(t, anon.get("/settings/backups", nil), 303)
	status(t, anon.get("/settings/backups/download?file=x", nil), 303)

	c := e.client()
	tok := c.login()
	rec := c.get("/settings/backups", nil)
	status(t, rec, 200)
	contains(t, rec, "Backups", "Choose a backup folder to turn backups on", "Choose a backup folder first")
	notContains(t, rec, "Back up now")

	// Every POST needs the CSRF token.
	for _, p := range []string{"schedule", "folder", "run", "cancel", "recheck", "dismiss", "delete", "restore"} {
		status(t, c.postForm("/settings/backups/"+p, url.Values{"folder": {t.TempDir()}}, nil), 403)
	}
	if got := e.svc.BackupSettings(bg); got.Folder != "" {
		t.Fatalf("a request without token changed the settings: %+v", got)
	}
	// Starting without a folder is an error shown on the page, not a 500.
	rec = c.postForm("/settings/backups/run", url.Values{"_csrf": {tok}}, nil)
	status(t, rec, 400)
	contains(t, rec, "Choose a backup folder first")
}

func TestBackupsWebFlow(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	folder := t.TempDir()
	post := func(path string, v url.Values) *httptest.ResponseRecorder {
		v.Set("_csrf", tok)
		return c.postForm(path, v, nil)
	}

	// Folder and schedule.
	status(t, post("/settings/backups/folder", url.Values{"folder": {"relative"}}), 400)
	status(t, post("/settings/backups/folder", url.Values{"folder": {folder}}), 303)
	status(t, post("/settings/backups/schedule", url.Values{"mode": {"weekly"}, "weekday": {"2"}, "time": {"05:30"}, "keep": {"4"}, "roms": {"1"}}), 303)
	if s := e.svc.BackupSettings(bg); s.Mode != "weekly" || s.Weekday != 2 || s.Time != "05:30" || s.Keep != 4 || !s.IncludeROMs || s.Folder != folder {
		t.Fatalf("%+v", s)
	}
	status(t, post("/settings/backups/schedule", url.Values{"mode": {"weekly"}, "keep": {"0"}}), 400)
	rec := c.get("/settings/backups", nil)
	contains(t, rec, "Back up now", "No backup yet", `value="05:30"`, "Next ")

	// Back up now; the card follows and the list shows the file.
	status(t, post("/settings/backups/run", url.Values{}), 303)
	waitBackupIdle(t, e)
	rec = c.get("/settings/backups", nil)
	contains(t, rec, "Last backup ok", "Backups · 1", "Database · saves", "ROMs", "Download", "Restore…", "Copy file path", "Delete…")
	notContains(t, rec, "Backup failed")
	list, _ := e.svc.ListBackups(bg)
	if len(list) != 1 {
		t.Fatalf("%d", len(list))
	}
	name := list[0].Name

	// The settings column shows the state.
	contains(t, rec, "Weekly 05:30")

	// Download streams the archive; bad names are 404.
	rec = c.get("/settings/backups/download?file="+name, nil)
	status(t, rec, 200)
	if rec.Header().Get("Content-Type") != "application/x-tar" || rec.Body.Len() != int(list[0].Size) || !strings.Contains(rec.Header().Get("Content-Disposition"), name) {
		t.Fatalf("%v %d", rec.Header(), rec.Body.Len())
	}
	status(t, c.get("/settings/backups/download?file=../uzume.db", nil), 404)

	// Restore confirmation lists the consequences; this test server cannot restart itself.
	rec = c.get("/settings/backups?restore="+name, nil)
	contains(t, rec, "Restore the backup from", "A safety backup of the current state comes first", "Saves and snapshots", "cannot restart itself")
	notContains(t, rec, "Restore and restart")
	rec = post("/settings/backups/restore", url.Values{"file": {name}})
	status(t, rec, 400)
	contains(t, rec, "cannot restart itself")
	// With a restart function the button is there and the restore is prepared.
	restarted := make(chan struct{}, 1)
	e.svc.SetBackupRestart(func() { restarted <- struct{}{} })
	contains(t, c.get("/settings/backups?restore="+name, nil), "Restore and restart")
	status(t, post("/settings/backups/restore", url.Values{"file": {name}}), 303)
	select {
	case <-restarted:
	case <-time.After(10 * time.Second):
		t.Fatal("no restart requested")
	}
	rec = c.get("/settings/backups", nil)
	contains(t, rec, "Restoring · step 3 of 3", "data-restart-poll")
	e.svc.Close()
}

func TestBackupFailureShowsOnStatusAndSidebar(t *testing.T) {
	free := uint64(1 << 40)
	e := newEnvOpts(t, true, nil, func(o *hub.Options) {
		o.BackupFree = func(string) (uint64, bool) { return free, true }
	})
	c := e.client()
	tok := c.login()
	folder := t.TempDir()
	e.svc.SetBackupSettings(bg, hub.BackupSettingsUpdate{Folder: &folder})
	run := func() {
		e.clk.Advance(time.Hour)
		c.postForm("/settings/backups/run", url.Values{"_csrf": {tok}}, nil)
		waitBackupIdle(t, e)
	}
	run() // good backup first
	free = 100
	run() // disk full: fails, the old backup stays
	if l, _ := e.svc.ListBackups(bg); len(l) != 1 {
		t.Fatalf("the old backup must stay: %d", len(l))
	}
	rec := c.get("/settings/backups", nil)
	contains(t, rec, "Backup failed", "Not enough space on "+folder, "The last good backup is from", "Try again")
	srv, err := New(e.svc, e.cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	hasProblem := func(id string) *health.Problem {
		for _, p := range srv.problems(bg) {
			if p.ID == id {
				return &p
			}
		}
		return nil
	}
	if p := hasProblem("backup"); p == nil || p.Title != "Backup failed" || p.Severity != health.Warn || p.ActionHref != "/settings/backups" || !strings.Contains(p.Detail, "Not enough space") {
		t.Fatalf("%+v", p)
	}
	// (The banner and the sidebar badge show the most urgent problem; on a nearly full test disk that can be "Disk".)
	rec = c.get("/status", nil)
	contains(t, rec, "Disk · backup", "Backups →")
	// The settings column flags it amber.
	rec = c.get("/settings/updates", nil)
	contains(t, rec, "Failed")

	// Unwritable folder: "Cannot write", with a way to check again.
	file := filepath.Join(t.TempDir(), "f")
	os.WriteFile(file, []byte("x"), 0o600)
	bad := filepath.Join(file, "sub")
	e.svc.SetBackupSettings(bg, hub.BackupSettingsUpdate{Folder: &bad})
	rec = c.get("/settings/backups", nil)
	contains(t, rec, "Cannot write", "Check again")
	if p := hasProblem("backup"); p == nil || p.Title != "Cannot write" {
		t.Fatalf("%+v", p)
	}
}

func TestStatusShowsBackupDiskOffWithoutFolder(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	contains(t, c.get("/status", nil), "Disk · backup", "No backup folder is set")
}
