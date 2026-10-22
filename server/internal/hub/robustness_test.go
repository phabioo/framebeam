package hub_test

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

// Package 7 (repo scan 2026-10-19): Server robustness fixes. One test per finding.

// HC-1: a CLI sub-command opens the data directory next to the running service and must not wipe its staging area.
func TestOpenWithoutCleanStagingKeepsTmp(t *testing.T) {
	dir := t.TempDir()
	tmp := filepath.Join(dir, "tmp")
	if err := os.MkdirAll(tmp, 0o750); err != nil {
		t.Fatal(err)
	}
	staged := filepath.Join(tmp, "rom-in-flight")
	if err := os.WriteFile(staged, []byte("x"), 0o640); err != nil {
		t.Fatal(err)
	}
	hubtest.New(t, func(o *hub.Options) { o.DataDir = dir; o.CleanStaging = false })
	if _, err := os.Stat(staged); err != nil {
		t.Fatalf("staging file of the running service removed: %v", err)
	}
	hubtest.New(t, func(o *hub.Options) { o.DataDir = dir; o.CleanStaging = true }) // the serving process cleans
	if _, err := os.Stat(staged); !os.IsNotExist(err) {
		t.Fatal("serving process did not clean its tmp")
	}
}

// HC-9: a firmware backup is the only copy when the target file is missing: it is restored, not deleted.
func TestOpenRestoresFirmwareBackupWhenTargetMissing(t *testing.T) {
	dir := t.TempDir()
	fw := filepath.Join(dir, "firmware", "nds")
	if err := os.MkdirAll(fw, 0o750); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(fw, "bios7.bin.bak"), []byte("old-bios"), 0o600); err != nil { // only the backup
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(fw, "bios9.bin"), []byte("new"), 0o600); err != nil { // target exists: leftover goes
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(fw, "bios9.bin.bak"), []byte("stale"), 0o600); err != nil {
		t.Fatal(err)
	}
	hubtest.New(t, func(o *hub.Options) { o.DataDir = dir })
	if b, err := os.ReadFile(filepath.Join(fw, "bios7.bin")); err != nil || string(b) != "old-bios" {
		t.Fatalf("backup not restored: %q %v", b, err)
	}
	if _, err := os.Stat(filepath.Join(fw, "bios7.bin.bak")); !os.IsNotExist(err) {
		t.Fatal("restored backup still present")
	}
	if _, err := os.Stat(filepath.Join(fw, "bios9.bin.bak")); !os.IsNotExist(err) {
		t.Fatal("stale backup kept")
	}
	if b, _ := os.ReadFile(filepath.Join(fw, "bios9.bin")); string(b) != "new" {
		t.Fatal("existing firmware file changed")
	}
}

// startSessionOnRead starts a Session for the game when the body is first read, i.e. while the upload is staged.
type startSessionOnRead struct {
	r     io.Reader
	start func()
	done  bool
}

func (s *startSessionOnRead) Read(p []byte) (int, error) {
	if !s.done {
		s.done = true
		s.start()
	}
	return s.r.Read(p)
}

// HC-6: a Session that starts while the save body is staged still blocks the upload.
func TestUploadSaveFileRechecksSessionInsideTransaction(t *testing.T) {
	e := newSavesEnv(t)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	p := principal(t, e.svc, e.user.ID, "Host")
	body := &startSessionOnRead{r: strings.NewReader("two"), start: func() {
		if _, err := e.svc.PublishSession(ctx, p, e.game.ID, hub.VisibilityPrivate); err != nil {
			t.Fatal(err)
		}
	}}
	_, err := e.svc.UploadSaveFile(ctx, hub.UploadSaveInput{UserID: e.user.ID, DeviceID: e.devB, GameID: e.game.ID, Slot: "default", Body: body})
	if !errors.Is(err, hub.ErrSessionRunning) {
		t.Fatalf("upload during a Session that started meanwhile: %v", err)
	}
	if string(e.content()) != "one" {
		t.Fatalf("checkpoint replaced: %q", e.content())
	}
}

