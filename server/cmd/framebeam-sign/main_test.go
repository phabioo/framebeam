package main

import (
	"bytes"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/updates"
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
	if st, _ := os.Stat(seedFile); runtime.GOOS != "windows" && st.Mode().Perm() != 0o600 { // Windows has no unix modes
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

func TestReleaseAddKeepPruneAndDeterminism(t *testing.T) {
	dir := t.TempDir()
	fixed := time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC)
	now := func() time.Time { return fixed }
	var out bytes.Buffer
	runE := func(env map[string]string, args ...string) error {
		out.Reset()
		return run(args, func(k string) string { return env[k] }, &out, now)
	}
	relFile := filepath.Join(dir, "release.json")
	mk := func(product, channel, version string) {
		t.Helper()
		plat, kind, name := "linux-amd64", "deb", "framebeam-hub_"+version+"_amd64.deb"
		if product == "player" {
			plat, kind, name = "windows-x64", "installer", "framebeam-player-"+version+"-windows-x64-setup.exe"
		}
		b, _ := json.Marshal(updates.Release{Product: product, Channel: channel, Version: version, Commit: strings.Repeat("a", 40),
			PublishedAt: fixed, ProtocolVersion: 1, MinProtocolVersion: 1,
			Artifacts: []updates.Artifact{{Platform: plat, Kind: kind, Name: name, Size: 5, SHA256: strings.Repeat("ab", 32), URL: "https://example.org/" + name}}})
		os.WriteFile(relFile, b, 0o644)
	}
	add := func(idx, product, channel, version string, extra ...string) {
		t.Helper()
		mk(product, channel, version)
		if err := runE(nil, append([]string{"release-add", "-index", idx, "-release", relFile}, extra...)...); err != nil {
			t.Fatal(err)
		}
	}
	versions := []string{"0.3.0-beta.2", "0.3.0-beta.10", "0.3.0-beta.1", "0.3.0-beta.9", "0.3.0-beta.3", "0.3.0-beta.4", "0.3.0-beta.11", "0.3.0-beta.5"}
	idxA := filepath.Join(dir, "a.json")
	for _, v := range versions {
		add(idxA, "hub", "beta", v)
	}
	add(idxA, "hub", "stable", "0.2.0")
	add(idxA, "player", "beta", "0.3.0-beta.10")

	data, _ := os.ReadFile(idxA)
	idx, errs := updates.ParseIndex(data)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	var got []string
	for _, r := range idx.Releases {
		got = append(got, r.Product+"/"+r.Channel+"/"+r.Version)
	}
	want := "hub/beta/0.3.0-beta.4 hub/beta/0.3.0-beta.5 hub/beta/0.3.0-beta.9 hub/beta/0.3.0-beta.10 hub/beta/0.3.0-beta.11 hub/stable/0.2.0 player/beta/0.3.0-beta.10"
	if strings.Join(got, " ") != want {
		t.Fatalf("keep 5 per product+channel by SemVer:\n got %v\nwant %s", got, want)
	}
	if !idx.GeneratedAt.Equal(fixed) {
		t.Fatalf("generated_at %v", idx.GeneratedAt)
	}

	// Deterministic: another order gives identical bytes; replacing an existing release is idempotent.
	idxB := filepath.Join(dir, "b.json")
	for i := len(versions) - 1; i >= 0; i-- {
		add(idxB, "hub", "beta", versions[i])
	}
	add(idxB, "player", "beta", "0.3.0-beta.10")
	add(idxB, "hub", "stable", "0.2.0")
	add(idxB, "hub", "beta", "0.3.0-beta.11")
	dataB, _ := os.ReadFile(idxB)
	if !bytes.Equal(data, dataB) {
		t.Fatalf("not deterministic:\n%s\n---\n%s", data, dataB)
	}
	// -keep applies to every call.
	add(idxA, "hub", "beta", "0.3.0-beta.12", "-keep", "2")
	data, _ = os.ReadFile(idxA)
	idx, _ = updates.ParseIndex(data)
	n := 0
	for _, r := range idx.Releases {
		if r.Product == "hub" && r.Channel == "beta" {
			n++
		}
	}
	if n != 2 {
		t.Fatalf("keep 2: %d", n)
	}

	// Invalid release, bad keep and missing flags are refused; an existing invalid index is not overwritten.
	os.WriteFile(relFile, []byte(`{"product":"hub","channel":"dev","version":"1.0.0"}`), 0o644)
	if err := runE(nil, "release-add", "-index", idxA, "-release", relFile); err == nil {
		t.Fatal("invalid release accepted")
	}
	mk("hub", "beta", "0.9.0")
	if err := runE(nil, "release-add", "-index", idxA, "-release", relFile, "-keep", "0"); err == nil {
		t.Fatal("keep 0 accepted")
	}
	if err := runE(nil, "release-add", "-index", idxA); err == nil {
		t.Fatal("missing -release")
	}
	bad := filepath.Join(dir, "bad.json")
	os.WriteFile(bad, []byte(`{"schema":9,"releases":[]}`), 0o644)
	if err := runE(nil, "release-add", "-index", bad, "-release", relFile); err == nil {
		t.Fatal("unknown schema overwritten")
	}
}

func TestSignAcceptsUpdatesIndex(t *testing.T) {
	dir := t.TempDir()
	now := func() time.Time { return time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC) }
	var out bytes.Buffer
	runE := func(env map[string]string, args ...string) error {
		out.Reset()
		return run(args, func(k string) string { return env[k] }, &out, now)
	}
	seedFile := filepath.Join(dir, "seed")
	if err := runE(nil, "keygen", "-out", seedFile); err != nil {
		t.Fatal(err)
	}
	seedB, _ := os.ReadFile(seedFile)
	env := map[string]string{signingKeyEnv: strings.TrimSpace(string(seedB))}
	pub := strings.TrimPrefix(strings.SplitN(out.String(), "\n", 2)[0], "public_key=")

	idx := filepath.Join(dir, "updates-index.json")
	b, _ := json.Marshal(updates.Release{Product: "hub", Channel: "stable", Version: "0.3.0", PublishedAt: now(), ProtocolVersion: 1, MinProtocolVersion: 1,
		Artifacts: []updates.Artifact{{Platform: "linux-arm64", Kind: "deb", Name: "x.deb", Size: 1, SHA256: strings.Repeat("0", 64), URL: "https://example.org/x.deb"}}})
	rel := filepath.Join(dir, "r.json")
	os.WriteFile(rel, b, 0o644)
	if err := runE(nil, "release-add", "-index", idx, "-release", rel); err != nil {
		t.Fatal(err)
	}
	if err := runE(env, "sign", "-index", idx); err != nil {
		t.Fatal(err)
	}
	if err := runE(nil, "verify", "-index", idx, "-sig", idx+".sig", "-pub", pub); err != nil || !strings.HasPrefix(out.String(), "OK") {
		t.Fatalf("%v %q", err, out.String())
	}
	// An invalid updates index (unknown schema / duplicate) is not signed; a core index still is.
	dup := filepath.Join(dir, "dup.json")
	os.WriteFile(dup, []byte(`{"schema":2,"releases":[]}`), 0o644)
	if err := runE(env, "sign", "-index", dup); err == nil {
		t.Fatal("unknown schema signed")
	}
	if _, err := os.Stat(dup + ".sig"); err == nil {
		t.Fatal("signature written for an invalid index")
	}
}

