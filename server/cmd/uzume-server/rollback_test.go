package main

import (
	"bytes"
	"encoding/json"
	"runtime"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/store"
)

func TestRollbackCommand(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("an MSI is only offered when it allows downgrades")
	}
	setVersion(t, "0.4.0", "beta", "")
	f := newCLIFeed(t, "0.3.0") // the feed holds 0.3.0 in the beta channel; no schema_version: unknown
	data, req := t.TempDir(), t.TempDir()
	flags := f.flags(data, req)

	// --list --json
	var out bytes.Buffer
	if err := runRollback(append([]string{"--list", "--json"}, flags...), strings.NewReader(""), &out); err != nil {
		t.Fatal(err)
	}
	var list rollbackListJSON
	if err := json.Unmarshal(out.Bytes(), &list); err != nil {
		t.Fatalf("%v: %s", err, out.String())
	}
	if list.Current != "0.4.0" || list.CurrentSchema != store.KnownSchemaVersion() || len(list.Versions) != 1 || list.Versions[0].Version != "0.3.0" {
		t.Fatalf("%+v", list)
	}
	// The test binary is not the package install: it cannot go back by itself and says how to do it by hand.
	if list.CanRollback || list.Unavailable == "" || !strings.Contains(list.Versions[0].ManualCommand, "--allow-downgrades") {
		t.Fatalf("%+v", list)
	}
	// The index entry does not say the schema: refused, never guessed.
	if list.Versions[0].Data != "refuse" || !strings.Contains(list.Versions[0].Reason, "does not say") {
		t.Fatalf("%+v", list.Versions[0])
	}

	// Plain text list.
	out.Reset()
	if err := runRollback(append([]string{"--list"}, flags...), strings.NewReader(""), &out); err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"Running 0.4.0", "Earlier versions:", "0.3.0", "not possible:", "manual: curl -fLO"} {
		if !strings.Contains(out.String(), want) {
			t.Fatalf("missing %q in:\n%s", want, out.String())
		}
	}

	// Going back is refused here, and a wrong version lists the choices.
	out.Reset()
	if err := runRollback(append([]string{"0.3.0", "--yes"}, flags...), strings.NewReader(""), &out); err == nil {
		t.Fatal("not a package install: must refuse")
	}
	if err := runRollback(append([]string{"9.9.9", "--yes"}, flags...), strings.NewReader(""), &out); err == nil || !strings.Contains(err.Error(), "not one of the earlier versions") {
		t.Fatalf("%v", err)
	}
	// Usage errors.
	for _, args := range [][]string{{}, {"0.3.0", "--list"}, {"--resume", "0.3.0"}, {"--list", "stray"}} {
		if err := runRollback(append(args, flags...), strings.NewReader(""), &out); err == nil || !strings.Contains(err.Error(), "usage") {
			t.Fatalf("%v: %v", args, err)
		}
	}
	// Nothing is paused.
	if err := runRollback(append([]string{"--resume"}, flags...), strings.NewReader(""), &out); err == nil {
		t.Fatal("resume without a pause must fail")
	}
}
