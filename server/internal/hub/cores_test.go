package hub_test

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func coreSvc(t *testing.T, src *hubtest.CoreSource) *hub.Service {
	t.Helper()
	svc, _ := hubtest.New(t, func(o *hub.Options) {
		if src != nil {
			src.Apply(o)
		}
	})
	return svc
}

func lib(n int, b byte) []byte { return bytes.Repeat([]byte{b}, n) }

func cacheFiles(t *testing.T, svc *hub.Service) []string {
	t.Helper()
	var out []string
	filepath.Walk(filepath.Join(svc.DataDir(), "cores"), func(p string, fi os.FileInfo, err error) error {
		if err == nil && !fi.IsDir() {
			out = append(out, p)
		}
		return nil
	})
	return out
}

func TestSyncCoresDownloadsSelectedVersionForAllPlatforms(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
	src.AddPackage(t, "melonds_ds", "1.4.0", "windows-x64", lib(200, 2))
	src.AddPackage(t, "melonds_ds", "1.5.0", "linux-x64", lib(300, 3))
	svc := coreSvc(t, src)
	// The seeded system "nds" expects melonds_ds 1.4.0.
	rep, err := svc.SyncCores(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if rep.Packages != 3 || rep.Downloaded != 4 {
		t.Fatalf("report %+v", rep)
	}
	if n := len(cacheFiles(t, svc)); n != 4 {
		t.Fatalf("cache files: %v", cacheFiles(t, svc))
	}
	for _, plat := range []string{"linux-x64", "windows-x64"} {
		p, err := svc.GetCorePackage(ctx, "melonds_ds", "1.4.0", plat)
		if err != nil || p.CachedFiles() != 2 {
			t.Fatalf("%s: %+v %v", plat, p, err)
		}
	}
	if p, _ := svc.GetCorePackage(ctx, "melonds_ds", "1.5.0", "linux-x64"); p.CachedFiles() != 0 {
		t.Fatal("unselected version was downloaded")
	}
	st, _ := svc.CoreSource(ctx)
	if st.LastCheck == nil || st.LastSuccess == nil || st.LastError != "" {
		t.Fatalf("state %+v", st)
	}
	// A second sync downloads nothing.
	before := src.Hits["/files/melonds_ds-1.4.0-linux-x64-melonds_ds.so"]
	if rep, err := svc.SyncCores(ctx); err != nil || rep.Downloaded != 0 {
		t.Fatalf("resync %+v %v", rep, err)
	}
	if src.Hits["/files/melonds_ds-1.4.0-linux-x64-melonds_ds.so"] != before {
		t.Fatal("cached file downloaded again")
	}
	// Served version: expected 1.4.0; with "any version" the highest fully cached one (1.5.0 is not cached).
	if v, _ := svc.CoreServedVersion(ctx, "melonds_ds", "1.4.0"); v != "1.4.0" {
		t.Fatal(v)
	}
	if v, _ := svc.CoreServedVersion(ctx, "melonds_ds", ""); v != "1.4.0" {
		t.Fatalf("served %q", v)
	}
	if vs, _ := svc.CoreVersions(ctx, "melonds_ds"); strings.Join(vs, ",") != "1.5.0,1.4.0" {
		t.Fatal(vs)
	}
}

func TestSyncCoresRejectsBadFilesLeavesNothingBehind(t *testing.T) {
	for name, bad := range map[string][]byte{"size": lib(99, 1), "longer": lib(101, 1), "hash": lib(100, 9)} {
		src := hubtest.NewCoreSource(t)
		src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
		src.SetFile("/files/melonds_ds-1.4.0-linux-x64-melonds_ds.so", bad)
		svc := coreSvc(t, src)
		rep, err := svc.SyncCores(ctx)
		if err == nil || len(rep.Problems) != 1 {
			t.Fatalf("%s: %+v %v", name, rep, err)
		}
		for _, f := range cacheFiles(t, svc) {
			if strings.Contains(f, ".so") || strings.Contains(f, ".tmp-") {
				t.Fatalf("%s: left behind %s", name, f)
			}
		}
		p, _ := svc.GetCorePackage(ctx, "melonds_ds", "1.4.0", "linux-x64")
		if p.Files[0].Available {
			t.Fatalf("%s: marked available", name)
		}
		if st, _ := svc.CoreSource(ctx); st.LastError == "" {
			t.Fatalf("%s: error not recorded", name)
		}
		if _, f, err := svc.OpenCoreFile(ctx, "melonds_ds", "1.4.0", "linux-x64", "melonds_ds.so"); err == nil {
			t.Fatal("unavailable file opened")
		} else {
			_ = f
		}
	}
}

func TestSyncCoresSignatureFailureKeepsState(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
	svc := coreSvc(t, src)
	if _, err := svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	src.AddPackage(t, "melonds_ds", "1.9.0", "linux-x64", lib(50, 5))
	src.Tamper()
	if _, err := svc.SyncCores(ctx); err == nil || !strings.Contains(err.Error(), "signature") {
		t.Fatalf("tampered index accepted: %v", err)
	}
	if vs, _ := svc.CoreVersions(ctx, "melonds_ds"); strings.Join(vs, ",") != "1.4.0" {
		t.Fatalf("state changed: %v", vs)
	}
	if st, _ := svc.CoreSource(ctx); !strings.Contains(st.LastError, "signature") {
		t.Fatalf("error %q", st.LastError)
	}
	// Wrong key: the source signs with a key the Hub does not trust.
	otherSeed := make([]byte, 32)
	otherSeed[0] = 1
	otherPub, _ := corepkg.PublicFromSeed(otherSeed)
	wrong, _ := hubtest.New(t, func(o *hub.Options) {
		src.Apply(o)
		o.CoreTrustKeys = []ed25519.PublicKey{otherPub}
	})
	src.Publish(t) // valid signature again, but by an untrusted key
	if _, err := wrong.SyncCores(ctx); err == nil || !strings.Contains(err.Error(), "not trusted") {
		t.Fatalf("untrusted key accepted: %v", err)
	}
	if vs, _ := wrong.CoreVersions(ctx, "melonds_ds"); len(vs) != 0 {
		t.Fatal("state recorded from an untrusted index")
	}
}

func TestSyncCoresNoTrustedKeyAndNetworkFailure(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
	nokey, _ := hubtest.New(t, func(o *hub.Options) { o.CoreIndexURL = src.IndexURL(); o.CoreHTTPClient = src.Server.Client() })
	if _, err := nokey.SyncCores(ctx); err == nil || err.Error() != "no trusted signing key configured" {
		t.Fatalf("err %v", err)
	}
	if src.Hits["/cores-index.json"] != 0 {
		t.Fatal("index fetched without a trusted key")
	}
	if st, _ := nokey.CoreSource(ctx); st.LastError != "no trusted signing key configured" || st.LastCheck == nil {
		t.Fatalf("state %+v", st)
	}

	svc := coreSvc(t, src)
	if _, err := svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	src.Down = true
	if _, err := svc.SyncCores(ctx); err == nil {
		t.Fatal("network failure not reported")
	}
	if st, _ := svc.CoreSource(ctx); st.LastError == "" {
		t.Fatal("network failure not recorded")
	}
	f, _, err := svc.OpenCoreFile(ctx, "melonds_ds", "1.4.0", "linux-x64", "melonds_ds.so")
	if err != nil {
		t.Fatalf("cache not served after failure: %v", err)
	}
	f.Close()
	// Success clears the error.
	src.Down = false
	if _, err := svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	if st, _ := svc.CoreSource(ctx); st.LastError != "" {
		t.Fatal("error not cleared")
	}
}

func TestSyncCoresKeepsSelectedVersionAndDropsOthers(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
	src.AddPackage(t, "melonds_ds", "1.3.0", "linux-x64", lib(100, 2))
	svc := coreSvc(t, src)
	if _, err := svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	// Cache the non-selected version too (import-like), then drop both from the source.
	src.RemovePackage(t, "melonds_ds", "1.4.0")
	src.RemovePackage(t, "melonds_ds", "1.3.0")
	src.AddPackage(t, "melonds_ds", "2.0.0", "linux-x64", lib(10, 3))
	if _, err := svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	if vs, _ := svc.CoreVersions(ctx, "melonds_ds"); strings.Join(vs, ",") != "2.0.0,1.4.0" {
		t.Fatalf("%v", vs)
	}
	// The selected version keeps its cached files.
	f, _, err := svc.OpenCoreFile(ctx, "melonds_ds", "1.4.0", "linux-x64", "melonds_ds.so")
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
}

func TestExpectedVersionChangeDownloadsInBackground(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
	src.AddPackage(t, "melonds_ds", "1.5.0", "linux-x64", lib(120, 2))
	svc := coreSvc(t, src)
	c, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	var reports atomic.Int32
	go func() { svc.RunCoreSync(c, time.Hour, func(hub.CoreSyncReport, error) { reports.Add(1) }); close(done) }()
	waitFor(t, func() bool { return reports.Load() >= 1 })
	if err := svc.SetExpectedCoreVersion(ctx, "nds", "1.5.0"); err != nil {
		t.Fatal(err)
	}
	waitFor(t, func() bool {
		p, err := svc.GetCorePackage(ctx, "melonds_ds", "1.5.0", "linux-x64")
		return err == nil && p.CachedFiles() == 2
	})
	// "Check source now".
	n := reports.Load()
	svc.TriggerCoreSync()
	waitFor(t, func() bool { return reports.Load() > n })
	cancel()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("RunCoreSync did not stop on shutdown")
	}
}

