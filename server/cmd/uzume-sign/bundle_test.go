package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/updates"
)

func TestSignBundleRefreshAndExpiry(t *testing.T) {
	dir := t.TempDir()
	cur := time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC)
	now := func() time.Time { return cur }
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
	read := func() updates.Index {
		data, _ := os.ReadFile(idx)
		i, errs := updates.ParseIndex(data)
		if len(errs) > 0 {
			t.Fatal(errs)
		}
		return i
	}
	if i := read(); !i.ExpiresAt.Equal(cur.Add(updates.IndexValidity)) {
		t.Fatalf("expires_at %v", i.ExpiresAt)
	}
	if err := runE(env, "sign", "-index", idx, "-bundle", idx+".signed"); err != nil {
		t.Fatal(err)
	}
	if err := runE(nil, "verify", "-index", idx, "-bundle", idx+".signed", "-pub", pub); err != nil || !strings.Contains(out.String(), "context=uzume-updates-index-v1") {
		t.Fatalf("%v %q", err, out.String())
	}
	if err := runE(nil, "verify", "-index", idx, "-sig", idx+".sig", "-pub", pub); err != nil {
		t.Fatal(err) // the legacy pair is still produced
	}
	if err := runE(nil, "verify", "-index", idx, "-sig", idx+".sig", "-bundle", idx+".signed", "-pub", pub); err == nil {
		t.Fatal("-sig and -bundle together accepted")
	}
	// The legacy signature line must not pass as a bundle.
	legacyAsBundle := filepath.Join(dir, "legacy.signed")
	sig, _ := os.ReadFile(idx + ".sig")
	data, _ := os.ReadFile(idx)
	os.WriteFile(legacyAsBundle, append(sig, data...), 0o644)
	if err := runE(nil, "verify", "-index", idx, "-bundle", legacyAsBundle, "-pub", pub); err == nil {
		t.Fatal("legacy signature line accepted in a bundle")
	}
	// index-refresh only moves generated_at and expires_at.
	before := read()
	cur = cur.Add(24 * time.Hour)
	if err := runE(nil, "index-refresh", "-index", idx); err != nil {
		t.Fatal(err)
	}
	after := read()
	if !after.GeneratedAt.Equal(cur) || !after.ExpiresAt.Equal(cur.Add(updates.IndexValidity)) || len(after.Releases) != len(before.Releases) {
		t.Fatalf("%+v", after)
	}
	// Changed content: the old bundle no longer matches.
	if err := runE(nil, "verify", "-index", idx, "-bundle", idx+".signed", "-pub", pub); err == nil {
		t.Fatal("stale bundle accepted")
	}
}
