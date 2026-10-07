package hub_test

import (
	"bytes"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
	"github.com/phabioo/framebeam/server/internal/updates"
)

type updFeed struct {
	dir  string
	pub  ed25519.PublicKey
	seed []byte
	rels []updates.Release
}

func newUpdFeed(t *testing.T) *updFeed {
	t.Helper()
	f := &updFeed{dir: t.TempDir(), seed: bytes.Repeat([]byte{7}, 32)}
	f.pub, _ = corepkg.PublicFromSeed(f.seed)
	return f
}

func (f *updFeed) url() string { return "file://" + filepath.Join(f.dir, "updates-index.json") }

// add publishes a hub release with a dummy .deb for linux-amd64 and re-signs the index.
func (f *updFeed) add(t *testing.T, channel, version string, minProto int) {
	t.Helper()
	name := "framebeam-hub_" + version + "_amd64.deb"
	data := []byte("dummy deb " + version)
	if err := os.WriteFile(filepath.Join(f.dir, name), data, 0o644); err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256(data)
	f.rels = append(f.rels, updates.Release{Product: "hub", Channel: channel, Version: version, PublishedAt: time.Now().UTC().Truncate(time.Second),
		ProtocolVersion: 3, MinProtocolVersion: minProto, NotesURL: "https://example.org/notes",
		Artifacts: []updates.Artifact{{Platform: "linux-amd64", Kind: "deb", Name: name, Size: int64(len(data)),
			SHA256: hex.EncodeToString(sum[:]), URL: "file://" + filepath.Join(f.dir, name)}}})
	b, err := updates.Marshal(updates.Index{Schema: 1, GeneratedAt: time.Now().UTC(), Releases: f.rels})
	if err != nil {
		t.Fatal(err)
	}
	sig, _ := corepkg.Sign(b, f.seed)
	p := filepath.Join(f.dir, "updates-index.json")
	os.WriteFile(p, b, 0o644)
	os.WriteFile(p+".sig", sig, 0o644)
}

type updEnv struct {
	svc    *hub.Service
	clk    *hubtest.Clock
	feed   *updFeed
	reqDir string
}

// newUpdEnv builds a Hub running version `current`, compiled channel `compiled`, "installed" as the package.
func newUpdEnv(t *testing.T, current, compiled string, packaged bool) *updEnv {
	t.Helper()
	e := &updEnv{feed: newUpdFeed(t), reqDir: t.TempDir()}
	e.svc, e.clk = hubtest.New(t, func(o *hub.Options) {
		o.HubVersion, o.UpdateChannel = current, compiled
		o.UpdateIndexURL, o.UpdateRequestDir, o.UpdatePlatform = e.feed.url(), e.reqDir, "linux-amd64"
		o.CoreTrustKeys = []ed25519.PublicKey{e.feed.pub}
		if packaged {
			o.Executable = updates.PackagedExecutable
		} else {
			o.Executable = "/usr/local/bin/framebeam-hub"
		}
	})
	return e
}

func TestUpdateSettingsDefaults(t *testing.T) {
	for _, c := range []struct {
		compiled, channel string
		auto              bool
	}{{"test", "test", true}, {"stable", "stable", false}, {"dev", "off", false}} {
		e := newUpdEnv(t, "0.3.0", c.compiled, true)
		s, err := e.svc.UpdateSettings(ctx)
		if err != nil || s.Channel != c.channel || s.Auto != c.auto || s.ChannelIsSet {
			t.Fatalf("%s: %+v %v", c.compiled, s, err)
		}
	}
	e := newUpdEnv(t, "0.3.0", "dev", true)
	if err := e.svc.SetUpdateSettings(ctx, "test", false); err != nil {
		t.Fatal(err)
	}
	s, _ := e.svc.UpdateSettings(ctx)
	if s.Channel != "test" || s.Auto || !s.ChannelIsSet {
		t.Fatalf("selection overrides the compiled default: %+v", s)
	}
	if err := e.svc.SetUpdateSettings(ctx, "nightly", true); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatal(err)
	}
	if err := e.svc.SetUpdateSettings(ctx, "", false); err != nil {
		t.Fatal(err)
	}
	if s, _ = e.svc.UpdateSettings(ctx); s.Channel != "off" {
		t.Fatalf("empty resets to the compiled default: %+v", s)
	}
}