func TestAllowFileURLs(t *testing.T) {
	dir := t.TempDir()
	now := func() time.Time { return time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC) }
	var out bytes.Buffer
	runE := func(env map[string]string, args ...string) error {
		out.Reset()
		return run(args, func(k string) string { return env[k] }, &out, now)
	}
	seedFile := filepath.Join(dir, "seed")
	if err := runE(nil, "keygen", "-out", seedFile); err != nil {
		t.Fatal(err)
	}
	seedB, _ := os.ReadFile(seedFile)
	env := map[string]string{signingKeyEnv: strings.TrimSpace(string(seedB))}
	b, _ := json.Marshal(updates.Release{Product: "hub", Channel: "beta", Version: "0.3.0-beta.1", PublishedAt: now(), ProtocolVersion: 1, MinProtocolVersion: 1,
		Artifacts: []updates.Artifact{{Platform: "linux-amd64", Kind: "deb", Name: "x.deb", Size: 1, SHA256: strings.Repeat("0", 64), URL: "file:///tmp/x.deb"}}})
	rel, idx := filepath.Join(dir, "r.json"), filepath.Join(dir, "updates-index.json")
	os.WriteFile(rel, b, 0o644)
	if err := runE(nil, "release-add", "-index", idx, "-release", rel); err == nil {
		t.Fatal("file:// accepted by default")
	}
	if err := runE(nil, "release-add", "-index", idx, "-release", rel, "-allow-file-urls"); err != nil {
		t.Fatal(err)
	}
	if err := runE(env, "sign", "-index", idx); err == nil {
		t.Fatal("sign accepted file:// by default")
	}
	if err := runE(env, "sign", "-index", idx, "-allow-file-urls"); err != nil {
		t.Fatal(err)
	}
	if err := runE(nil, "release-add", "-index", idx, "-release", rel); err == nil {
		t.Fatal("existing file:// index accepted without the flag")
	}
}

