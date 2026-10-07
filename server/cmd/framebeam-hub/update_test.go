package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/updates"
	"github.com/phabioo/framebeam/server/internal/version"
)

func setVersion(t *testing.T, v, ch, commit string) {
	t.Helper()
	ov, oc, om := version.Version, version.Channel, version.Commit
	t.Cleanup(func() { version.Version, version.Channel, version.Commit = ov, oc, om })
	version.Version, version.Channel, version.Commit = v, ch, commit
}

func TestVersionJSON(t *testing.T) {
	setVersion(t, "0.3.0-beta.57", "beta", "abc123")
	var out bytes.Buffer
	if err := runVersion([]string{"--json"}, &out); err != nil {
		t.Fatal(err)
	}
	if strings.Count(out.String(), "\n") != 1 {
		t.Fatalf("must be one line: %q", out.String())
	}
	var m map[string]any
	if err := json.Unmarshal(out.Bytes(), &m); err != nil {
		t.Fatal(err)
	}
	want := map[string]any{"product": "hub", "version": "0.3.0-beta.57", "channel": "beta", "commit": "abc123", "protocol_version": 1.0, "min_protocol_version": 1.0}
	if len(m) != len(want) {
		t.Fatalf("%v", m)
	}
	for k, v := range want {
		if m[k] != v {
			t.Fatalf("%s = %v, want %v", k, m[k], v)
		}
	}
	out.Reset()
	if err := runVersion(nil, &out); err != nil || out.String() != "0.3.0-beta.57\n" {
		t.Fatalf("%q %v", out.String(), err)
	}
	// Defaults.
	setVersion(t, "dev", "dev", "")
	out.Reset()
	runVersion([]string{"-json"}, &out)
	if !strings.Contains(out.String(), `"channel":"dev"`) || !strings.Contains(out.String(), `"commit":""`) {
		t.Fatalf("%q", out.String())
	}
}

type cliFeed struct {
	dir    string
	pubB64 string
	name   string
	data   []byte
}

func newCLIFeed(t *testing.T, ver string) *cliFeed {
	t.Helper()
	seed := bytes.Repeat([]byte{5}, 32)
	pub, _ := corepkg.PublicFromSeed(seed)
	f := &cliFeed{dir: t.TempDir(), pubB64: base64.StdEncoding.EncodeToString(pub), data: []byte("dummy deb " + ver)}
	f.name = "framebeam-hub_" + strings.ReplaceAll(ver, "-", "~") + "_" + strings.TrimPrefix(updates.LinuxPlatform(), "linux-") + ".deb"
	os.WriteFile(filepath.Join(f.dir, f.name), f.data, 0o644)
	sum := sha256.Sum256(f.data)
	idx, err := updates.Marshal(updates.Index{Schema: 1, GeneratedAt: time.Now().UTC(), Releases: []updates.Release{{
		Product: "hub", Channel: "beta", Version: ver, PublishedAt: time.Now().UTC(), ProtocolVersion: 1, MinProtocolVersion: 1,
		Artifacts: []updates.Artifact{{Platform: updates.LinuxPlatform(), Kind: "deb", Name: f.name, Size: int64(len(f.data)),
			SHA256: hex.EncodeToString(sum[:]), URL: "file://" + filepath.Join(f.dir, f.name)}}}}})
	if err != nil {
		t.Fatal(err)
	}
	sig, _ := corepkg.Sign(idx, seed)
	p := filepath.Join(f.dir, "updates-index.json")
	os.WriteFile(p, idx, 0o644)
	os.WriteFile(p+".sig", sig, 0o644)
	return f
}

func (f *cliFeed) flags(data, req string) []string {
	return []string{"-data-dir", data, "-update-index-url", "file://" + filepath.Join(f.dir, "updates-index.json"), "-core-trust-key", f.pubB64,
		"-update-request-dir", req}
}

