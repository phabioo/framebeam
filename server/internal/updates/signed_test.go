package updates

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
)

func TestVerifyContextSeparatesDocumentTypes(t *testing.T) {
	keys := testKeys(t)
	data := []byte(`{"schema":1}`)
	line, err := corepkg.SignContext(corepkg.ContextUpdatesIndex, data, testSeed)
	if err != nil {
		t.Fatal(err)
	}
	if err := corepkg.VerifyContext(corepkg.ContextUpdatesIndex, data, line, keys, false); err != nil {
		t.Fatal(err)
	}
	if err := corepkg.VerifyContext(corepkg.ContextPluginIndex, data, line, keys, true); err == nil || !strings.Contains(err.Error(), "not") {
		t.Fatalf("a signature for another document type must not verify: %v", err)
	}
	// The context is part of the signed bytes: relabelling the line fails.
	forged := []byte(strings.Replace(string(line), corepkg.ContextUpdatesIndex, corepkg.ContextPluginIndex, 1))
	if err := corepkg.VerifyContext(corepkg.ContextPluginIndex, data, forged, keys, true); err == nil {
		t.Fatal("relabelled context accepted")
	}
	// A legacy signature (raw bytes) only passes while legacy is allowed.
	legacy, _ := corepkg.Sign(data, testSeed)
	if err := corepkg.VerifyContext(corepkg.ContextUpdatesIndex, data, legacy, keys, true); err != nil {
		t.Fatal(err)
	}
	if err := corepkg.VerifyContext(corepkg.ContextUpdatesIndex, data, legacy, keys, false); err == nil {
		t.Fatal("legacy signature accepted in strict mode")
	}
	// The domain-separated line is not a valid legacy line and not valid for the raw bytes.
	if err := corepkg.Verify(data, line, keys); err == nil {
		t.Fatal("context line accepted as legacy")
	}
}

func TestBundleRoundTrip(t *testing.T) {
	line, _ := corepkg.SignContext(corepkg.ContextUpdatesIndex, []byte("x\ny"), testSeed)
	sig, data, err := corepkg.SplitBundle(corepkg.Bundle(line, []byte("x\ny")))
	if err != nil || string(data) != "x\ny" || string(sig) != string(line) {
		t.Fatalf("%q %q %v", sig, data, err)
	}
	if _, _, err := corepkg.SplitBundle([]byte("no newline")); err == nil {
		t.Fatal("bundle without signature line")
	}
}

// writeBundle writes <index>.signed next to the fixture index, signed with the domain-separated context.
func (f *fixture) writeBundle(t *testing.T) {
	t.Helper()
	p := filepath.Join(f.feed, "updates-index.json")
	data, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	line, _ := corepkg.SignContext(corepkg.ContextUpdatesIndex, data, testSeed)
	if err := os.WriteFile(p+BundleSuffix, corepkg.Bundle(line, data), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestLoadIndexPrefersBundleAndFallsBackToPair(t *testing.T) {
	f := newFixture(t, "0.3.0")
	ctx := context.Background()
	if _, err := f.src.LoadIndex(ctx, f.keys); err != nil { // no bundle yet: legacy pair
		t.Fatal(err)
	}
	f.writeBundle(t)
	fe, err := f.src.LoadIndex(ctx, f.keys)
	if err != nil {
		t.Fatal(err)
	}
	if !corepkg.IsContextSig(fe.Sig) {
		t.Fatal("bundle was not used")
	}
	// A broken legacy pair does not matter once the bundle exists ...
	p := filepath.Join(f.feed, "updates-index.json")
	os.WriteFile(p+".sig", []byte("garbage\n"), 0o644)
	if _, err := f.src.LoadIndex(ctx, f.keys); err != nil {
		t.Fatalf("bundle alone must be enough: %v", err)
	}
	// ... and a tampered bundle is an error, never a silent fallback to the (valid) legacy pair.
	plain, _ := os.ReadFile(p)
	legacySig, _ := corepkg.Sign(plain, testSeed)
	os.WriteFile(p+".sig", legacySig, 0o644)
	b, _ := os.ReadFile(p + BundleSuffix)
	os.WriteFile(p+BundleSuffix, append(b, ' '), 0o644)
	if _, err := f.src.LoadIndex(ctx, f.keys); err == nil || !strings.Contains(err.Error(), "signature") {
		t.Fatalf("tampered bundle: %v", err)
	}
}

func TestLoadIndexRejectsExpiredIndex(t *testing.T) {
	f := newFixture(t, "0.3.0")
	fe, err := f.src.LoadIndex(context.Background(), f.keys)
	if err != nil {
		t.Fatal(err)
	}
	rs := fe.Index.Releases
	write := func(expires time.Time) {
		data, err := Marshal(Index{Schema: 1, GeneratedAt: t0, ExpiresAt: expires, Releases: rs})
		if err != nil {
			t.Fatal(err)
		}
		sig, _ := corepkg.Sign(data, testSeed)
		p := filepath.Join(f.feed, "updates-index.json")
		os.WriteFile(p, data, 0o644)
		os.WriteFile(p+".sig", sig, 0o644)
	}
	write(time.Now().Add(time.Hour))
	if _, err := f.src.LoadIndex(context.Background(), f.keys); err != nil {
		t.Fatalf("valid index: %v", err)
	}
	write(time.Now().Add(-time.Hour))
	if _, err := f.src.LoadIndex(context.Background(), f.keys); err == nil || !strings.Contains(err.Error(), "expired") {
		t.Fatalf("expired index: %v", err)
	}
}

func TestReleaseRejectsUnsafeURLCharacters(t *testing.T) {
	for _, bad := range []string{"https://x.example/a;rm", "https://x.example/a b", "https://x.example/$(id)", "https://x.example/a'b", "https://x.example/a\"b", "https://x.example/a`b"} {
		a := art("linux-amd64", KindDeb, "framebeam-hub_1.0.0_amd64.deb")
		a.Size, a.SHA256, a.URL = 10, goodSHA, bad
		r := rel("hub", "beta", "1.0.0", a)
		r.Commit, r.PublishedAt = strings.Repeat("a", 40), t0
		if err := ValidateReleaseOpts(r, false); err == nil {
			t.Fatalf("%q accepted", bad)
		}
	}
}
