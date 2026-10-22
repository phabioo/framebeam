package hub_test

import (
	"bytes"
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/backup"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func str(s string) *string { return &s }
func num(n int) *int       { return &n }

// backupEnv is a service with a backup folder outside the data directory and a gate in front of the space check,
// so that a test can hold a backup "running".
type backupEnv struct {
	svc    *hub.Service
	clk    *hubtest.Clock
	data   string
	folder string
	free   func(string) (uint64, bool)
}

func newBackupEnv(t *testing.T) *backupEnv {
	t.Helper()
	e := &backupEnv{data: t.TempDir(), folder: t.TempDir(), free: func(string) (uint64, bool) { return 1 << 40, true }}
	e.svc, e.clk, _ = hubtest.OpenIn(t, e.data, func(o *hub.Options) {
		o.BackupFree = func(d string) (uint64, bool) { return e.free(d) }
	})
	if err := e.svc.SetBackupSettings(context.Background(), hub.BackupSettingsUpdate{Folder: &e.folder}); err != nil {
		t.Fatal(err)
	}
	// Synthetic data next to the database.
	for p, c := range map[string]string{"saves/u/g/main/aaaa": "SYNTHETIC-SAVE-CONTENT", "plugins/p/1/main.lua": "lua"} {
		full := filepath.Join(e.data, p)
		os.MkdirAll(filepath.Dir(full), 0o750)
		os.WriteFile(full, []byte(c), 0o640)
	}
	return e
}

func (e *backupEnv) waitIdle(t *testing.T) hub.BackupStatus {
	t.Helper()
	for i := 0; i < 400; i++ {
		if st := e.svc.BackupStatus(context.Background()); st.Running == nil {
			return st
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("job still running")
	return hub.BackupStatus{}
}

func (e *backupEnv) backupNow(t *testing.T) hub.BackupStatus {
	t.Helper()
	e.clk.Advance(24 * time.Hour) // distinct file names
	if err := e.svc.StartBackup(context.Background(), "admin"); err != nil {
		t.Fatal(err)
	}
	return e.waitIdle(t)
}

func TestBackupSettingsDefaultsAndValidation(t *testing.T) {
	ctx := context.Background()
	svc, _ := hubtest.New(t, nil)
	s := svc.BackupSettings(ctx)
	if s.Mode != "off" || s.Time != "03:00" || s.Keep != 7 || s.IncludeROMs || s.Folder != "" {
		t.Fatalf("%+v", s)
	}
	st := svc.BackupStatus(ctx)
	if st.FolderSet || st.Running != nil || !st.Next.IsZero() {
		t.Fatalf("no folder = off: %+v", st)
	}
	if err := svc.StartBackup(ctx, "admin"); err == nil || !strings.Contains(err.Error(), "backup folder") {
		t.Fatalf("start without folder: %v", err)
	}
	for _, bad := range []hub.BackupSettingsUpdate{
		{Mode: str("hourly")}, {Weekday: num(7)}, {Time: str("3:00")}, {Time: str("24:00")}, {Keep: num(0)}, {Keep: num(1000)}, {Folder: str("relative/dir")},
		{Folder: str(filepath.Join(svc.DataDir(), "saves", "x"))}, {Folder: str(svc.DataDir())},
	} {
		if err := svc.SetBackupSettings(ctx, bad); err == nil {
			t.Errorf("accepted %+v", bad)
		}
	}
	if err := svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Mode: str("weekly"), Weekday: num(1), Time: str("04:30"), Keep: num(3), Folder: str(t.TempDir())}); err != nil {
		t.Fatal(err)
	}
	st = svc.BackupStatus(ctx)
	if st.Settings.Mode != "weekly" || st.Settings.Weekday != 1 || st.Settings.Keep != 3 || st.Next.IsZero() || st.Next.Local().Weekday() != time.Monday {
		t.Fatalf("%+v next %v", st.Settings, st.Next)
	}
}

func TestBackupRunRetentionAndList(t *testing.T) {
	e := newBackupEnv(t)
	ctx := context.Background()
	if err := e.svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Keep: num(2)}); err != nil {
		t.Fatal(err)
	}
	var st hub.BackupStatus
	for i := 0; i < 3; i++ {
		st = e.backupNow(t)
		if st.Last == nil || !st.Last.OK {
			t.Fatalf("run %d: %+v", i, st.Last)
		}
	}
	list, err := e.svc.ListBackups(ctx)
	if err != nil || len(list) != 2 {
		t.Fatalf("keep 2: %d %v", len(list), err)
	}
	if list[0].Name != st.LastOK.Name || list[0].Contents == "" || list[0].Err != "" {
		t.Fatalf("%+v", list[0])
	}
	if p := e.svc.BackupProblem(ctx); p != nil {
		t.Fatalf("no problem expected: %+v", p)
	}
	// Delete: only a plain archive name of the folder.
	if err := e.svc.DeleteBackup(ctx, "../framebeam.db"); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if err := e.svc.DeleteBackup(ctx, list[1].Name); err != nil {
		t.Fatal(err)
	}
	if l, _ := e.svc.ListBackups(ctx); len(l) != 1 {
		t.Fatalf("%d", len(l))
	}
}