func waitFor(t *testing.T, cond func() bool) {
	t.Helper()
	for i := 0; i < 500; i++ {
		if cond() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("timeout")
}

func TestImportCores(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	p := src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", lib(100, 1))
	dir := t.TempDir()
	idx, sig := src.Index()
	os.WriteFile(filepath.Join(dir, "cores-index.json"), idx, 0o644)
	os.WriteFile(filepath.Join(dir, "cores-index.json.sig"), sig, 0o644)
	// Library under its URL's last segment, license under its plain name, via a bad file for a mismatch test later.
	os.WriteFile(filepath.Join(dir, "melonds_ds-1.4.0-linux-x64-melonds_ds.so"), lib(100, 1), 0o644)
	os.WriteFile(filepath.Join(dir, "LICENSE.txt"), []byte("license of melonds_ds"), 0o644)

	svc, _ := hubtest.New(t, func(o *hub.Options) { o.CoreTrustKeys = []ed25519.PublicKey{src.Pub} })
	sum, err := svc.ImportCores(ctx, dir)
	if err != nil || sum.Packages != 1 || sum.Copied != 2 || sum.Missing != 0 || sum.Rejected != 0 {
		t.Fatalf("%+v %v", sum, err)
	}
	f, def, err := svc.OpenCoreFile(ctx, "melonds_ds", "1.4.0", "linux-x64", p.Files[0].Name)
	if err != nil || def.Size != 100 {
		t.Fatal(err)
	}
	f.Close()
	// Second import: already cached.
	if sum, _ := svc.ImportCores(ctx, dir); sum.AlreadyCached != 2 || sum.Copied != 0 {
		t.Fatalf("%+v", sum)
	}

	// Wrong content is rejected and nothing is left behind.
	svc2, _ := hubtest.New(t, func(o *hub.Options) { o.CoreTrustKeys = []ed25519.PublicKey{src.Pub} })
	os.WriteFile(filepath.Join(dir, "melonds_ds-1.4.0-linux-x64-melonds_ds.so"), lib(100, 7), 0o644)
	sum, err = svc2.ImportCores(ctx, dir)
	if err != nil || sum.Rejected != 1 || sum.Copied != 1 {
		t.Fatalf("%+v %v", sum, err)
	}
	for _, f := range cacheFiles(t, svc2) {
		if strings.Contains(f, ".so") || strings.Contains(f, ".tmp-") {
			t.Fatal("left behind", f)
		}
	}
	// Tampered index / no key.
	os.WriteFile(filepath.Join(dir, "cores-index.json"), append([]byte(" "), idx...), 0o644)
	if _, err := svc2.ImportCores(ctx, dir); err == nil {
		t.Fatal("tampered index imported")
	}
	nokey, _ := hubtest.New(t, nil)
	if _, err := nokey.ImportCores(ctx, dir); err == nil || err.Error() != "no trusted signing key configured" {
		t.Fatal(err)
	}
	_ = corepkg.DefaultIndexURL
}

func TestSyncCoresAnyVersionDownloadsHighest(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.9.0", "linux-x64", lib(10, 1))
	src.AddPackage(t, "melonds_ds", "1.10.0", "linux-x64", lib(20, 2))
	src.AddPackage(t, "melonds_ds", "1.10.0", "windows-x64", lib(30, 3))
	svc := coreSvc(t, src)
	if err := svc.SetExpectedCoreVersion(ctx, "nds", ""); err != nil {
		t.Fatal(err)
	}
	rep, err := svc.SyncCores(ctx)
	if err != nil || rep.Downloaded != 4 {
		t.Fatalf("%+v %v", rep, err)
	}
	if p, _ := svc.GetCorePackage(ctx, "melonds_ds", "1.9.0", "linux-x64"); p.CachedFiles() != 0 {
		t.Fatal("older version downloaded")
	}
	if v, _ := svc.CoreServedVersion(ctx, "melonds_ds", ""); v != "1.10.0" {
		t.Fatalf("served %q", v)
	}
	// Dropped from the index later: the served version stays protected.
	src.RemovePackage(t, "melonds_ds", "1.10.0")
	src.RemovePackage(t, "melonds_ds", "1.9.0")
	src.AddPackage(t, "melonds_ds", "1.8.0", "linux-x64", lib(5, 4))
	if _, err := svc.SyncCores(ctx); err != nil {
		t.Fatal(err)
	}
	f, _, err := svc.OpenCoreFile(ctx, "melonds_ds", "1.10.0", "windows-x64", "melonds_ds.so")
	if err != nil {
		t.Fatalf("served version lost: %v", err)
	}
	f.Close()
}
