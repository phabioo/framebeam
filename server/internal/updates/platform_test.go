package updates

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestPlatformAndKind(t *testing.T) {
	if Platform() != runtime.GOOS+"-"+runtime.GOARCH {
		t.Fatal(Platform())
	}
	for platform, kind := range map[string]string{"linux-amd64": KindDeb, "linux-arm64": KindDeb, "windows-amd64": KindMSI} {
		if KindFor(platform) != kind {
			t.Fatalf("%s: %s", platform, KindFor(platform))
		}
	}
}

func TestPlatformKindsInIndex(t *testing.T) {
	ok := func(product, platform, kind string) bool {
		for _, k := range platformKinds[product][platform] {
			if k == kind {
				return true
			}
		}
		return false
	}
	if !ok(ProductHub, "windows-amd64", KindMSI) || !ok(ProductHub, "windows-amd64", KindBinary) || ok(ProductHub, "windows-amd64", KindDeb) ||
		!ok(ProductPlayer, "windows-x64", KindMSI) || !ok(ProductPlayer, "windows-x64", KindInstaller) || !ok(ProductHub, "linux-amd64", KindDeb) {
		t.Fatalf("%v", platformKinds)
	}
	r := rel("hub", "stable", "0.9.0", art("windows-amd64", KindMSI, "FrameBeam-Hub-0.9.0.msi"))
	if _, errs := ParseIndex(indexJSON(t, 1, r)); len(errs) != 0 {
		t.Fatal(errs)
	}
	sel, err := Select(Index{Releases: []Release{r}}, Query{Product: ProductHub, Channel: ChannelStable, Platform: "windows-amd64", Kind: KindFor("windows-amd64"), Current: "0.8.0"})
	if err != nil || sel.Release == nil || sel.Artifact.Kind != KindMSI {
		t.Fatalf("%+v %v", sel, err)
	}
}

func TestInstallCommand(t *testing.T) {
	name, args := InstallCommand("windows-amd64", `C:\p\a.msi`, `C:\p\msiexec.log`)
	if name != "msiexec.exe" || strings.Join(args, " ") != `/i C:\p\a.msi /qn /norestart /l*v C:\p\msiexec.log ALLUSERS=1` {
		t.Fatal(name, args)
	}
	name, args = InstallCommand("linux-amd64", "/p/a.deb", "/p/log")
	if name != "dpkg" || strings.Join(args, " ") != "-i /p/a.deb" {
		t.Fatal(name, args)
	}
}

func TestIsPackagedMarker(t *testing.T) {
	bin, req := t.TempDir(), t.TempDir()
	exe := filepath.Join(bin, "framebeam-hub.exe")
	if IsPackaged("windows-amd64", exe, req) {
		t.Fatal("no marker yet")
	}
	if err := os.WriteFile(filepath.Join(bin, MSIMarkerName), nil, 0o644); err != nil {
		t.Fatal(err)
	}
	if !IsPackaged("windows-amd64", exe, req) {
		t.Fatal("marker and writable request dir")
	}
	if IsPackaged("windows-amd64", exe, filepath.Join(req, "missing")) || IsPackaged("windows-amd64", "", req) {
		t.Fatal("request dir must be writable")
	}
	if IsPackaged("linux-amd64", exe, req) || !IsPackaged("linux-amd64", PackagedExecutable, req) {
		t.Fatal("linux detection uses the .deb path only")
	}
}

func TestDefaultPrivateDir(t *testing.T) {
	get := func(k string) string {
		if k == "ProgramData" {
			return "PD"
		}
		return ""
	}
	if DefaultPrivateDir("windows", get) != filepath.Join("PD", "FrameBeam", "HubUpdater") || DefaultPrivateDir("linux", get) != "" {
		t.Fatal("DefaultPrivateDir")
	}
}

func TestApplyStagedMSI(t *testing.T) {
	f := newFixtureFor(t, "0.9.1", "windows-amd64", KindMSI, "_amd64.msi")
	fe, err := f.src.LoadIndex(context.Background(), f.keys)
	if err != nil {
		t.Fatal(err)
	}
	sel, err := Select(fe.Index, Query{Product: ProductHub, Channel: ChannelBeta, Platform: "windows-amd64", Kind: KindMSI, Current: "0.9.0"})
	if err != nil || sel.Release == nil {
		t.Fatalf("%+v %v", sel, err)
	}
	if _, err := Stage(context.Background(), fe, *sel.Release, *sel.Artifact, f.dir); err != nil {
		t.Fatal(err)
	}
	fr := &fakeRunner{}
	res, err := f.apply(t, fr.run, "0.9.0", func(o *ApplyOptions) { o.Platform = "windows-amd64" })
	if err != nil || !res.OK {
		t.Fatal(res, err)
	}
	c := fr.calls[0]
	if len(c) != 8 || c[0] != "msiexec.exe" || c[1] != "/i" || c[3] != "/qn" || c[4] != "/norestart" || c[5] != "/l*v" || !fr.copyOK || string(fr.content) != string(f.debData) {
		t.Fatalf("calls=%v", fr.calls)
	}
	if strings.HasPrefix(c[2], f.dir) || !strings.HasSuffix(c[2], ".msi") {
		t.Fatal("msiexec must get the private copy")
	}
	// The deb of another platform is not accepted for the msi query.
	d := newFixture(t, "0.9.1")
	d.stage(t, "0.9.0")
	fr = &fakeRunner{}
	if _, err := d.apply(t, fr.run, "0.9.0", func(o *ApplyOptions) { o.Platform = "windows-amd64" }); err == nil || len(fr.calls) != 0 {
		t.Fatal("deb must not be installed as msi", err)
	}
}

func TestWatch(t *testing.T) {
	dir := t.TempDir()
	req := RequestPath(dir)
	if err := os.WriteFile(req, []byte("0.9.1\n"), 0o600); err != nil { // present at start: handled at once
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	var applied atomic.Int32
	errs := make(chan error, 4)
	done := make(chan struct{})
	go func() {
		defer close(done)
		Watch(ctx, WatchOptions{RequestFile: req, Interval: 5 * time.Millisecond,
			Apply: func(context.Context) error {
				applied.Add(1)
				os.Remove(req) // like ApplyStaged
				if applied.Load() == 2 {
					return errors.New("boom")
				}
				return nil
			},
			Done: func(err error) { errs <- err }})
	}()
	wait := func(want int32) {
		t.Helper()
		for i := 0; i < 400 && applied.Load() < want; i++ {
			time.Sleep(5 * time.Millisecond)
		}
		if applied.Load() != want {
			t.Fatalf("applied %d, want %d", applied.Load(), want)
		}
	}
	wait(1)
	time.Sleep(30 * time.Millisecond)
	if applied.Load() != 1 {
		t.Fatal("no request, no apply")
	}
	if err := os.WriteFile(req, nil, 0o600); err != nil {
		t.Fatal(err)
	}
	wait(2)
	if e1, e2 := <-errs, <-errs; e1 != nil || e2 == nil {
		t.Fatalf("Done errors: %v %v", e1, e2)
	}
	cancel()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("Watch did not stop with ctx")
	}
}
