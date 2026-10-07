package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
)

func TestSignerFlow(t *testing.T) {
	dir := t.TempDir()
	now := func() time.Time { return time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC) }
	var out bytes.Buffer
	run0 := func(env map[string]string, args ...string) error {
		out.Reset()
		return run(args, func(k string) string { return env[k] }, &out, now)
	}
	seedFile := filepath.Join(dir, "seed")
	if err := run0(nil, "keygen", "-out", seedFile); err != nil {
		t.Fatal(err)
	}
	seedB, _ := os.ReadFile(seedFile)
	seed := strings.TrimSpace(string(seedB))
	if strings.Contains(out.String(), seed) || !strings.Contains(out.String(), "public_key=") || !strings.Contains(out.String(), "key_id=") {
		t.Fatalf("keygen output %q", out.String())
	}
	if st, _ := os.Stat(seedFile); st.Mode().Perm() != 0o600 {
		t.Fatalf("seed mode %v", st.Mode())
	}
	if err := run0(nil, "keygen", "-out", seedFile); err == nil {
		t.Fatal("keygen overwrote the seed file")
	}
	env := map[string]string{signingKeyEnv: seed}
	if err := run0(env, "pubkey"); err != nil || strings.Contains(out.String(), seed) {
		t.Fatalf("pubkey: %v %q", err, out.String())
	}
	pub := strings.TrimPrefix(strings.SplitN(out.String(), "\n", 2)[0], "public_key=")

	pkg := corepkg.Package{CoreID: "melonds_ds", Version: "1.4.0", Platform: "linux-x64", License: "GPL-3.0",
		Files: []corepkg.File{{Name: "lib.so", Role: "library", Size: 3, SHA256: strings.Repeat("ab", 32), URL: "https://example.org/lib.so"}}}
	pkgFile, idxFile := filepath.Join(dir, "pkg.json"), filepath.Join(dir, "cores-index.json")
	write := func(p corepkg.Package) {
		b, _ := json.Marshal(p)
		os.WriteFile(pkgFile, b, 0o644)
	}
	write(pkg)
	if err := run0(nil, "add", "-index", idxFile, "-package", pkgFile); err != nil {
		t.Fatal(err)
	}
	pkg.Platform, pkg.Version = "windows-x64", "1.3.0"
	write(pkg)
	run0(nil, "add", "-index", idxFile, "-package", pkgFile)
	pkg.License = "replaced"
	write(pkg)
	if err := run0(nil, "add", "-index", idxFile, "-package", pkgFile); err != nil {
		t.Fatal(err)
	}
	data, _ := os.ReadFile(idxFile)
	idx, errs := corepkg.ParseIndex(data)
	if len(errs) != 0 || len(idx.Packages) != 2 || idx.Packages[0].Version != "1.3.0" || idx.Packages[0].License != "replaced" {
		t.Fatalf("index %+v %v", idx, errs)
	}
	write(corepkg.Package{CoreID: "x"})
	if err := run0(nil, "add", "-index", idxFile, "-package", pkgFile); err == nil {
		t.Fatal("invalid package accepted")
	}

	if err := run0(nil, "sign", "-index", idxFile); err == nil {
		t.Fatal("sign without env key succeeded")
	}
	if err := run0(env, "sign", "-index", idxFile); err != nil {
		t.Fatal(err)
	}
	if err := run0(nil, "verify", "-index", idxFile, "-sig", idxFile+".sig", "-pub", pub); err != nil || !strings.HasPrefix(out.String(), "OK") {
		t.Fatalf("verify: %v %q", err, out.String())
	}
	os.WriteFile(idxFile, append(data, ' '), 0o644)
	if err := run0(nil, "verify", "-index", idxFile, "-sig", idxFile+".sig", "-pub", pub); err == nil {
		t.Fatal("tampered index verified")
	}
	if err := run0(map[string]string{signingKeyEnv: "garbage-seed"}, "pubkey"); err == nil || strings.Contains(err.Error(), "garbage-seed") {
		t.Fatalf("bad seed error %v", err)
	}
}