func TestReleaseAddDropsInvalidExistingReleases(t *testing.T) {
	dir := t.TempDir()
	idxPath := filepath.Join(dir, "updates-index.json")
	mkRel := func(channel, version string) updates.Release {
		name := "framebeam-hub_" + version + "_amd64.deb"
		return updates.Release{Product: "hub", Channel: channel, Version: version, Commit: strings.Repeat("a", 40),
			PublishedAt: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC), ProtocolVersion: 1, MinProtocolVersion: 1,
			Artifacts: []updates.Artifact{{Platform: "linux-amd64", Kind: "deb", Name: name, Size: 5, SHA256: strings.Repeat("ab", 32), URL: "https://example.org/" + name}}}
	}
	old, _ := updates.Marshal(updates.Index{Schema: updates.Schema, GeneratedAt: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC),
		Releases: []updates.Release{mkRel("test", "0.3.0-test.196"), mkRel("stable", "0.2.0")}})
	os.WriteFile(idxPath, old, 0o644)
	nb, _ := json.Marshal(mkRel("beta", "0.3.0-beta.1"))
	relFile := filepath.Join(dir, "release.json")
	os.WriteFile(relFile, nb, 0o644)
	now := func() time.Time { return time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC) }
	runE := func(env map[string]string, args ...string) error {
		return run(args, func(k string) string { return env[k] }, io.Discard, now)
	}
	var warn bytes.Buffer
	prev := warnOut
	warnOut = &warn
	defer func() { warnOut = prev }()
	if err := runE(nil, "release-add", "-index", idxPath, "-release", relFile); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(warn.String(), "0.3.0-test.196") {
		t.Fatalf("dropped entry not reported: %q", warn.String())
	}
	data, _ := os.ReadFile(idxPath)
	idx, errs := updates.ParseIndex(data)
	if len(errs) != 0 || len(idx.Releases) != 2 || idx.Releases[0].Channel != "beta" || idx.Releases[1].Channel != "stable" {
		t.Fatalf("%v %+v", errs, idx.Releases)
	}
	// Unknown schema stays fatal.
	os.WriteFile(idxPath, []byte(`{"schema":99,"releases":[]}`), 0o644)
	if err := runE(nil, "release-add", "-index", idxPath, "-release", relFile); err == nil {
		t.Fatal("unknown schema must fail")
	}
}
