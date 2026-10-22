package main

import (
	"bytes"
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/backup"
)

func runOK(t *testing.T, f func([]string, *strings.Builder) error, args ...string) string {
	t.Helper()
	var out strings.Builder
	if err := f(args, &out); err != nil {
		t.Fatalf("%v: %v\n%s", args, err, out.String())
	}
	return out.String()
}

func backupCmd(args []string, out *strings.Builder) error  { return runBackup(args, out) }
func restoreCmd(args []string, out *strings.Builder) error { return runRestore(args, out) }

func TestBackupCLIRoundTrip(t *testing.T) {
	data, folder, safety := t.TempDir(), t.TempDir(), t.TempDir()
	put := func(rel, content string) {
		p := filepath.Join(data, rel)
		os.MkdirAll(filepath.Dir(p), 0o750)
		if err := os.WriteFile(p, []byte(content), 0o640); err != nil {
			t.Fatal(err)
		}
	}
	put("saves/u/g/main/aaaa", "synthetic save one")

	// Settings: show defaults, then set everything.
	out := runOK(t, backupCmd, "config", "-data-dir", data)
	if !strings.Contains(out, "not set") || !strings.Contains(out, "keep:     7") {
		t.Fatal(out)
	}
	out = runOK(t, backupCmd, "config", "-data-dir", data, "-folder", folder, "-schedule", "weekly", "-weekday", "fri", "-time", "04:15", "-keep", "3", "-roms", "off")
	if !strings.Contains(out, "weekly on fri at 04:15") || !strings.Contains(out, "keep:     3") {
		t.Fatal(out)
	}
	var bad strings.Builder
	if err := runBackup([]string{"config", "-data-dir", data, "-schedule", "hourly"}, &bad); err == nil {
		t.Fatal("bad schedule accepted")
	}
	if err := runBackup([]string{"config", "-data-dir", data, "-folder", filepath.Join(data, "saves")}, &bad); err == nil {
		t.Fatal("folder inside saves accepted")
	}

	// Backup into the configured folder, list, verify.
	out = runOK(t, backupCmd, "-data-dir", data)
	if !strings.Contains(out, "Backup written: "+folder) {
		t.Fatal(out)
	}
	list := runOK(t, backupCmd, "list", "-data-dir", data)
	if !strings.Contains(list, "1 backup(s) in "+folder) || !strings.Contains(list, "Database · saves") {
		t.Fatal(list)
	}
	infos, _ := backup.List(folder)
	if len(infos) != 1 {
		t.Fatalf("%d", len(infos))
	}
	file := infos[0].Path
	if v := runOK(t, backupCmd, "verify", file); !strings.HasPrefix(v, "OK:") {
		t.Fatal(v)
	}
	// A copy with --out leaves the configured folder alone.
	other := t.TempDir()
	runOK(t, backupCmd, "-data-dir", data, "--out", other, "--roms")
	if l, _ := backup.List(other); len(l) != 1 || !l[0].Manifest.IncludesROMs {
		t.Fatalf("%+v", l)
	}

	// Change the live data and the settings, then restore.
	put("saves/u/g/main/bbbb", "saved after the backup")
	runOK(t, backupCmd, "config", "-data-dir", data, "-keep", "5")

	// A running Hub holds the lock: restore refuses.
	release, err := backup.LockDataDir(data)
	if err != nil {
		t.Fatal(err)
	}
	var lockedOut strings.Builder
	if err := runRestore([]string{file, "-data-dir", data, "-safety-dir", safety}, &lockedOut); err == nil || !strings.Contains(err.Error(), "Hub is running") {
		t.Fatalf("locked: %v", err)
	}
	release()

	// A damaged file changes nothing (the safety backup was made before, it is kept).
	b, _ := os.ReadFile(file)
	i := bytes.Index(b, []byte("synthetic save one"))
	b[i] ^= 0xff
	damaged := filepath.Join(t.TempDir(), "damaged.tar")
	os.WriteFile(damaged, b, 0o600)
	var dOut strings.Builder
	if err := runRestore([]string{damaged, "-data-dir", data, "-safety-dir", safety}, &dOut); err == nil || !strings.Contains(err.Error(), "checksum mismatch in saves") {
		t.Fatalf("damaged: %v", err)
	}
	if _, err := os.Stat(filepath.Join(data, "saves/u/g/main/bbbb")); err != nil {
		t.Fatal("live data changed by a failed restore")
	}
	if _, err := os.Stat(filepath.Join(data, backup.StagingDir)); err == nil {
		t.Fatal("staging left over")
	}

	// A backup of a newer schema is rejected before anything is read.
	newer, err := backup.Create(context.Background(), backup.Options{DataDir: data, TargetDir: t.TempDir(), HubName: "x", SchemaVersion: 9999,
		Snapshot: func(dest string) error { return os.WriteFile(dest, []byte("x"), 0o600) }, Now: time.Now})
	if err != nil {
		t.Fatal(err)
	}
	var nOut strings.Builder
	if err := runRestore([]string{newer.Path, "-data-dir", data, "-no-safety"}, &nOut); err == nil || !strings.Contains(err.Error(), "newer FrameBeam Hub") {
		t.Fatalf("newer: %v", err)
	}

	// The real restore.
	out = runOK(t, restoreCmd, file, "-data-dir", data, "-safety-dir", safety)
	if !strings.Contains(out, "Safety backup:") || !strings.Contains(out, "Restored the backup") {
		t.Fatal(out)
	}
	if _, err := os.Stat(filepath.Join(data, "saves/u/g/main/bbbb")); err == nil {
		t.Fatal("the save made after the backup must be gone")
	}
	if got, _ := os.ReadFile(filepath.Join(data, "saves/u/g/main/aaaa")); string(got) != "synthetic save one" {
		t.Fatalf("%q", got)
	}
	out = runOK(t, backupCmd, "config", "-data-dir", data)
	if !strings.Contains(out, "keep:     3") {
		t.Fatalf("settings must be those of the backup: %s", out)
	}
	if l, _ := backup.List(safety); len(l) != 2 || !l[0].Safety {
		t.Fatalf("safety backup: %+v", l)
	}
	if r := backup.ReadResult(data); r == nil || !r.OK || r.Safety == "" {
		t.Fatalf("%+v", r)
	}
	// Undo with the safety backup: the later save and the setting are back.
	runOK(t, restoreCmd, infosPath(t, safety), "-data-dir", data, "-no-safety")
	if _, err := os.Stat(filepath.Join(data, "saves/u/g/main/bbbb")); err != nil {
		t.Fatal("undo failed")
	}
	if !strings.Contains(runOK(t, backupCmd, "config", "-data-dir", data), "keep:     5") {
		t.Fatal("setting not back")
	}
}

func infosPath(t *testing.T, dir string) string {
	t.Helper()
	l, err := backup.List(dir)
	if err != nil || len(l) == 0 {
		t.Fatal("no backup in " + dir)
	}
	return l[0].Path
}

func TestRestoreNeedsSafetyChoice(t *testing.T) {
	data := t.TempDir()
	runOK(t, backupCmd, "config", "-data-dir", data)
	var out strings.Builder
	// No folder anywhere and no --no-safety: refuses instead of restoring unprotected.
	other, _ := backup.Create(context.Background(), backup.Options{DataDir: data, TargetDir: t.TempDir(), HubName: "x", SchemaVersion: 1,
		Snapshot: func(dest string) error { return os.WriteFile(dest, []byte("x"), 0o600) }, Now: time.Now})
	if err := runRestore([]string{other.Path, "-data-dir", data}, &out); err == nil || !strings.Contains(err.Error(), "safety") {
		t.Fatalf("%v", err)
	}
	if err := runRestore([]string{"-data-dir", data}, &out); err == nil {
		t.Fatal("file argument is required")
	}
}
