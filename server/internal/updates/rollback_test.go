package updates

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/uzume/server/internal/corepkg"
)

func rr(channel, version string, mod func(*Release)) Release {
	r := rel("hub", channel, version)
	r.Artifacts = []Artifact{art("linux-amd64", KindDeb, "framebeam-hub_"+version+"_amd64.deb")}
	if mod != nil {
		mod(&r)
	}
	return r
}

func versionsOf(cs []Candidate) []string {
	var out []string
	for _, c := range cs {
		out = append(out, c.Release.Version)
	}
	return out
}

func eq(a, b []string) bool { return strings.Join(a, ",") == strings.Join(b, ",") }

func TestRollbackCandidates(t *testing.T) {
	idx := Index{Schema: 1, Releases: []Release{
		rr("stable", "0.10.0", nil), rr("stable", "0.11.0", nil), rr("stable", "0.12.0", nil), rr("stable", "0.13.0", nil),
		rr("stable", "0.14.0", nil), rr("stable", "0.15.0", nil), rr("stable", "0.16.0", nil), rr("stable", "0.17.0", nil),
		rr("beta", "0.16.1", nil), rr("beta", "0.17.1", nil),
	}}
	q := RollbackQuery{Product: ProductHub, Platform: "linux-amd64", Kind: KindDeb, Current: "0.17.0", Channel: ChannelStable, Protocol: 1}

	t.Run("stable install: stable only, older only, at most five, newest first", func(t *testing.T) {
		got := versionsOf(RollbackCandidates(idx, q))
		if !eq(got, []string{"0.16.0", "0.15.0", "0.14.0", "0.13.0", "0.12.0"}) {
			t.Fatal(got)
		}
	})
	t.Run("beta install also sees beta releases", func(t *testing.T) {
		q := q
		q.Channel, q.Current, q.Limit = ChannelBeta, "0.17.1", 3
		if got := versionsOf(RollbackCandidates(idx, q)); !eq(got, []string{"0.17.0", "0.16.1", "0.16.0"}) {
			t.Fatal(got)
		}
	})
	t.Run("history adds a version of another channel and carries the dates", func(t *testing.T) {
		q := q
		q.History = []HistoryEntry{{Version: "0.16.1", First: t0, Last: t0.Add(48 * time.Hour)}}
		cs := RollbackCandidates(idx, q)
		if got := versionsOf(cs); !eq(got, []string{"0.16.1", "0.16.0", "0.15.0", "0.14.0", "0.13.0"}) {
			t.Fatal(got)
		}
		if !cs[0].Ran || !cs[0].First.Equal(t0) || cs[1].Ran {
			t.Fatalf("%+v", cs[:2])
		}
	})
	t.Run("protocol floor", func(t *testing.T) {
		i2 := Index{Schema: 1, Releases: []Release{
			rr("stable", "0.1.0", func(r *Release) { r.ProtocolVersion, r.MinProtocolVersion = 1, 1 }),
			rr("stable", "0.2.0", func(r *Release) { r.ProtocolVersion, r.MinProtocolVersion = 2, 1 }),
			rr("stable", "0.3.0", func(r *Release) { r.ProtocolVersion, r.MinProtocolVersion = 2, 2 }),
			rr("stable", "0.4.0", func(r *Release) { r.ProtocolVersion, r.MinProtocolVersion = 2, 2 }),
		}}
		q := RollbackQuery{Product: ProductHub, Platform: "linux-amd64", Kind: KindDeb, Current: "0.4.0", Channel: ChannelStable, Protocol: 2}
		if got := versionsOf(RollbackCandidates(i2, q)); !eq(got, []string{"0.3.0", "0.2.0"}) { // 0.1.0 does not speak protocol 2
			t.Fatal(got)
		}
		q.Protocol = 0
		if got := versionsOf(RollbackCandidates(i2, q)); len(got) != 3 {
			t.Fatalf("no filter without a protocol: %v", got)
		}
	})
	t.Run("an MSI must allow a downgrade", func(t *testing.T) {
		msi := func(v string, ok bool) Release {
			r := rr("stable", v, func(r *Release) { r.MSIDowngrade = ok })
			r.Artifacts = []Artifact{art("windows-amd64", KindMSI, "framebeam-"+v+".msi")}
			return r
		}
		i2 := Index{Schema: 1, Releases: []Release{msi("0.1.0", false), msi("0.2.0", true), msi("0.3.0", true)}}
		q := RollbackQuery{Product: ProductHub, Platform: "windows-amd64", Kind: KindMSI, Current: "0.3.0", Channel: ChannelStable}
		if got := versionsOf(RollbackCandidates(i2, q)); !eq(got, []string{"0.2.0"}) {
			t.Fatal(got)
		}
	})
	t.Run("other product, platform, legacy companion and prerelease order", func(t *testing.T) {
		i2 := Index{Schema: 1, Releases: []Release{
			rr("stable", "0.5.0", nil), rr("stable", "0.5.0-legacy", nil),
			func() Release { r := rr("stable", "0.4.0", nil); r.Product = "player"; return r }(),
			func() Release {
				r := rr("stable", "0.3.0", nil)
				r.Artifacts[0].Platform = "linux-arm64"
				return r
			}(),
			rr("stable", "0.6.0", nil),
		}}
		q := RollbackQuery{Product: ProductHub, Platform: "linux-amd64", Kind: KindDeb, Current: "0.6.0", Channel: ChannelStable}
		if got := versionsOf(RollbackCandidates(i2, q)); !eq(got, []string{"0.5.0"}) {
			t.Fatal(got)
		}
	})
	t.Run("same version in two channels appears once, stable", func(t *testing.T) {
		i2 := Index{Schema: 1, Releases: []Release{rr("beta", "0.5.0", nil), rr("stable", "0.5.0", nil), rr("beta", "0.6.0", nil)}}
		q := RollbackQuery{Product: ProductHub, Platform: "linux-amd64", Kind: KindDeb, Current: "0.6.0", Channel: ChannelBeta}
		cs := RollbackCandidates(i2, q)
		if len(cs) != 1 || cs[0].Release.Channel != ChannelStable {
			t.Fatalf("%+v", cs)
		}
	})
	t.Run("running version not SemVer", func(t *testing.T) {
		q := q
		q.Current = "dev"
		if RollbackCandidates(idx, q) != nil {
			t.Fatal("dev builds have no candidates")
		}
	})
}

