package main

import (
	"archive/zip"
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func TestRunDiagnosticsWritesBundleAndNeverOverwrites(t *testing.T) {
	data := t.TempDir()
	if err := os.MkdirAll(filepath.Join(data, "logs"), 0o750); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(data, "logs", "hub.log"), []byte("level=INFO token=PLANTED12345 ok\n"), 0o640); err != nil {
		t.Fatal(err)
	}
	out := filepath.Join(t.TempDir(), "d.zip")
	var buf bytes.Buffer
	if err := runDiagnostics([]string{out, "-data-dir", data, "-dev"}, &buf); err != nil {
		t.Fatal(err)
	}
	zr, err := zip.OpenReader(out)
	if err != nil {
		t.Fatal(err)
	}
	defer zr.Close()
	names := map[string]bool{}
	for _, f := range zr.File {
		names[f.Name] = true
	}
	if !names["hub-info.json"] || !names["logs/hub.log"] || !names["README.txt"] {
		t.Fatalf("entries: %v", names)
	}
	if err := runDiagnostics([]string{out, "-data-dir", data, "-dev"}, &buf); err == nil {
		t.Fatal("an existing file must not be overwritten")
	}
	if err := runDiagnostics(nil, &buf); err == nil {
		t.Fatal("usage error expected")
	}
}