// HC-8: a data file shorter than the session's offset restarts the session instead of storing a ROM that does not
// match its hash.
func TestResumedUploadWithTruncatedDataRestarts(t *testing.T) {
	dir := t.TempDir()
	svc, _, _ := hubtest.OpenIn(t, dir, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	data := ndsROM("ABCD")
	data = append(data, bytes.Repeat([]byte("rom"), 200)...)
	up, err := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "t.nds", Size: int64(len(data))}) // no hash announced
	if err != nil {
		t.Fatal(err)
	}
	if s, _, err := chunkUpload(t, svc, admin.ID, up.ID, data, 0, 300); err != nil || s.Offset != 300 {
		t.Fatalf("%+v %v", s, err)
	}
	// Something cut the data file (antivirus, manual cleanup).
	if err := os.Truncate(filepath.Join(dir, "tmp", "uploads", up.ID), 100); err != nil {
		t.Fatal(err)
	}
	if _, _, err := chunkUpload(t, svc, admin.ID, up.ID, data, 300, len(data)); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("append after truncation: %v", err)
	}
	st, err := svc.GetUpload(ctx, admin.ID, up.ID)
	if err != nil || st.Offset != 0 {
		t.Fatalf("session not restarted: %+v %v", st, err)
	}
	// The client continues at offset 0 and the stored ROM matches its hash.
	_, g, err := chunkUpload(t, svc, admin.ID, up.ID, data, 0, len(data))
	if err != nil || g == nil || g.ROMSHA256 != hexSHA(data) || g.ROMSize != int64(len(data)) {
		t.Fatalf("%+v %v", g, err)
	}
}

// HC-16: ROM uploads leave a reserve of free space for the database and the saves.
func TestUploadRefusedWithoutFreeSpace(t *testing.T) {
	svc, _, admin := newAdmin(t)
	restore := hub.SetFreeBytesForTest(func(string) uint64 { return uint64(hub.DiskReserveBytes) + 1000 })
	defer restore()
	if _, err := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "ok.nds", Size: 900}); err != nil {
		t.Fatalf("fits next to the reserve: %v", err)
	}
	_, err := svc.CreateUpload(ctx, admin.ID, hub.UploadInput{Filename: "big.nds", Size: 200}) // 900 pending + 200 > 1000
	var he *hub.Error
	if !errors.As(err, &he) || he.Code != hub.CodePayloadTooLarge {
		t.Fatalf("pending uploads count against the free space: %v", err)
	}
	restore()
	restore = hub.SetFreeBytesForTest(func(string) uint64 { return uint64(hub.DiskReserveBytes) - 1 })
	if _, err := svc.AddROM(ctx, bytes.NewReader([]byte("dummy-rom")), "full.nds", "", "", admin.ID); !errors.As(err, &he) || he.Code != hub.CodePayloadTooLarge {
		t.Fatalf("AddROM on a full disk: %v", err)
	}
	restore()
	restore = hub.SetFreeBytesForTest(func(string) uint64 { return 0 }) // unknown: never refuses
	if _, err := svc.AddROM(ctx, bytes.NewReader([]byte("dummy-rom")), "full.nds", "", "", admin.ID); err != nil {
		t.Fatalf("unknown free space: %v", err)
	}
	restore()
}