func TestPlanSchema(t *testing.T) {
	b := func(name string) DBBackup {
		d, ok := ParseDBBackupName(name)
		if !ok {
			t.Fatal(name)
		}
		return d
	}
	bks := []DBBackup{b("hub-17-to-18-20261020T090000Z.db"), b("hub-15-to-17-20261001T090000Z.db")}

	if p := PlanSchema(18, 18, bks); p.Action != SchemaKeep {
		t.Fatalf("same schema keeps data: %+v", p)
	}
	if p := PlanSchema(19, 18, nil); p.Action != SchemaKeep {
		t.Fatalf("target knows more: %+v", p)
	}
	// Target understands 17: the newest backup whose source schema it knows (17-to-18) is the one.
	if p := PlanSchema(17, 18, bks); p.Action != SchemaRestore || p.Backup.Name != "hub-17-to-18-20261020T090000Z.db" {
		t.Fatalf("%+v", p)
	}
	// Target understands only 16: 17-to-18 starts at 17 (too new), 15-to-17 starts at 15.
	if p := PlanSchema(16, 18, bks); p.Action != SchemaRestore || p.Backup.Name != "hub-15-to-17-20261001T090000Z.db" {
		t.Fatalf("%+v", p)
	}
	// Target understands only 14: no backup starts that low, so going back is refused.
	if p := PlanSchema(14, 18, bks); p.Action != SchemaRefuse || !strings.Contains(p.Reason, "none exists") {
		t.Fatalf("%+v", p)
	}
	if p := PlanSchema(16, 18, nil); p.Action != SchemaRefuse {
		t.Fatalf("no backup at all must refuse: %+v", p)
	}
	// Unknown schema is never guessed.
	if p := PlanSchema(0, 18, bks); p.Action != SchemaRefuse || !strings.Contains(p.Reason, "does not say") {
		t.Fatalf("%+v", p)
	}
	// The history knows the schema of a version the index does not.
	c := Candidate{Release: Release{Version: "0.16.3"}}
	if SchemaKnown(c, nil) != 0 || SchemaKnown(c, []HistoryEntry{{Version: "0.16.3", Schema: 17}}) != 17 {
		t.Fatal("history schema")
	}
	c.Release.SchemaVersion = 16
	if SchemaKnown(c, []HistoryEntry{{Version: "0.16.3", Schema: 17}}) != 16 {
		t.Fatal("the index wins")
	}
}