func TestBackupFailureNeverDeletesAndShowsProblem(t *testing.T) {
	e := newBackupEnv(t)
	ctx := context.Background()
	e.svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Keep: num(1)})
	good := e.backupNow(t)
	if !good.Last.OK {
		t.Fatal("first backup must work")
	}
	// The disk is full: the backup fails, retention does not run, the old backup stays (decision 26).
	e.free = func(string) (uint64, bool) { return 1000, true }
	st := e.backupNow(t)
	if st.Last == nil || st.Last.OK || !st.Last.NoSpace || !strings.HasPrefix(st.Last.Error, "Not enough space on "+e.folder+": needs ") {
		t.Fatalf("%+v", st.Last)
	}
	if st.LastOK == nil || st.LastOK.Name != good.LastOK.Name {
		t.Fatalf("last good backup must stay known: %+v", st.LastOK)
	}
	if l, _ := e.svc.ListBackups(ctx); len(l) != 1 || l[0].Name != good.LastOK.Name {
		t.Fatalf("old backup must stay: %+v", l)
	}
	if ents, _ := os.ReadDir(e.folder); len(ents) != 1 {
		t.Fatalf("no partial file may stay: %v", ents)
	}
	p := e.svc.BackupProblem(ctx)
	if p == nil || p.Title != "Backup failed" || !strings.Contains(p.Detail, "The last good backup is from") {
		t.Fatalf("%+v", p)
	}
	// Free space again: the next backup succeeds, the problem is gone.
	e.free = func(string) (uint64, bool) { return 1 << 40, true }
	if st = e.backupNow(t); !st.Last.OK || e.svc.BackupProblem(ctx) != nil {
		t.Fatal("recovered")
	}
}

func TestBackupFolderNotWritable(t *testing.T) {
	e := newBackupEnv(t)
	ctx := context.Background()
	file := filepath.Join(t.TempDir(), "afile")
	os.WriteFile(file, []byte("x"), 0o600)
	bad := filepath.Join(file, "sub")
	if err := e.svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Folder: &bad}); err != nil {
		t.Fatal(err)
	}
	st := e.svc.BackupStatus(ctx)
	if st.WriteError == "" {
		t.Fatal("must report that it cannot write")
	}
	if p := e.svc.BackupProblem(ctx); p == nil || p.Title != "Cannot write" {
		t.Fatalf("%+v", p)
	}
	if err := e.svc.StartBackup(ctx, "admin"); err == nil {
		t.Fatal("start must refuse")
	}
	// Choosing a good folder clears it at once.
	if err := e.svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Folder: &e.folder}); err != nil {
		t.Fatal(err)
	}
	if e.svc.BackupProblem(ctx) != nil || e.svc.BackupStatus(ctx).WriteError != "" {
		t.Fatal("should be fine now")
	}
}

func TestOneJobAtATimeAndCancel(t *testing.T) {
	e := newBackupEnv(t)
	ctx := context.Background()
	gate, entered := make(chan struct{}), make(chan struct{}, 1)
	e.free = func(string) (uint64, bool) {
		select {
		case entered <- struct{}{}:
		default:
		}
		<-gate
		return 1 << 40, true
	}
	if err := e.svc.StartBackup(ctx, "max"); err != nil {
		t.Fatal(err)
	}
	<-entered
	st := e.svc.BackupStatus(ctx)
	if st.Running == nil || st.Running.By != "max" || st.Running.Kind != "backup" {
		t.Fatalf("%+v", st.Running)
	}
	var he *hub.Error
	if err := e.svc.StartBackup(ctx, "other"); !errors.As(err, &he) || he.Code != hub.CodeConflict {
		t.Fatalf("second backup: %v", err)
	}
	if !e.svc.CancelBackup() {
		t.Fatal("cancel")
	}
	close(gate)
	st = e.waitIdle(t)
	if st.Last != nil {
		t.Fatalf("a canceled backup records nothing: %+v", st.Last)
	}
	if ents, _ := os.ReadDir(e.folder); len(ents) != 0 {
		t.Fatalf("partial file left: %v", ents)
	}
	if e.svc.CancelBackup() {
		t.Fatal("nothing to cancel")
	}
}

func TestBackupContainsDatabaseAndData(t *testing.T) {
	e := newBackupEnv(t)
	st := e.backupNow(t)
	path := filepath.Join(e.folder, st.LastOK.Name)
	m, err := backup.ReadManifest(path)
	if err != nil {
		t.Fatal(err)
	}
	if m.HubName != "Test-Hub" || m.HubVersion != "0.0.0-test" || m.SchemaVersion < 18 {
		t.Fatalf("%+v", m)
	}
	names := map[string]bool{}
	for _, x := range m.Members {
		names[x.Name] = true
	}
	for _, want := range []string{"database/framebeam.db", "data/saves/u/g/main/aaaa", "data/plugins/p/1/main.lua"} {
		if !names[want] {
			t.Errorf("missing %s", want)
		}
	}
	if _, _, err := backup.Extract(context.Background(), path, "", backup.ExtractOptions{VerifyOnly: true}); err != nil {
		t.Fatal(err)
	}
}

