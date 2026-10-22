package hub_test

import (
	"crypto/ed25519"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
	"github.com/phabioo/framebeam/server/internal/store"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// setSchema sets schema_version of a published release and re-signs the index.
func (f *updFeed) setSchema(t *testing.T, version string, schema int) {
	t.Helper()
	for i := range f.rels {
		if f.rels[i].Version == version {
			f.rels[i].SchemaVersion = schema
		}
	}
	b, err := updates.Marshal(updates.Index{Schema: 1, GeneratedAt: time.Now().UTC(), Releases: f.rels})
	if err != nil {
		t.Fatal(err)
	}
	sig, _ := corepkg.Sign(b, f.seed)
	p := filepath.Join(f.dir, "updates-index.json")
	os.WriteFile(p, b, 0o644)
	os.WriteFile(p+".sig", sig, 0o644)
}

type rbEnv struct {
	*updEnv
	data string
	open func(version string) *hub.Service
}

// newRBEnv is a packaged Hub running `current` with a backup folder; open reopens the same data directory as
// another version (the Hub after going back).
func newRBEnv(t *testing.T, current string) *rbEnv {
	t.Helper()
	e := &rbEnv{updEnv: &updEnv{feed: newUpdFeed(t), reqDir: t.TempDir()}, data: t.TempDir()}
	mod := func(version string) func(o *hub.Options) {
		return func(o *hub.Options) {
			o.HubVersion, o.UpdateChannel = version, "beta"
			o.UpdateIndexURL, o.UpdateRequestDir, o.UpdatePlatform = e.feed.url(), e.reqDir, "linux-amd64"
			o.CoreTrustKeys = []ed25519.PublicKey{e.feed.pub}
			o.Executable = updates.PackagedExecutable
		}
	}
	e.svc, e.clk, _ = hubtest.OpenIn(t, e.data, mod(current))
	e.open = func(v string) *hub.Service {
		svc, _, _ := hubtest.OpenIn(t, e.data, mod(v))
		return svc
	}
	return e
}

func (e *rbEnv) version(t *testing.T, info hub.RollbackInfo, v string) hub.RollbackVersion {
	t.Helper()
	for _, x := range info.Versions {
		if x.Version == v {
			return x
		}
	}
	t.Fatalf("%s is not offered: %+v", v, info.Versions)
	return hub.RollbackVersion{}
}

func writeDBBackup(t *testing.T, data string, from, to int, ts string) string {
	t.Helper()
	os.MkdirAll(filepath.Join(data, "backups"), 0o750)
	name := "hub-" + itoa(from) + "-to-" + itoa(to) + "-" + ts + ".db"
	if err := os.WriteFile(filepath.Join(data, "backups", name), []byte("SQLite format 3\x00 test"), 0o640); err != nil {
		t.Fatal(err)
	}
	return name
}

func itoa(i int) string {
	s := ""
	for ; i > 0; i /= 10 {
		s = string(rune('0'+i%10)) + s
	}
	return s
}

func TestRollbackVersionsAndSchemaRule(t *testing.T) {
	e := newRBEnv(t, "0.4.0")
	known := store.KnownSchemaVersion()
	e.feed.add(t, "stable", "0.3.0", 1)
	e.feed.add(t, "stable", "0.2.0", 1)
	e.feed.add(t, "stable", "0.1.0", 1)
	e.feed.add(t, "stable", "0.0.9", 1)
	e.feed.add(t, "stable", "0.5.0", 1) // newer: never a rollback target
	e.feed.setSchema(t, "0.3.0", known)
	e.feed.setSchema(t, "0.2.0", known-1)
	e.feed.setSchema(t, "0.0.9", 1)
	// 0.1.0 does not say.
	bk := writeDBBackup(t, e.data, known-1, known, "20261001T090000Z")

	info, err := e.svc.RollbackVersions(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !info.CanRollback || info.Current != "0.4.0" || info.CurrentSchema != known || info.InContainer {
		t.Fatalf("%+v", info)
	}
	var got []string
	for _, v := range info.Versions {
		got = append(got, v.Version)
	}
	if strings.Join(got, ",") != "0.3.0,0.2.0,0.1.0,0.0.9" {
		t.Fatalf("versions: %v", got)
	}
	if v := e.version(t, info, "0.3.0"); v.Action != updates.SchemaKeep {
		t.Fatalf("a target that knows the schema keeps the data: %+v", v)
	}
	if v := e.version(t, info, "0.2.0"); v.Action != updates.SchemaRestore || v.DBBackup != bk || v.LostSince.IsZero() {
		t.Fatalf("an older schema restores the pre-update backup: %+v", v)
	}
	if v := e.version(t, info, "0.1.0"); v.Action != updates.SchemaRefuse || !strings.Contains(v.Reason, "does not say") {
		t.Fatalf("unknown schema is refused: %+v", v)
	}
	if v := e.version(t, info, "0.0.9"); v.Action != updates.SchemaRefuse || !strings.Contains(v.Reason, "none exists") {
		t.Fatalf("no usable backup is refused: %+v", v)
	}
}

func TestRollbackKeepsDataAndPausesUpdates(t *testing.T) {
	e := newRBEnv(t, "0.4.0")
	known := store.KnownSchemaVersion()
	e.feed.add(t, "stable", "0.3.0", 1)
	e.feed.setSchema(t, "0.3.0", known)
	folder := t.TempDir()
	if err := e.svc.SetBackupSettings(ctx, hub.BackupSettingsUpdate{Folder: &folder}); err != nil {
		t.Fatal(err)
	}
	// Automatic install was on (beta default): it must be restored by "Resume updates".
	res, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.3.0", By: "max"})
	if err != nil {
		t.Fatal(err)
	}
	if res.Plan.Action != updates.SchemaKeep || res.Staged.Version != "0.3.0" || !res.Staged.Rollback || res.Staged.RestoreDB != "" {
		t.Fatalf("%+v", res)
	}
	if res.SafetyDir != folder || !strings.HasSuffix(res.Safety, "-safety.tar") {
		t.Fatalf("safety backup: %+v", res)
	}
	if _, err := os.Stat(filepath.Join(folder, res.Safety)); err != nil {
		t.Fatal("the safety backup must exist before the installer is requested:", err)
	}
	if !updates.RequestPending(e.reqDir) {
		t.Fatal("the root helper must be asked")
	}
	if st, _ := updates.ReadStaged(e.data); st == nil || !st.Rollback || st.Version != "0.3.0" {
		t.Fatalf("%+v", st)
	}

	// The Hub after going back (older version, same database).
	old := e.open("0.3.0")
	set, _ := old.UpdateSettings(ctx)
	if set.Auto {
		t.Fatal("automatic install must be off for every binary that reads update_auto")
	}
	p := old.UpdatePause(ctx)
	if p == nil || p.From != "0.4.0" || p.To != "0.3.0" || p.By != "max" || p.Backup != res.Safety || p.PrevAuto != "" {
		t.Fatalf("%+v", p)
	}
	if pp := old.UpdatePausedProblem(ctx); pp == nil || !strings.Contains(pp.Detail, "Running 0.3.0") || !strings.Contains(pp.Detail, "automatic updates paused") || !strings.Contains(pp.Detail, "0.4.0") {
		t.Fatalf("%+v", pp)
	}
	if st := old.UpdateSkipped(ctx); st != "0.4.0" {
		t.Fatalf("skipped: %q", st)
	}

	// 0.4.0 is available again, listed skipped, and never installed automatically even with auto on.
	e.feed.add(t, "stable", "0.4.0", 1)
	rep, err := old.CheckUpdates(ctx)
	if err != nil || rep.Available == nil || rep.Available.Version != "0.4.0" || rep.Staged {
		t.Fatalf("%+v %v", rep, err)
	}
	stt, _ := old.UpdateStatus(ctx)
	if stt.Paused == nil || stt.Skipped != "0.4.0" || stt.Available == nil || !stt.Available.Skipped {
		t.Fatalf("%+v", stt)
	}
	if err := old.SetUpdateSettings(ctx, "beta", true); err != nil { // an admin switches auto on by hand
		t.Fatal(err)
	}
	if rep, _ = old.CheckUpdates(ctx); rep.Staged {
		t.Fatal("paused: nothing is staged")
	}

	// Resume: auto is back as before, the skipped version is still not installed, a newer one is.
	if err := old.ResumeUpdates(ctx); err != nil {
		t.Fatal(err)
	}
	if old.UpdatePause(ctx) != nil || old.UpdatePausedProblem(ctx) != nil {
		t.Fatal("pause must be gone")
	}
	if err := old.ResumeUpdates(ctx); err == nil {
		t.Fatal("resuming twice must fail")
	}
	set, _ = old.UpdateSettings(ctx)
	if !set.Auto {
		t.Fatalf("auto is on again: %+v", set)
	}
	if rep, _ = old.CheckUpdates(ctx); rep.Staged {
		t.Fatal("the skipped version must not be installed automatically")
	}
	e.feed.add(t, "stable", "0.4.1", 1)
	if rep, _ = old.CheckUpdates(ctx); !rep.Staged || rep.Available.Version != "0.4.1" {
		t.Fatalf("a newer version is installed automatically: %+v", rep)
	}
}

func TestInstallUpdateAfterGoingBackEndsThePause(t *testing.T) {
	e := newRBEnv(t, "0.4.0")
	e.feed.add(t, "stable", "0.3.0", 1)
	e.feed.setSchema(t, "0.3.0", store.KnownSchemaVersion())
	if _, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.3.0", By: "max"}); err != nil {
		t.Fatal(err)
	}
	old := e.open("0.3.0")
	e.feed.add(t, "stable", "0.4.0", 1)
	os.Remove(updates.RequestPath(e.reqDir))
	if _, err := old.CheckUpdates(ctx); err != nil {
		t.Fatal(err)
	}
	if _, err := old.InstallUpdate(ctx, false); err != nil { // "Update to 0.4.0"
		t.Fatal(err)
	}
	if old.UpdatePause(ctx) != nil || old.UpdateSkipped(ctx) != "" {
		t.Fatal("moving forward ends the pause and the skip")
	}
}

func TestRollbackDatabaseRestoreNeedsConfirmation(t *testing.T) {
	e := newRBEnv(t, "0.4.0")
	known := store.KnownSchemaVersion()
	e.feed.add(t, "stable", "0.2.0", 1)
	e.feed.setSchema(t, "0.2.0", known-1)

	// No pre-update backup: refused with a reason, nothing happens.
	_, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.2.0", ConfirmDataLoss: true})
	var he *hub.Error
	if !errors.As(err, &he) || he.Code != hub.CodeConflict || !strings.Contains(he.Message, "none exists") {
		t.Fatalf("%v", err)
	}
	if updates.RequestPending(e.reqDir) {
		t.Fatal("nothing may be requested")
	}

	bk := writeDBBackup(t, e.data, known-1, known, "20261001T090000Z")
	if _, err = e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.2.0"}); !errors.Is(err, hub.ErrRollbackConfirm) {
		t.Fatalf("without the explicit confirmation: %v", err)
	}
	if updates.RequestPending(e.reqDir) {
		t.Fatal("nothing may be requested without the confirmation")
	}
	if entries, _ := os.ReadDir(filepath.Join(e.data, "backups")); len(entries) != 1 {
		t.Fatal("no safety backup before the confirmation")
	}
	res, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.2.0", By: "max", ConfirmDataLoss: true})
	if err != nil {
		t.Fatal(err)
	}
	if res.Plan.Action != updates.SchemaRestore || res.Staged.RestoreDB != bk || res.Pause.RestoredDB != bk || res.Pause.LostSince.IsZero() {
		t.Fatalf("%+v", res)
	}
	// With no backup folder set the safety backup goes to <data>/backups.
	if res.SafetyDir != filepath.Join(e.data, "backups") {
		t.Fatalf("safety dir: %s", res.SafetyDir)
	}
	if _, err := os.Stat(filepath.Join(res.SafetyDir, res.Safety)); err != nil {
		t.Fatal(err)
	}
	// The settings the root helper writes into the restored database travel in the staged directory.
	b, err := os.ReadFile(filepath.Join(updates.StagedDir(e.data), updates.SettingsFileName))
	if err != nil || !strings.Contains(string(b), `"update_auto":"0"`) || !strings.Contains(string(b), `"update_paused"`) || !strings.Contains(string(b), `"update_skip":"0.4.0"`) {
		t.Fatalf("%s %v", b, err)
	}
}