func TestListDBBackups(t *testing.T) {
	dir := t.TempDir()
	os.MkdirAll(filepath.Join(dir, DBBackupDir), 0o750)
	for _, n := range []string{"hub-1-to-2-20260101T000000Z.db", "hub-2-to-3-20260201T000000Z.db", "notes.txt", "hub-x-to-y-20260101T000000Z.db"} {
		os.WriteFile(filepath.Join(dir, DBBackupDir, n), []byte("x"), 0o640)
	}
	got := ListDBBackups(dir)
	if len(got) != 2 || got[0].From != 2 || got[0].To != 3 || got[1].Name != "hub-1-to-2-20260101T000000Z.db" {
		t.Fatalf("%+v", got)
	}
	if ListDBBackups(t.TempDir()) != nil {
		t.Fatal("no folder, no backups")
	}
}

func TestHistoryAndPause(t *testing.T) {
	var h []HistoryEntry
	h, changed := RecordVersion(h, "0.16.3", 17, t0)
	if !changed || len(h) != 1 || h[0].First != t0 || h[0].Schema != 17 {
		t.Fatalf("%+v", h)
	}
	if h2, changed := RecordVersion(h, "0.16.3", 17, t0.Add(10*time.Minute)); changed || !h2[0].Last.Equal(t0) {
		t.Fatal("the last date is refreshed at most hourly")
	}
	h, changed = RecordVersion(h, "0.16.3", 17, t0.Add(2*time.Hour))
	if !changed || !h[0].Last.Equal(t0.Add(2*time.Hour)) || !h[0].First.Equal(t0) {
		t.Fatalf("%+v", h)
	}
	h, _ = RecordVersion(h, "0.16.4", 18, t0.Add(48*time.Hour))
	if len(h) != 2 {
		t.Fatal(h)
	}
	if _, changed := RecordVersion(h, "dev", 18, t0); changed {
		t.Fatal("non-release versions are not recorded")
	}
	for i := 0; i < MaxHistory+5; i++ {
		h, _ = RecordVersion(h, "2."+itoa(i)+".0", 0, t0.Add(time.Duration(i)*time.Hour))
	}
	if len(h) != MaxHistory {
		t.Fatalf("history is capped: %d", len(h))
	}
	if got := ParseHistory(MarshalHistory(h)); len(got) != len(h) {
		t.Fatal("round trip")
	}
	if ParseHistory("{broken") != nil || ParsePause("") != nil || ParsePause("{") != nil || ParsePause(`{"to":"x"}`) != nil {
		t.Fatal("damaged values are empty")
	}
	p := Pause{From: "0.16.4", To: "0.16.3", At: t0, By: "max", PrevAuto: "1"}
	if got := ParsePause(MarshalPause(p)); got == nil || got.From != "0.16.4" || got.PrevAuto != "1" || got.By != "max" {
		t.Fatalf("%+v", got)
	}
}

func signBytes(t *testing.T, data []byte) []byte {
	t.Helper()
	sig, err := corepkg.Sign(data, testSeed)
	if err != nil {
		t.Fatal(err)
	}
	return sig
}

func writeStagedJSON(t *testing.T, dir string, st Staged) {
	t.Helper()
	b, _ := json.Marshal(st)
	if err := os.WriteFile(filepath.Join(StagedDir(dir), StagedFileName), b, 0o640); err != nil {
		t.Fatal(err)
	}
}

func itoa(i int) string {
	if i == 0 {
		return "0"
	}
	s := ""
	for ; i > 0; i /= 10 {
		s = string(rune('0'+i%10)) + s
	}
	return s
}

// ---- the root helper ----

func (f *fixture) stageRollback(t *testing.T, restoreDB string, settings map[string]string) Staged {
	t.Helper()
	ctx := context.Background()
	fe, err := f.src.LoadIndex(ctx, f.keys)
	if err != nil {
		t.Fatal(err)
	}
	r := fe.Index.Releases[0]
	st, err := StageRollback(ctx, fe, r, r.Artifacts[0], f.dir, restoreDB, settings)
	if err != nil {
		t.Fatal(err)
	}
	return st
}