// HC-10: a failing delete transaction leaves the user as it was (enabled), not disabled.
func TestDeleteUserFailureLeavesUserEnabled(t *testing.T) {
	svc, _, db := hubtest.OpenIn(t, t.TempDir(), nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	u, err := svc.CreateUser(ctx, "max", "Max")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := db.Exec(`CREATE TRIGGER block_user_delete BEFORE DELETE ON users BEGIN SELECT RAISE(ABORT, 'blocked'); END`); err != nil {
		t.Fatal(err)
	}
	if err := svc.DeleteUser(ctx, u.ID, admin.ID); err == nil {
		t.Fatal("delete succeeded despite the trigger")
	}
	got, err := svc.GetUser(ctx, u.ID)
	if err != nil || got.Disabled() {
		t.Fatalf("user left disabled after a failed deletion: %+v %v", got, err)
	}
	// A user that was disabled before stays disabled.
	if err := svc.DisableUser(ctx, u.ID); err != nil {
		t.Fatal(err)
	}
	svc.DeleteUser(ctx, u.ID, admin.ID)
	if got, _ := svc.GetUser(ctx, u.ID); !got.Disabled() {
		t.Fatal("previously disabled user was re-enabled")
	}
	if _, err := db.Exec(`DROP TRIGGER block_user_delete`); err != nil {
		t.Fatal(err)
	}
	if err := svc.DeleteUser(ctx, u.ID, admin.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetUser(ctx, u.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
}

// HC-12: Close stops the grace timers, and none is armed afterwards.
func TestServiceCloseStopsGraceTimers(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	svc.ArmOwnerGraceForTest("s1", "d1")
	svc.ArmViewerGraceForTest("v1", "d1")
	if !svc.OwnerGraceArmedForTest("s1") {
		t.Fatal("timer not armed")
	}
	svc.Close()
	if svc.OwnerGraceArmedForTest("s1") {
		t.Fatal("timer survived Close")
	}
	svc.ArmOwnerGraceForTest("s2", "d1")
	if svc.OwnerGraceArmedForTest("s2") {
		t.Fatal("timer armed after Close")
	}
	svc.Close() // idempotent
}

// HC-15: one platform's index failing keeps the other platform's builds and the previous builds of the failed one.
func TestCatalogRefreshSurvivesOnePlatformFailure(t *testing.T) {
	svc, bb := bbEnv(t, nil)
	refresh(t, svc)
	bb.FailDirs = []string{"windows/x86_64"}
	rep, err := svc.RefreshCatalog(ctx)
	if err != nil || rep.Cores != 4 || len(rep.Warnings) != 1 || !strings.Contains(rep.Warnings[0], "windows-x64") {
		t.Fatalf("%+v %v", rep, err)
	}
	st, _ := svc.CoreSource(ctx)
	if st.LastSuccess == nil || !strings.Contains(st.LastError, "windows-x64") {
		t.Fatalf("%+v", st)
	}
	sc, err := svc.SystemCores(ctx, "nds")
	if err != nil || len(sc.Available) != 3 {
		t.Fatalf("%+v %v", sc, err)
	}
	if got := strings.Join(sc.Available[0].Platforms, ","); got != "windows-x64,linux-x64" {
		t.Fatalf("previous builds of the failed platform lost: %s", got)
	}
	// A fresh server without a previous catalog gets the platform that works.
	svc2, bb2 := bbEnv(t, nil)
	bb2.FailDirs = []string{"windows/x86_64"}
	rep, err = svc2.RefreshCatalog(ctx)
	if err != nil || rep.Cores != 4 {
		t.Fatalf("%+v %v", rep, err)
	}
	if sc, _ := svc2.SystemCores(ctx, "nds"); strings.Join(sc.Available[0].Platforms, ",") != "linux-x64" {
		t.Fatalf("%+v", sc.Available)
	}
	// Every platform failing is a failed refresh.
	bb2.FailDirs = []string{"windows/x86_64", "linux/x86_64"}
	if _, err := svc2.RefreshCatalog(ctx); err == nil {
		t.Fatal("refresh succeeded with every index down")
	}
}

// HA-3: a few sources cannot fill the whole queue of open pairing requests.
func TestOpenPairingRequestsPerSource(t *testing.T) {
	svc, clk, _ := newAdmin(t)
	for i := 0; i < hub.MaxOpenPairingPerSource; i++ {
		if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), fmt.Sprintf("2001:db8:7:7::%x", i+1))); err != nil {
			t.Fatal(err)
		}
		clk.Advance(15 * time.Second) // slower than the per-minute limit
	}
	clk.Advance(2 * time.Minute)
	if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), "2001:db8:7:7:aaaa::1")); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("same /64 beyond its open budget: %v", err)
	}
	if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), "2001:db8:7:8::1")); err != nil {
		t.Fatalf("another source is not affected: %v", err)
	}
}