func TestRollbackRefusals(t *testing.T) {
	e := newRBEnv(t, "0.4.0")
	e.feed.add(t, "stable", "0.3.0", 1)
	e.feed.setSchema(t, "0.3.0", store.KnownSchemaVersion())

	if _, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.9.9"}); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("a version that is not offered: %v", err)
	}
	if _, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.5.0"}); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("a newer version is no rollback: %v", err)
	}

	t.Setenv(updates.ContainerEnv, "1")
	if _, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.3.0"}); !errors.Is(err, hub.ErrInContainer) {
		t.Fatalf("container: %v", err)
	}
	info, err := e.svc.RollbackVersions(ctx)
	if err != nil || info.CanRollback || !info.InContainer || info.Unavailable == "" || !strings.Contains(info.Versions[0].ManualCommand, "docker pull") {
		t.Fatalf("%+v %v", info, err)
	}
	t.Setenv(updates.ContainerEnv, "")

	// A Hub that is not the package install cannot go back by itself but still lists the versions.
	u := newUpdEnv(t, "0.4.0", "beta", false)
	u.feed.add(t, "stable", "0.3.0", 1)
	if _, err := u.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.3.0"}); !errors.Is(err, hub.ErrNotPackaged) {
		t.Fatalf("not packaged: %v", err)
	}
	ui, err := u.svc.RollbackVersions(ctx)
	if err != nil || ui.CanRollback || ui.Unavailable == "" || !strings.Contains(ui.Versions[0].ManualCommand, "--allow-downgrades") {
		t.Fatalf("%+v %v", ui, err)
	}

	// Updates off (development build).
	d := newUpdEnv(t, "dev", "dev", true)
	if _, err := d.svc.RollbackVersions(ctx); !errors.Is(err, hub.ErrUpdatesOff) {
		t.Fatalf("dev: %v", err)
	}
}