func TestStageRollbackRejectsBadBackupName(t *testing.T) {
	f := newFixture(t, "0.3.0")
	fe, _ := f.src.LoadIndex(context.Background(), f.keys)
	r := fe.Index.Releases[0]
	for _, bad := range []string{"../framebeam.db", "hub-1-to-2.db", "framebeam.db", "hub-1-to-2-20260101T000000Z.db/../x"} {
		if _, err := StageRollback(context.Background(), fe, r, r.Artifacts[0], f.dir, bad, nil); err == nil {
			t.Fatalf("%q must be rejected", bad)
		}
	}
}

func TestApplyStagedRollbackKeepsData(t *testing.T) {
	f := newFixture(t, "0.3.0") // the signed index holds 0.3.0; the running server is newer
	st := f.stageRollback(t, "", nil)
	if !st.Rollback {
		t.Fatal("staged.json must say rollback")
	}
	fr := &fakeRunner{}
	res, err := f.apply(t, fr.run, "0.4.0", func(o *ApplyOptions) { o.Protocol = 1 })
	if err != nil || !res.OK {
		t.Fatal(res, err)
	}
	if len(fr.calls) != 1 || fr.calls[0][0] != "dpkg" || !fr.copyOK || string(fr.content) != string(f.debData) {
		t.Fatalf("only the installer runs when the data is kept: %v", fr.calls)
	}
}

func TestApplyStagedRollbackRejects(t *testing.T) {
	t.Run("a staged rollback that is not older", func(t *testing.T) {
		f := newFixture(t, "0.3.0")
		f.stageRollback(t, "", nil)
		fr := &fakeRunner{}
		for _, cur := range []string{"0.3.0", "0.2.0"} {
			if _, err := f.apply(t, fr.run, cur, nil); err == nil || !strings.Contains(err.Error(), "not older") || len(fr.calls) != 0 {
				t.Fatalf("cur %s: %v %v", cur, err, fr.calls)
			}
		}
	})
	t.Run("an expired index is a replay", func(t *testing.T) {
		f := newFixture(t, "0.3.0")
		f.stageRollback(t, "", nil)
		// Re-sign the staged index with a past expiry.
		idxData, _ := os.ReadFile(filepath.Join(StagedDir(f.dir), IndexFileName))
		idx, _ := ParseIndexOpts(idxData, true)
		idx.ExpiresAt = t0.Add(-time.Hour)
		data, _ := Marshal(idx)
		sig := signBytes(t, data)
		os.WriteFile(filepath.Join(StagedDir(f.dir), IndexFileName), data, 0o640)
		os.WriteFile(filepath.Join(StagedDir(f.dir), SigFileName), sig, 0o640)
		fr := &fakeRunner{}
		if _, err := f.apply(t, fr.run, "0.4.0", nil); err == nil || !strings.Contains(err.Error(), "expired") || len(fr.calls) != 0 {
			t.Fatalf("%v %v", err, fr.calls)
		}
	})
	t.Run("the target must speak the running protocol", func(t *testing.T) {
		f := newFixture(t, "0.3.0")
		f.stageRollback(t, "", nil)
		fr := &fakeRunner{}
		if _, err := f.apply(t, fr.run, "0.4.0", func(o *ApplyOptions) { o.Protocol = 2 }); err == nil || !strings.Contains(err.Error(), "protocol") || len(fr.calls) != 0 {
			t.Fatalf("%v %v", err, fr.calls)
		}
	})
	t.Run("a tampered package", func(t *testing.T) {
		f := newFixture(t, "0.3.0")
		st := f.stageRollback(t, "", nil)
		os.WriteFile(filepath.Join(StagedDir(f.dir), st.Artifact), []byte(strings.Repeat("x", len(f.debData))), 0o640)
		fr := &fakeRunner{}
		if _, err := f.apply(t, fr.run, "0.4.0", nil); err == nil || len(fr.calls) != 0 {
			t.Fatal(err)
		}
	})
	t.Run("an update must not carry a database restore", func(t *testing.T) {
		f := newFixture(t, "0.5.0")
		st := f.stage(t, "0.4.0")
		st.RestoreDB = "hub-1-to-2-20260101T000000Z.db"
		writeStagedJSON(t, f.dir, st)
		fr := &fakeRunner{}
		if _, err := f.apply(t, fr.run, "0.4.0", nil); err == nil || len(fr.calls) != 0 {
			t.Fatal(err)
		}
	})
	t.Run("an MSI that cannot be installed over a newer version", func(t *testing.T) {
		f := newFixtureFor(t, "0.3.0", "windows-amd64", KindMSI, ".msi")
		f.stageRollbackPlatform(t)
		fr := &fakeRunner{}
		_, err := f.apply(t, fr.run, "0.4.0", func(o *ApplyOptions) { o.Platform = "windows-amd64" })
		if err == nil || !strings.Contains(err.Error(), "over a newer") || len(fr.calls) != 0 {
			t.Fatalf("%v %v", err, fr.calls)
		}
	})
}