func TestUpdateCheckAndStage(t *testing.T) {
	setVersion(t, "0.3.0-beta.5", "beta", "")
	f := newCLIFeed(t, "0.3.0-beta.6")
	data, req := t.TempDir(), t.TempDir()

	var out bytes.Buffer
	if err := runUpdate(append([]string{"check", "-channel", "beta"}, f.flags(data, req)...), &out); err != nil {
		t.Fatal(err)
	}
	var res updateCheckJSON
	if err := json.Unmarshal(out.Bytes(), &res); err != nil {
		t.Fatalf("%v: %s", err, out.String())
	}
	if res.UpToDate || res.Available == nil || res.Available.Version != "0.3.0-beta.6" || res.Available.Artifact.Name != f.name || res.Staged != nil {
		t.Fatalf("%+v", res)
	}
	if updates.RequestPending(req) {
		t.Fatal("check must not create a request")
	}

	// Build channel dev without -channel: off.
	setVersion(t, "0.3.0-dev", "dev", "")
	if err := runUpdate(append([]string{"check"}, f.flags(data, req)...), &out); err == nil {
		t.Fatal("dev build without a channel must refuse")
	}
	setVersion(t, "0.3.0-beta.5", "beta", "")

	out.Reset()
	if err := runUpdate(append([]string{"stage", "-channel", "beta"}, f.flags(data, req)...), &out); err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(out.Bytes(), &res); err != nil || res.Staged == nil || res.Staged.Version != "0.3.0-beta.6" {
		t.Fatalf("%v %s", err, out.String())
	}
	if !updates.RequestPending(req) {
		t.Fatal("request file missing")
	}
	if st, _ := updates.ReadStaged(data); st == nil || st.Artifact != f.name {
		t.Fatalf("%+v", st)
	}

	// Up to date: nothing staged.
	setVersion(t, "0.3.0-beta.6", "beta", "")
	out.Reset()
	if err := runUpdate(append([]string{"check", "-channel", "beta"}, f.flags(data, req)...), &out); err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(out.Bytes(), &res); err != nil || !res.UpToDate || res.Available != nil {
		t.Fatalf("%v %+v", err, res)
	}
	// Untrusted key.
	setVersion(t, "0.3.0-beta.5", "beta", "")
	bad := append([]string{"check", "-channel", "beta"}, f.flags(data, req)...)
	for i, a := range bad {
		if a == f.pubB64 {
			bad[i] = base64.StdEncoding.EncodeToString(bytes.Repeat([]byte{1}, 32))
		}
	}
	if err := runUpdate(bad, &out); err == nil || !strings.Contains(err.Error(), "not trusted") {
		t.Fatalf("%v", err)
	}
}

func TestApplyStagedCommand(t *testing.T) {
	setVersion(t, "0.3.0-beta.5", "beta", "")
	f := newCLIFeed(t, "0.3.0-beta.6")
	data, req := t.TempDir(), t.TempDir()
	var out bytes.Buffer
	if err := runUpdate(append([]string{"stage", "-channel", "beta"}, f.flags(data, req)...), &out); err != nil {
		t.Fatal(err)
	}
	t.Setenv("FRAMEBEAM_DATA_DIR", data)
	t.Setenv("FRAMEBEAM_HUB_CORE_TRUST_KEYS", f.pubB64)
	t.Setenv("FRAMEBEAM_HUB_UPDATE_REQUEST_DIR", req)

	var calls [][]string
	run := func(_ context.Context, name string, args ...string) ([]byte, error) {
		calls = append(calls, append([]string{name}, args...))
		return nil, nil
	}
	// Replay: the running version is not older.
	setVersion(t, "0.3.0-beta.6", "beta", "")
	out.Reset()
	if err := runApplyStaged(nil, &out, run); err == nil || len(calls) != 0 {
		t.Fatalf("replay accepted: %v %v", err, calls)
	}
	if updates.RequestPending(req) {
		t.Fatal("the request file is deleted first, also on failure")
	}
	r, _ := updates.ReadResult(data)
	if r == nil || r.OK {
		t.Fatalf("%+v", r)
	}

	// Good path (stage again: the request file is gone).
	setVersion(t, "0.3.0-beta.5", "beta", "")
	updates.WriteRequest(req, "0.3.0-beta.6")
	out.Reset()
	if err := runApplyStaged(nil, &out, run); err != nil {
		t.Fatal(err)
	}
	if len(calls) != 1 || calls[0][0] != "dpkg" || calls[0][1] != "-i" || !strings.HasSuffix(calls[0][2], f.name) {
		t.Fatalf("%v", calls)
	}
	if !strings.Contains(out.String(), "0.3.0-beta.6") {
		t.Fatal(out.String())
	}
	// A failing dpkg is an error.
	updates.WriteRequest(req, "x")
	fail := func(context.Context, string, ...string) ([]byte, error) { return []byte("boom"), errors.New("exit 1") }
	if err := runApplyStaged(nil, &out, fail); err == nil {
		t.Fatal("dpkg failure must fail")
	}
}