func TestPauseEndsWhenTheRollbackDidNotHappen(t *testing.T) {
	e := newRBEnv(t, "0.4.0")
	e.feed.add(t, "stable", "0.3.0", 1)
	e.feed.setSchema(t, "0.3.0", store.KnownSchemaVersion())
	if _, err := e.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.3.0", By: "max"}); err != nil {
		t.Fatal(err)
	}
	// Still the old (newer) version, the request is being handled: nothing shows, nothing is undone yet.
	if p := e.svc.UpdatePause(ctx); p != nil {
		t.Fatalf("a pending rollback does not show as paused: %+v", p)
	}
	set, _ := e.svc.UpdateSettings(ctx)
	if set.Auto {
		t.Fatal("auto stays off while the request is pending")
	}
	// The helper failed: the next look ends the pause and puts the settings back.
	os.Remove(updates.RequestPath(e.reqDir))
	os.MkdirAll(filepath.Join(e.data, updates.UpdatesDir), 0o750)
	os.WriteFile(filepath.Join(e.data, updates.UpdatesDir, updates.LastResultName),
		[]byte(`{"version":"0.3.0","time":"`+e.clk.Now().Add(time.Minute).UTC().Format(time.RFC3339)+`","ok":false,"message":"dpkg failed"}`), 0o640)
	e.clk.Advance(2 * time.Minute)
	if p := e.svc.UpdatePause(ctx); p != nil {
		t.Fatal("a failed rollback is no pause")
	}
	set, _ = e.svc.UpdateSettings(ctx)
	if !set.Auto || e.svc.UpdateSkipped(ctx) != "" {
		t.Fatalf("settings must be back: %+v", set)
	}

	// A request nobody handled ends after a while.
	e2 := newRBEnv(t, "0.4.0")
	e2.feed.add(t, "stable", "0.3.0", 1)
	e2.feed.setSchema(t, "0.3.0", store.KnownSchemaVersion())
	if _, err := e2.svc.Rollback(ctx, hub.RollbackRequest{Version: "0.3.0"}); err != nil {
		t.Fatal(err)
	}
	os.Remove(updates.RequestPath(e2.reqDir))
	e2.clk.Advance(time.Hour)
	if e2.svc.UpdatePause(ctx) != nil {
		t.Fatal("stale pause")
	}
	if set, _ := e2.svc.UpdateSettings(ctx); !set.Auto {
		t.Fatal("auto restored")
	}
}

