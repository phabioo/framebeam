package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/envcompat"
)

// The tool reads UZUME_SIGNING_KEY and still accepts FRAMEBEAM_SIGNING_KEY (the GitHub secret keeps that name) with a hint.
func TestSigningKeyOldNameFallback(t *testing.T) {
	envcompat.Reset()
	t.Cleanup(envcompat.Reset)
	now := func() time.Time { return time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC) }
	var out, warn bytes.Buffer
	prev := warnOut
	warnOut = &warn
	defer func() { warnOut = prev }()
	seedFile := filepath.Join(t.TempDir(), "seed")
	if err := run([]string{"keygen", "-out", seedFile}, func(string) string { return "" }, &out, now); err != nil {
		t.Fatal(err)
	}
	seed := readSeed(t, seedFile)
	out.Reset()
	if err := run([]string{"pubkey"}, func(k string) string {
		if k == "FRAMEBEAM_SIGNING_KEY" {
			return seed
		}
		return ""
	}, &out, now); err != nil || !strings.Contains(out.String(), "public_key=") {
		t.Fatalf("old name: %v %q", err, out.String())
	}
	if !strings.Contains(warn.String(), "FRAMEBEAM_SIGNING_KEY is deprecated, use UZUME_SIGNING_KEY") {
		t.Fatalf("hint missing: %q", warn.String())
	}
	warn.Reset()
	envcompat.Reset()
	if err := run([]string{"pubkey"}, func(k string) string {
		if k == "UZUME_SIGNING_KEY" {
			return seed
		}
		return ""
	}, &out, now); err != nil || warn.Len() != 0 {
		t.Fatalf("new name: %v %q", err, warn.String())
	}
}

func readSeed(t *testing.T, path string) string {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return strings.TrimSpace(string(b))
}