// HA-3: failures of many sources do not lock out a source that has not failed itself.
func TestRedeemFailureCapSparesCleanSources(t *testing.T) {
	svc, _, admin := newAdmin(t)
	_, code, err := svc.CreateInvite(ctx, admin.ID, time.Hour, true)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < hub.MaxRedeemFailuresPerMinute/hub.MaxRedeemPerIPPerMinute+2; i++ { // exceeds the global cap
		for j := 0; j < hub.MaxRedeemPerIPPerMinute; j++ {
			in := redeemIn("UZ-AAAA-AAAA", "x")
			in.RemoteAddr = fmt.Sprintf("10.%d.%d.1", i/250, i%250)
			svc.RedeemInvite(ctx, in)
		}
	}
	in := redeemIn("UZ-AAAA-AAAA", "x") // a source with a failure of its own is held back by the global cap
	in.RemoteAddr = "10.0.0.1"
	if _, err := svc.RedeemInvite(ctx, in); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("failing source below the global cap: %v", err)
	}
	good := redeemIn(code, "Anna")
	good.RemoteAddr = "192.0.2.77"
	if _, err := svc.RedeemInvite(ctx, good); err != nil {
		t.Fatalf("clean source locked out by other sources' failures: %v", err)
	}
}

// HA-7: when pairing fails after the admin was created, the admin is rolled back and setup works again.
func TestLocalSetupRollsBackAdminWhenPairingFails(t *testing.T) {
	svc, clk := hubtest.New(t, nil)
	for i := 0; i < hub.MaxOpenPairingPerSource; i++ { // fill the open budget of the caller's source
		if _, err := svc.CreatePairingRequest(ctx, pairIn(uuid.NewString(), "127.0.0.1")); err != nil {
			t.Fatal(err)
		}
	}
	in := hub.LocalInput{Username: "fabio", Password: "secret-12345", Device: pairIn(uuid.NewString(), "127.0.0.1")}
	if _, _, err := svc.LocalSetup(ctx, in); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("setup with a full pairing budget: %v", err)
	}
	if has, err := svc.HasAdmin(ctx); err != nil || has {
		t.Fatalf("admin left behind: %v %v", has, err)
	}
	for _, r := range pendingRequests(t, svc) {
		if err := svc.DenyPairing(ctx, r); err != nil {
			t.Fatal(err)
		}
	}
	clk.Advance(2 * time.Minute) // past the per-minute limit
	if _, _, err := svc.LocalSetup(ctx, in); err != nil {
		t.Fatalf("setup after the failed attempt: %v", err)
	}
}

func pendingRequests(t *testing.T, svc *hub.Service) []string {
	t.Helper()
	rs, err := svc.ListPendingRequests(ctx)
	if err != nil {
		t.Fatal(err)
	}
	var ids []string
	for _, r := range rs {
		ids = append(ids, r.ID)
	}
	return ids
}