func TestCheckUpdatesDevAndNonSemVerAreOff(t *testing.T) {
	e := newUpdEnv(t, "0.3.0-dev", "dev", true)
	e.feed.add(t, "stable", "0.4.0", 1)
	if _, err := e.svc.CheckUpdates(ctx); !errors.Is(err, hub.ErrUpdatesOff) {
		t.Fatalf("dev channel: %v", err)
	}
	e2 := newUpdEnv(t, "dev", "test", true)
	e2.feed.add(t, "stable", "0.4.0", 1)
	if _, err := e2.svc.CheckUpdates(ctx); !errors.Is(err, hub.ErrUpdatesOff) {
		t.Fatalf("non-SemVer version: %v", err)
	}
	st, _ := e2.svc.UpdateStatus(ctx)
	if st.Disabled == "" || st.Available != nil {
		t.Fatalf("%+v", st)
	}
	// An explicit selection enables a dev build.
	e.svc.SetUpdateSettings(ctx, "stable", false)
	rep, err := e.svc.CheckUpdates(ctx)
	if err != nil || rep.Available == nil || rep.Available.Version != "0.4.0" {
		t.Fatalf("%+v %v", rep, err)
	}
}

func TestCheckUpdatesRecordsAvailableAndErrors(t *testing.T) {
	e := newUpdEnv(t, "0.3.0-test.5", "test", false)
	e.feed.add(t, "test", "0.3.0-test.6", 1)
	e.feed.add(t, "stable", "0.3.0", 1)
	rep, err := e.svc.CheckUpdates(ctx)
	if err != nil || rep.Available == nil || rep.Available.Version != "0.3.0" || rep.Staged {
		t.Fatalf("test sees stable too, highest wins: %+v %v", rep, err)
	}
	st, _ := e.svc.UpdateStatus(ctx)
	if st.Available == nil || st.LastCheck == nil || st.LastError != "" || st.Packaged || st.ManualCommand == "" || !e.svc.UpdateAvailableBadge(ctx) {
		t.Fatalf("%+v", st)
	}
	if _, err := e.svc.InstallUpdate(ctx, false); !errors.Is(err, hub.ErrNotPackaged) {
		t.Fatalf("script install cannot self-update: %v", err)
	}
	// A broken source is recorded and returned, nothing else.
	os.Remove(filepath.Join(e.feed.dir, "updates-index.json.sig"))
	if _, err := e.svc.CheckUpdates(ctx); err == nil {
		t.Fatal("missing signature must fail")
	}
	if st, _ = e.svc.UpdateStatus(ctx); st.LastError == "" {
		t.Fatal("error must be recorded")
	}
}

func TestInstallUpdateStagesAndRequests(t *testing.T) {
	e := newUpdEnv(t, "0.3.0-test.5", "test", true)
	if _, err := e.svc.InstallUpdate(ctx, false); !errors.Is(err, hub.ErrNoUpdate) && err == nil {
		t.Fatal("empty feed")
	}
	e.feed.add(t, "test", "0.3.0-test.6", 1)
	st, err := e.svc.InstallUpdate(ctx, false)
	if err != nil || st.Version != "0.3.0-test.6" {
		t.Fatalf("%+v %v", st, err)
	}
	if !updates.RequestPending(e.reqDir) {
		t.Fatal("request file missing")
	}
	got, _ := updates.ReadStaged(e.svc.DataDir())
	if got == nil || got.Version != "0.3.0-test.6" {
		t.Fatalf("%+v", got)
	}
	status, _ := e.svc.UpdateStatus(ctx)
	if !status.Packaged || !status.RequestPending || status.Staged == nil {
		t.Fatalf("%+v", status)
	}
}