func TestHistoryRecordsRunningVersions(t *testing.T) {
	e := newRBEnv(t, "0.3.0")
	e.feed.add(t, "stable", "0.2.0", 1)
	e.feed.setSchema(t, "0.2.0", store.KnownSchemaVersion())
	if _, err := e.svc.CheckUpdates(ctx); err != nil {
		t.Fatal(err)
	}
	e.clk.Advance(72 * time.Hour)
	newer := e.open("0.4.0")
	info, err := newer.RollbackVersions(ctx)
	if err != nil {
		t.Fatal(err)
	}
	v := e.version(t, info, "0.2.0")
	if !v.FirstUsed.IsZero() {
		t.Fatal("0.2.0 never ran here")
	}
	// 0.3.0 ran here, is not in the index: it is no candidate. Publish it in the beta channel while the install
	// is on stable: the history makes it one.
	e.feed.add(t, "beta", "0.3.0", 1)
	e.feed.setSchema(t, "0.3.0", store.KnownSchemaVersion())
	if err := newer.SetUpdateSettings(ctx, "stable", false); err != nil {
		t.Fatal(err)
	}
	info, err = newer.RollbackVersions(ctx)
	if err != nil {
		t.Fatal(err)
	}
	v = e.version(t, info, "0.3.0")
	if v.FirstUsed.IsZero() || !v.LastUsed.After(time.Time{}) {
		t.Fatalf("history dates: %+v", v)
	}
}