// HC-2: deleting a game and adding the same ROM again links the saves to the new game (the id is re-used).
func TestReaddedROMGetsItsSavesBack(t *testing.T) {
	svc, _, admin := newAdmin(t)
	dev := pairDevice(t, svc, admin.ID, "Desktop")
	rom := randomROM(3000) // no readable identity: only the file hash can bring the game back
	g, err := svc.AddROM(ctx, bytes.NewReader(rom), "demo.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	save := []byte("progress")
	if _, err := svc.PutSave(ctx, hub.PutSaveInput{UserID: admin.ID, DeviceID: dev, GameID: g.ID, Slot: "default", BaseRevision: 0,
		SHA256: hexSHA(save), Reason: hub.SyncCheckpoint, Body: bytes.NewReader(save)}); err != nil {
		t.Fatal(err)
	}
	if err := svc.DeleteGame(ctx, g.ID); err != nil {
		t.Fatal(err)
	}
	rows, err := svc.ListSaveSlots(ctx, "")
	if err != nil || len(rows) != 1 || rows[0].GameTitle != "(removed game)" {
		t.Fatalf("saves of a removed game stay listed: %+v %v", rows, err)
	}
	// A different ROM does not take them.
	if other, err := svc.AddROM(ctx, bytes.NewReader(randomROM(3000)), "other.nds", "", "", admin.ID); err != nil || other.ID == g.ID {
		t.Fatalf("unrelated ROM took the saves: %+v %v", other, err)
	}
	back, err := svc.AddROM(ctx, bytes.NewReader(rom), "demo.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	if back.ID != g.ID {
		t.Fatalf("game id not re-used: old %s new %s", g.ID, back.ID)
	}
	rows, err = svc.ListSaveSlots(ctx, "")
	if err != nil || len(rows) != 1 || rows[0].GameTitle == "(removed game)" || rows[0].GameID != back.ID {
		t.Fatalf("saves not linked to the new game: %+v %v", rows, err)
	}
	// A device still holding the old library entry can upload again.
	next := []byte("progress-2")
	if _, err := svc.PutSave(ctx, hub.PutSaveInput{UserID: admin.ID, DeviceID: dev, GameID: g.ID, Slot: "default", BaseRevision: 1,
		SHA256: hexSHA(next), Reason: hub.SyncCheckpoint, Body: bytes.NewReader(next)}); err != nil {
		t.Fatalf("save upload for the re-linked game: %v", err)
	}
}

// HC-2: with an identity the game comes back for another file of the same game (same code, kind and version).
func TestReaddedROMByIdentityGetsItsSavesBack(t *testing.T) {
	svc, _, admin := newAdmin(t)
	dev := pairDevice(t, svc, admin.ID, "Desktop")
	g, err := svc.AddROM(ctx, bytes.NewReader(append(ndsROM("ABCD"), 1)), "a.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	save := []byte("save-data")
	if _, err := svc.PutSave(ctx, hub.PutSaveInput{UserID: admin.ID, DeviceID: dev, GameID: g.ID, Slot: "default", BaseRevision: 0,
		SHA256: hexSHA(save), Reason: hub.SyncCheckpoint, Body: bytes.NewReader(save)}); err != nil {
		t.Fatal(err)
	}
	other, err := svc.AddROM(ctx, bytes.NewReader(append(ndsROM("WXYZ"), 2)), "other.nds", "", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	if err := svc.DeleteGame(ctx, g.ID); err != nil {
		t.Fatal(err)
	}
	// A different ROM does not take the saves.
	if again, err := svc.AddROM(ctx, bytes.NewReader(append(ndsROM("EFGH"), 3)), "e.nds", "", "", admin.ID); err != nil || again.ID == g.ID {
		t.Fatalf("unrelated ROM took the saves: %+v %v", again, err)
	}
	// Another dump (different bytes, same game code) is the same game.
	back, err := svc.AddROM(ctx, bytes.NewReader(append(ndsROM("ABCD"), 9, 9)), "a-redump.nds", "", "", admin.ID)
	if err != nil || back.ID != g.ID {
		t.Fatalf("same identity did not bring the game back: %+v %v", back, err)
	}
	if _, err := svc.GetSaveSlot(ctx, admin.ID, back.ID, "default"); err != nil {
		t.Fatalf("slot not reachable through the new game: %v", err)
	}
	_ = other
}