func TestAutomaticInstall(t *testing.T) {
	// Test channel default: on.
	e := newUpdEnv(t, "0.3.0-test.5", "test", true)
	e.feed.add(t, "test", "0.3.0-test.6", 1)
	rep, err := e.svc.CheckUpdates(ctx)
	if err != nil || !rep.Staged || !updates.RequestPending(e.reqDir) {
		t.Fatalf("%+v %v", rep, err)
	}
	// Already requested for this version: not staged again.
	if rep, _ = e.svc.CheckUpdates(ctx); rep.Staged {
		t.Fatal("must not stage twice")
	}

	// Stable channel default: off, only reported.
	s := newUpdEnv(t, "0.2.0", "stable", true)
	s.feed.add(t, "stable", "0.3.0", 1)
	if rep, err = s.svc.CheckUpdates(ctx); err != nil || rep.Staged || rep.Available == nil || updates.RequestPending(s.reqDir) {
		t.Fatalf("stable must not auto-install: %+v %v", rep, err)
	}

	// Not while a Session is active; retried at the next check.
	a := newUpdEnv(t, "0.3.0-test.5", "test", true)
	a.feed.add(t, "test", "0.3.0-test.6", 1)
	admin, _ := a.svc.CreateAdmin(ctx, "fabio", "secret-1234")
	g, err := a.svc.AddROM(ctx, bytes.NewReader(randomROM(2000)), "demo.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	owner := principal(t, a.svc, admin.ID, "Desktop")
	sess, err := a.svc.PublishSession(ctx, owner, g.ID, hub.VisibilityPrivate)
	if err != nil {
		t.Fatal(err)
	}
	if rep, err = a.svc.CheckUpdates(ctx); err != nil || rep.Staged || rep.Available == nil || updates.RequestPending(a.reqDir) {
		t.Fatalf("active session: %+v %v", rep, err)
	}
	if err := a.svc.EndSession(ctx, owner, sess.SessionID); err != nil {
		t.Fatal(err)
	}
	if rep, err = a.svc.CheckUpdates(ctx); err != nil || !rep.Staged {
		t.Fatalf("retry after the Session ended: %+v %v", rep, err)
	}

	// A failed earlier attempt for the same version is not retried automatically.
	f := newUpdEnv(t, "0.3.0-test.5", "test", true)
	f.feed.add(t, "test", "0.3.0-test.6", 1)
	os.MkdirAll(filepath.Join(f.svc.DataDir(), "updates"), 0o750)
	os.WriteFile(filepath.Join(f.svc.DataDir(), "updates", updates.LastResultName), []byte(`{"version":"0.3.0-test.6","ok":false,"message":"x"}`), 0o640)
	if rep, _ = f.svc.CheckUpdates(ctx); rep.Staged {
		t.Fatal("failed attempt must not loop")
	}
}

func TestBreakingUpdateWarnsAndNeedsConfirmation(t *testing.T) {
	e := newUpdEnv(t, "0.3.0-test.5", "test", true)
	admin, _ := e.svc.CreateAdmin(ctx, "fabio", "secret-1234")
	dev := pairDevice(t, e.svc, admin.ID, "Old Player")
	cores := []hub.CoreReport{}
	if _, err := e.svc.Handshake(ctx, dev, hub.HandshakeInput{Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1,
		MinProtocolVersion: 1, Cores: &cores}); err != nil {
		t.Fatal(err)
	}
	e.feed.add(t, "test", "0.3.0-test.6", 2) // needs Players with protocol >= 2
	rep, err := e.svc.CheckUpdates(ctx)
	if err != nil || rep.Available == nil || !rep.Available.Breaking || rep.Available.Warning() == "" || rep.Staged {
		t.Fatalf("breaking must warn and skip automatic install: %+v %v", rep, err)
	}
	if _, err := e.svc.InstallUpdate(ctx, false); !errors.Is(err, hub.ErrUpdateBreaking) {
		t.Fatalf("manual install needs confirmation: %v", err)
	}
	if _, err := e.svc.InstallUpdate(ctx, true); err != nil {
		t.Fatalf("confirmed: %v", err)
	}
	// A Player not seen for 30 days no longer counts.
	e2 := newUpdEnv(t, "0.3.0-test.5", "test", true)
	admin2, _ := e2.svc.CreateAdmin(ctx, "fabio", "secret-1234")
	dev2 := pairDevice(t, e2.svc, admin2.ID, "Old Player")
	e2.svc.Handshake(ctx, dev2, hub.HandshakeInput{Platform: "linux", Arch: "x86_64", PlayerVersion: "0.1.0", ProtocolVersion: 1, MinProtocolVersion: 1, Cores: &cores})
	e2.feed.add(t, "test", "0.3.0-test.6", 2)
	e2.clk.Advance(31 * 24 * time.Hour)
	if rep, err := e2.svc.CheckUpdates(ctx); err != nil || rep.Available == nil || rep.Available.Breaking {
		t.Fatalf("a Player not seen for 30 days must not count: %+v %v", rep, err)
	}
}
