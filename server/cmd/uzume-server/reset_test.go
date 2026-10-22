package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/backup"
	"github.com/phabioo/uzume/server/internal/tlsutil"
)

// resetData builds a data folder with a database, saves, a ROM, a certificate, a backups folder and logs, and
// returns it with the server name.
func resetData(t *testing.T) (dir, name string) {
	t.Helper()
	dir = newServerData(t, true)
	svc, closeFn := testService(t, dir)
	name = svc.Info().Name
	closeFn()
	put := func(rel, content string) {
		p := filepath.Join(dir, rel)
		os.MkdirAll(filepath.Dir(p), 0o750)
		if err := os.WriteFile(p, []byte(content), 0o640); err != nil {
			t.Fatal(err)
		}
	}
	put("saves/u/g/main/aaaa", "synthetic save")
	put("roms/ab/abcd.nds", "dummy rom bytes")
	put("backups/old-migration.db", "an earlier backup stays")
	put("logs/hub.log", "log line")
	put("library-import/readme.txt", "in the data folder")
	if _, err := tlsutil.EnsureSelfSigned(dir); err != nil {
		t.Fatal(err)
	}
	return dir, name
}

func exists(p string) bool { _, err := os.Stat(p); return err == nil }

func TestResetRefusedWhileServerRuns(t *testing.T) {
	dir, name := resetData(t)
	release, err := backup.LockDataDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	defer release()
	var out strings.Builder
	err = runReset([]string{"-data-dir", dir, "-confirm", name}, strings.NewReader(""), &out)
	if err == nil || !strings.Contains(err.Error(), "server is running") || !strings.Contains(err.Error(), "stop") {
		t.Fatalf("%v", err)
	}
	if !exists(filepath.Join(dir, "uzume.db")) || !exists(filepath.Join(dir, "saves/u/g/main/aaaa")) {
		t.Fatal("nothing may be deleted while the server runs")
	}
}

func TestResetWrongName(t *testing.T) {
	dir, name := resetData(t)
	var out strings.Builder
	if err := runReset([]string{"-data-dir", dir}, strings.NewReader("not-the-name\n"), &out); err == nil || !strings.Contains(err.Error(), "canceled") {
		t.Fatalf("typed: %v", err)
	}
	if err := runReset([]string{"-data-dir", dir}, strings.NewReader(""), &out); err == nil {
		t.Fatal("no answer must cancel")
	}
	if err := runReset([]string{"-data-dir", dir, "-confirm", name + "x"}, strings.NewReader(""), &out); err == nil || !strings.Contains(err.Error(), "does not match") {
		t.Fatalf("-confirm: %v", err)
	}
	if !exists(filepath.Join(dir, "uzume.db")) || !exists(filepath.Join(dir, "roms/ab/abcd.nds")) {
		t.Fatal("a wrong name must not delete anything")
	}
	if l, _ := backup.List(filepath.Join(dir, "backups")); len(l) != 0 {
		t.Fatalf("no safety backup before the name is confirmed: %+v", l)
	}
	if !strings.Contains(out.String(), name) {
		t.Fatalf("the prompt must name the server: %s", out.String())
	}
}

func TestResetKeepsBackupsAndWritesSafetyBackup(t *testing.T) {
	dir, name := resetData(t)
	outside := filepath.Join(t.TempDir(), "own-cert.pem")
	os.WriteFile(outside, []byte("outside the data folder"), 0o600)

	var out strings.Builder
	if err := runReset([]string{"-data-dir", dir}, strings.NewReader(name+"\n"), &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	// Deleted: database, saves, ROMs, certificate, the import folder inside the data folder.
	for _, gone := range []string{"uzume.db", "saves", "roms", "tls", "library-import"} {
		if exists(filepath.Join(dir, gone)) {
			t.Errorf("%s must be deleted", gone)
		}
	}
	// Kept: backups (with the new safety backup), logs, the lock file, anything outside.
	for _, kept := range []string{"backups/old-migration.db", "logs/hub.log", "hub.lock"} {
		if !exists(filepath.Join(dir, kept)) {
			t.Errorf("%s must stay", kept)
		}
	}
	if !exists(outside) {
		t.Error("a file outside the data folder must stay")
	}
	infos, err := backup.List(filepath.Join(dir, "backups"))
	if err != nil {
		t.Fatal(err)
	}
	var safety *backup.Info
	for i := range infos {
		if infos[i].Safety {
			safety = &infos[i]
		}
	}
	if safety == nil || safety.Err != "" || safety.Manifest.IncludesROMs {
		t.Fatalf("want one safety backup without ROMs: %+v", infos)
	}
	for _, want := range []string{"Safety backup:", "Deleted", "new setup code", "security code changes"} {
		if !strings.Contains(out.String(), want) {
			t.Errorf("output lacks %q:\n%s", want, out.String())
		}
	}
	// A fresh start finds an empty folder: the server (not tested here) creates database and certificate.
	if hasDatabase(dir) {
		t.Fatal("reset leaves the creation of the new database to the next start")
	}
}

func TestResetUsesConfiguredBackupFolderAndNoBackup(t *testing.T) {
	dir, name := resetData(t)
	folder := t.TempDir()
	var out strings.Builder
	if err := runBackup([]string{"config", "-data-dir", dir, "-folder", folder}, &out); err != nil {
		t.Fatal(err)
	}
	if err := runReset([]string{"-data-dir", dir, "-confirm", name}, strings.NewReader(""), &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if l, _ := backup.List(folder); len(l) != 1 || !l[0].Safety {
		t.Fatalf("the safety backup belongs in the configured folder: %+v", l)
	}
	if l, _ := backup.List(filepath.Join(dir, "backups")); len(l) != 0 {
		t.Fatalf("not in <data>/backups: %+v", l)
	}

	// -no-backup writes none.
	dir, name = resetData(t)
	out.Reset()
	if err := runReset([]string{"-data-dir", dir, "-confirm", name, "-no-backup"}, strings.NewReader(""), &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if l, _ := backup.List(filepath.Join(dir, "backups")); len(l) != 0 {
		t.Fatalf("-no-backup must not write one: %+v", l)
	}
	if hasDatabase(dir) || !strings.Contains(out.String(), "No safety backup") {
		t.Fatalf("%s", out.String())
	}
}

func TestResetNothingToDo(t *testing.T) {
	var out strings.Builder
	if err := runReset([]string{"-data-dir", filepath.Join(t.TempDir(), "none")}, strings.NewReader(""), &out); err != nil || !strings.Contains(out.String(), "Nothing to reset") {
		t.Fatalf("%v %s", err, out.String())
	}
}
