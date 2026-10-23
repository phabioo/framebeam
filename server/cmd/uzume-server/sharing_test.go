package main

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// fakeSharing records what the sharing commands do to a pretend Windows machine.
type fakeSharing struct {
	services map[string]bool // registered
	calls    []string
	failOn   string // "<op> <name>" that fails
	port     int
}

func newFakeSharing(withUpdater bool) *fakeSharing {
	f := &fakeSharing{services: map[string]bool{hubServiceName: true}, port: 8443}
	if withUpdater {
		f.services[updaterServiceName] = true
	}
	return f
}

func (f *fakeSharing) do(op, name string) error {
	f.calls = append(f.calls, op+" "+name)
	if f.failOn == op+" "+name {
		return errors.New("boom")
	}
	return nil
}
func (f *fakeSharing) ServiceExists(name string) (bool, error) { return f.services[name], nil }
func (f *fakeSharing) StopService(name string, wait time.Duration) error {
	if wait != sharingWait {
		panic("wait")
	}
	return f.do("stop", name)
}
func (f *fakeSharing) StartService(name string, _ time.Duration) error { return f.do("run", name) }
func (f *fakeSharing) SetStartType(name string, s serviceStart) error {
	if s == startDisabled {
		return f.do("disable", name)
	}
	return f.do("auto", name)
}
func (f *fakeSharing) GrantModify(dir, account string) error {
	return f.do("grant", account+" on "+dir)
}
func (f *fakeSharing) ServerPort() int { return f.port }

func readResult(t *testing.T, path string) sharingResult {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var r sharingResult
	if err := json.Unmarshal(b, &r); err != nil {
		t.Fatalf("%s: %v", b, err)
	}
	return r
}

func TestSharingStop(t *testing.T) {
	dir, res := t.TempDir(), filepath.Join(t.TempDir(), "r.json")
	f := newFakeSharing(true)
	var out strings.Builder
	if err := runSharing([]string{"stop", "--grant-user", `PC\max`, "--result", res, "-data-dir", dir}, &out, f); err != nil {
		t.Fatal(err)
	}
	want := []string{`grant PC\max on ` + dir, "stop UzumeServer", "stop UzumeServerUpdater", "disable UzumeServer", "disable UzumeServerUpdater"}
	if strings.Join(f.calls, "|") != strings.Join(want, "|") {
		t.Fatalf("calls %v", f.calls)
	}
	r := readResult(t, res)
	if !r.OK || r.Error != "" || r.DataDir != dir || r.Port != 8443 {
		t.Fatalf("%+v", r)
	}
	// Without the updater service only the server is touched.
	f = newFakeSharing(false)
	if err := runSharing([]string{"stop", "--grant-user", `PC\max`, "-data-dir", dir}, &out, f); err != nil {
		t.Fatal(err)
	}
	if strings.Contains(strings.Join(f.calls, "|"), "Updater") || len(f.calls) != 3 {
		t.Fatalf("calls %v", f.calls)
	}
}

func TestSharingStopFailures(t *testing.T) {
	dir := t.TempDir()
	for _, c := range []struct{ name, fail, args, want string }{
		{"no user", "", "", "--grant-user is missing"},
		{"grant fails", `grant PC\max on ` + dir, `--grant-user PC\max`, "give PC\\max access"},
		{"stop times out", "stop UzumeServer", `--grant-user PC\max`, "stop UzumeServer"},
	} {
		res := filepath.Join(t.TempDir(), "r.json")
		f := newFakeSharing(true)
		f.failOn = c.fail
		args := append([]string{"stop", "--result", res, "-data-dir", dir}, strings.Fields(c.args)...)
		err := runSharing(args, &strings.Builder{}, f)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Fatalf("%s: %v", c.name, err)
		}
		r := readResult(t, res)
		if r.OK || !strings.Contains(r.Error, c.want) || r.DataDir != dir {
			t.Fatalf("%s: %+v", c.name, r)
		}
		for _, call := range f.calls {
			if strings.HasPrefix(call, "disable") {
				t.Fatalf("%s: disabled although stop failed: %v", c.name, f.calls)
			}
		}
	}
	// Not installed.
	f := newFakeSharing(false)
	f.services = map[string]bool{}
	if err := runSharing([]string{"stop", "--grant-user", "x", "-data-dir", dir}, &strings.Builder{}, f); err == nil || !strings.Contains(err.Error(), "not installed") {
		t.Fatalf("%v", err)
	}
	// Missing data folder.
	if err := runSharing([]string{"stop", "--grant-user", "x", "-data-dir", filepath.Join(dir, "none")}, &strings.Builder{}, newFakeSharing(false)); err == nil {
		t.Fatal("missing data folder accepted")
	}
}

func TestSharingStart(t *testing.T) {
	res := filepath.Join(t.TempDir(), "r.json")
	f := newFakeSharing(true)
	if err := runSharing([]string{"start", "--result", res}, &strings.Builder{}, f); err != nil {
		t.Fatal(err)
	}
	want := "auto UzumeServer|auto UzumeServerUpdater|run UzumeServer|run UzumeServerUpdater"
	if strings.Join(f.calls, "|") != want {
		t.Fatalf("calls %v", f.calls)
	}
	if r := readResult(t, res); !r.OK || r.Port != 8443 || r.DataDir == "" {
		t.Fatalf("%+v", r)
	}
	f = newFakeSharing(false)
	f.failOn = "run UzumeServer"
	err := runSharing([]string{"start", "--result", res}, &strings.Builder{}, f)
	if err == nil || readResult(t, res).OK {
		t.Fatalf("%v", err)
	}
}

func TestSharingUsageAndUnsupported(t *testing.T) {
	var ue *usageError
	if err := runSharing([]string{"pause"}, &strings.Builder{}, newFakeSharing(false)); !errors.As(err, &ue) {
		t.Fatalf("%v", err)
	}
	if err := runSharing(nil, &strings.Builder{}, newFakeSharing(false)); !errors.As(err, &ue) {
		t.Fatalf("%v", err)
	}
	if err := runSharing([]string{"stop"}, &strings.Builder{}, nil); !errors.Is(err, errSharingUnsupported) {
		t.Fatalf("%v", err)
	}
}