func (f *fixture) stageRollbackPlatform(t *testing.T) {
	t.Helper()
	ctx := context.Background()
	fe, err := f.src.LoadIndex(ctx, f.keys)
	if err != nil {
		t.Fatal(err)
	}
	r := fe.Index.Releases[0]
	if _, err := StageRollback(ctx, fe, r, r.Artifacts[0], f.dir, "", nil); err != nil {
		t.Fatal(err)
	}
}

// dbFixture prepares a data directory with a live database, a pre-update backup and a staged rollback to restore it.
func dbFixture(t *testing.T) (*fixture, string) {
	t.Helper()
	f := newFixture(t, "0.3.0")
	hdr := "SQLite format 3\x00"
	os.WriteFile(filepath.Join(f.dir, "framebeam.db"), []byte(hdr+"LIVE"), 0o640)
	os.WriteFile(filepath.Join(f.dir, "framebeam.db-wal"), []byte("wal"), 0o640)
	os.MkdirAll(filepath.Join(f.dir, DBBackupDir), 0o750)
	name := "hub-17-to-18-20261020T090000Z.db"
	os.WriteFile(filepath.Join(f.dir, DBBackupDir, name), []byte(hdr+"OLD"), 0o640)
	f.stageRollback(t, name, map[string]string{"update_auto": "0"})
	return f, name
}

func read(t *testing.T, p string) string {
	t.Helper()
	b, err := os.ReadFile(p)
	if err != nil {
		return "<missing>"
	}
	return string(b)
}

func TestApplyStagedRollbackRestoresDatabase(t *testing.T) {
	f, _ := dbFixture(t)
	var order []string
	var prepared map[string]string
	var preparedContent string
	run := func(ctx context.Context, name string, args ...string) ([]byte, error) {
		order = append(order, name+" "+args[0])
		if name == "dpkg" { // at install time the old database is in place and the service is stopped
			if got := read(t, filepath.Join(f.dir, "framebeam.db")); !strings.HasSuffix(got, "OLD") {
				t.Errorf("database at install time: %q", got)
			}
		}
		return nil, nil
	}
	res, err := f.apply(t, run, "0.4.0", func(o *ApplyOptions) {
		o.PrepareDB = func(p string, kv map[string]string) error {
			prepared, preparedContent = kv, read(t, p)
			return nil
		}
	})
	if err != nil || !res.OK {
		t.Fatal(res, err)
	}
	if strings.Join(order, "|") != "systemctl stop|dpkg -i" {
		t.Fatalf("order: %v", order)
	}
	if prepared["update_auto"] != "0" || !strings.HasSuffix(preparedContent, "OLD") {
		t.Fatalf("PrepareDB got %v / %q", prepared, preparedContent)
	}
	// The replaced database is kept aside, the restoring file is gone.
	entries, _ := os.ReadDir(f.dir)
	var aside []string
	for _, e := range entries {
		if strings.Contains(e.Name(), ".rollback-") {
			aside = append(aside, e.Name())
		}
		if e.Name() == dbRestoringName {
			t.Fatal("restoring file left behind")
		}
	}
	if len(aside) != 2 { // framebeam.db and its -wal
		t.Fatalf("aside: %v", aside)
	}
	if got := read(t, filepath.Join(f.dir, "framebeam.db.rollback-20261007T100000Z")); !strings.HasSuffix(got, "LIVE") {
		t.Fatalf("the old database must be kept: %q", got)
	}
}