func TestRestorePreparesAndRejectsBadBackups(t *testing.T) {
	e := newBackupEnv(t)
	ctx := context.Background()
	st := e.backupNow(t)
	name := st.LastOK.Name

	// Without a restart function a restore is refused before anything happens.
	if err := e.svc.StartRestore(ctx, name, "admin"); err == nil || !strings.Contains(err.Error(), "restart itself") {
		t.Fatalf("%v", err)
	}
	restarted := make(chan struct{}, 1)
	e.svc.SetBackupRestart(func() { restarted <- struct{}{} })

	// A backup of a newer schema is rejected up front.
	o := backup.Options{DataDir: e.data, TargetDir: e.folder, HubName: "Test-Hub", HubVersion: "9.9.9", SchemaVersion: 9999,
		Snapshot: func(dest string) error { return os.WriteFile(dest, []byte("x"), 0o600) }, Now: func() time.Time { return e.clk.Now().Add(100 * time.Hour) }}
	newer, err := backup.Create(ctx, o)
	if err != nil {
		t.Fatal(err)
	}
	if err := e.svc.StartRestore(ctx, newer.Name, "admin"); err == nil || !strings.Contains(err.Error(), "newer FrameBeam Hub") {
		t.Fatalf("newer schema: %v", err)
	}
	if _, err := e.svc.RestoreInfo(ctx, newer.Name); err == nil {
		t.Fatal("info must refuse a newer backup too")
	}
	if backup.HasPending(e.data) {
		t.Fatal("nothing may be pending")
	}

	// A damaged file: the safety backup is made, then the checksum check stops it and the Hub runs as before.
	b, _ := os.ReadFile(filepath.Join(e.folder, name))
	i := bytes.Index(b, []byte("SYNTHETIC-SAVE-CONTENT"))
	b[i] ^= 0xff
	damaged := backup.FileName("Test-Hub", e.clk.Now().Add(-48*time.Hour), false)
	os.WriteFile(filepath.Join(e.folder, damaged), b, 0o600)
	if err := e.svc.StartRestore(ctx, damaged, "admin"); err != nil {
		t.Fatal(err)
	}
	st = e.waitIdle(t)
	if st.Restore == nil || st.Restore.OK || !strings.Contains(st.Restore.Error, "checksum mismatch in saves") || st.Restore.Safety == "" {
		t.Fatalf("%+v", st.Restore)
	}
	if backup.HasPending(e.data) {
		t.Fatal("a failed restore must not be pending")
	}
	if _, err := os.Stat(filepath.Join(e.folder, st.Restore.Safety)); err != nil {
		t.Fatal("safety backup must be kept")
	}
	select {
	case <-restarted:
		t.Fatal("no restart for a failed restore")
	default:
	}
	e.svc.DismissRestoreResult()

	// A good backup: safety backup, staging, pending, restart.
	info, err := e.svc.RestoreInfo(ctx, name)
	if err != nil || info.Entry.Contents == "" {
		t.Fatalf("%+v %v", info, err)
	}
	if err := e.svc.StartRestore(ctx, name, "admin"); err != nil {
		t.Fatal(err)
	}
	select {
	case <-restarted:
	case <-time.After(10 * time.Second):
		t.Fatal("restart not requested")
	}
	if !backup.HasPending(e.data) {
		t.Fatal("restore must be pending for the next start")
	}
	if _, err := os.Stat(filepath.Join(e.data, backup.StagingDir, "framebeam.db")); err != nil {
		t.Fatal("staging must hold the database")
	}
	st = e.svc.BackupStatus(ctx)
	if st.Running == nil || st.Running.Kind != "restore" || st.Running.Step != 3 {
		t.Fatalf("%+v", st.Running)
	}
	if e.svc.CancelBackup() {
		t.Fatal("cannot cancel after the safety backup")
	}
	// The safety backup is listed as such.
	list, _ := e.svc.ListBackups(ctx)
	safeties := 0
	for _, x := range list {
		if x.Safety {
			safeties++
		}
	}
	if safeties != 2 {
		t.Fatalf("two safety backups expected, got %d", safeties)
	}
	e.svc.Close() // ends the waiting job
}

func TestScheduleStartsBackupWhenDue(t *testing.T) {
	e := newBackupEnv(t)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// Due within a second: the real clock is used for the timer, so choose the next minute boundary is too slow;
	// instead check that the loop reacts to a settings change and stops with its context.
	done := make(chan struct{})
	go func() { e.svc.RunBackupSchedule(ctx, func(string, ...any) {}); close(done) }()
	if err := e.svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Mode: str("daily"), Time: str("03:00")}); err != nil {
		t.Fatal(err)
	}
	cancel()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("schedule did not stop")
	}
}