func TestApplyStagedRollbackFailedInstallPutsTheDatabaseBack(t *testing.T) {
	f, _ := dbFixture(t)
	var order []string
	run := func(ctx context.Context, name string, args ...string) ([]byte, error) {
		order = append(order, name+" "+args[0])
		if name == "dpkg" {
			return []byte("dpkg: error"), errors.New("exit status 1")
		}
		return nil, nil
	}
	_, err := f.apply(t, run, "0.4.0", func(o *ApplyOptions) { o.PrepareDB = func(string, map[string]string) error { return nil } })
	if err == nil {
		t.Fatal("must fail")
	}
	if strings.Join(order, "|") != "systemctl stop|dpkg -i|systemctl start" {
		t.Fatalf("the service must be started again: %v", order)
	}
	if got := read(t, filepath.Join(f.dir, "framebeam.db")); !strings.HasSuffix(got, "LIVE") {
		t.Fatalf("the original database must be back: %q", got)
	}
	if got := read(t, filepath.Join(f.dir, "framebeam.db-wal")); got != "wal" {
		t.Fatalf("the original wal must be back: %q", got)
	}
}

func TestApplyStagedRollbackStopFailureChangesNothing(t *testing.T) {
	f, _ := dbFixture(t)
	run := func(ctx context.Context, name string, args ...string) ([]byte, error) {
		if name == "systemctl" {
			return []byte("cannot stop"), errors.New("exit status 5")
		}
		t.Error("the installer must not run")
		return nil, nil
	}
	_, err := f.apply(t, run, "0.4.0", func(o *ApplyOptions) { o.PrepareDB = func(string, map[string]string) error { return nil } })
	if err == nil || !strings.Contains(err.Error(), "stop the server") {
		t.Fatal(err)
	}
	if got := read(t, filepath.Join(f.dir, "framebeam.db")); !strings.HasSuffix(got, "LIVE") {
		t.Fatalf("%q", got)
	}
	if _, err := os.Stat(filepath.Join(f.dir, dbRestoringName)); err == nil {
		t.Fatal("restoring file left behind")
	}
}

func TestApplyStagedRollbackDatabaseBackupChecks(t *testing.T) {
	t.Run("not an SQLite file", func(t *testing.T) {
		f, name := dbFixture(t)
		os.WriteFile(filepath.Join(f.dir, DBBackupDir, name), []byte("not a database at all"), 0o640)
		run := func(context.Context, string, ...string) ([]byte, error) { t.Error("nothing may run"); return nil, nil }
		if _, err := f.apply(t, run, "0.4.0", func(o *ApplyOptions) { o.PrepareDB = func(string, map[string]string) error { return nil } }); err == nil || !strings.Contains(err.Error(), "SQLite") {
			t.Fatal(err)
		}
	})
	t.Run("a symlink is not followed", func(t *testing.T) {
		f, name := dbFixture(t)
		p := filepath.Join(f.dir, DBBackupDir, name)
		os.Remove(p)
		if err := os.Symlink("/etc/hostname", p); err != nil {
			t.Skip(err)
		}
		run := func(context.Context, string, ...string) ([]byte, error) { t.Error("nothing may run"); return nil, nil }
		if _, err := f.apply(t, run, "0.4.0", func(o *ApplyOptions) { o.PrepareDB = func(string, map[string]string) error { return nil } }); err == nil {
			t.Fatal("symlinked backup must be refused")
		}
	})
	t.Run("PrepareDB is required", func(t *testing.T) {
		f, _ := dbFixture(t)
		run := func(context.Context, string, ...string) ([]byte, error) { t.Error("nothing may run"); return nil, nil }
		if _, err := f.apply(t, run, "0.4.0", nil); err == nil {
			t.Fatal("must be refused")
		}
	})
}

func TestServiceCommand(t *testing.T) {
	if n, a := ServiceCommand("linux-arm64", "stop"); n != "systemctl" || strings.Join(a, " ") != "stop framebeam-hub.service" {
		t.Fatal(n, a)
	}
	if n, a := ServiceCommand("windows-amd64", "start"); n != "net.exe" || strings.Join(a, " ") != "start FrameBeamHub" {
		t.Fatal(n, a)
	}
}
