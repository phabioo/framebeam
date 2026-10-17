package hub_test

import (
	"context"
	"encoding/json"
	"errors"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
	"github.com/phabioo/framebeam/server/internal/pluginpkg"
)

type onePlugin struct{ id, version string }

func (p onePlugin) OfferedPlugin(_ context.Context, id, version string) (string, bool, error) {
	return strings.Repeat("c", 64), id == p.id && version == p.version, nil
}

func runFixture(t *testing.T) (*hub.Service, *hubtest.Clock, hub.User, hub.Game) {
	t.Helper()
	svc, clk := hubtest.New(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := svc.AddROM(ctx, strings.NewReader("homebrew-rom-dummy"), "demo.nds", "Demo", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	svc.SetPluginCatalog(onePlugin{"net.example.board", "1.0.0"})
	return svc, clk, admin, g
}

func newRun(t *testing.T, svc *hub.Service, admin hub.User, g hub.Game) hub.Run {
	t.Helper()
	run, err := svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "Run", GameID: g.ID,
		Plugins: []hub.RunPluginRef{{PluginID: "net.example.board", Version: "1.0.0"}}})
	if err != nil {
		t.Fatal(err)
	}
	return run
}

func TestRunWithoutCatalogOffersNothing(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	admin, _ := svc.CreateAdmin(ctx, "admin", "secret-12345")
	g, _ := svc.AddROM(ctx, strings.NewReader("homebrew-rom-dummy"), "demo.nds", "Demo", "", admin.ID)
	_, err := svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "Run", GameID: g.ID,
		Plugins: []hub.RunPluginRef{{PluginID: "net.example.board", Version: "1.0.0"}}})
	if !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("%v", err)
	}
}

func TestRunEventLimit(t *testing.T) {
	svc, _, admin, g := runFixture(t)
	run := newRun(t, svc, admin, g)
	in := hub.AppendRunEventInput{UserID: admin.ID, DeviceID: uuid.NewString(), RunID: run.ID, PluginID: "net.example.board", Source: "manual", Type: "x"}
	if err := hub.SetRunLastSeqForTest(svc, run.ID, hub.MaxRunEvents-1); err != nil {
		t.Fatal(err)
	}
	in.ClientID = uuid.NewString()
	seq, created, err := svc.AppendRunEvent(ctx, in)
	if err != nil || !created || seq != hub.MaxRunEvents {
		t.Fatalf("%d %v %v", seq, created, err)
	}
	in.ClientID = uuid.NewString()
	if _, _, err := svc.AppendRunEvent(ctx, in); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("over the limit: %v", err)
	}
	// Hub events still get recorded at the limit (a run can always be ended).
	if _, err := svc.SetRunStatus(ctx, admin.ID, run.ID, "active"); err != nil {
		t.Fatal(err)
	}
	if got, _ := svc.GetRun(ctx, admin.ID, run.ID); got.LastSeq != hub.MaxRunEvents+1 {
		t.Fatalf("last_seq %d", got.LastSeq)
	}
}

func TestRunEventTokenBucketRefills(t *testing.T) {
	svc, clk, admin, g := runFixture(t)
	run := newRun(t, svc, admin, g)
	add := func() error {
		_, _, err := svc.AppendRunEvent(ctx, hub.AppendRunEventInput{UserID: admin.ID, DeviceID: uuid.NewString(), RunID: run.ID,
			ClientID: uuid.NewString(), PluginID: "net.example.board", Source: "automatic", Type: "tick", Payload: json.RawMessage(`{}`)})
		return err
	}
	for i := 0; i < 30; i++ {
		if err := add(); err != nil {
			t.Fatalf("event %d: %v", i, err)
		}
	}
	if err := add(); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("31st event: %v", err)
	}
	clk.Advance(500 * time.Millisecond) // 1 token
	if err := add(); err != nil {
		t.Fatal(err)
	}
	if err := add(); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("%v", err)
	}
	clk.Advance(time.Hour) // refill caps at the burst
	for i := 0; i < 30; i++ {
		if err := add(); err != nil {
			t.Fatalf("event %d after refill: %v", i, err)
		}
	}
	if err := add(); !errors.Is(err, hub.ErrRateLimited) {
		t.Fatalf("%v", err)
	}
}

func TestRunDeleteAndArchive(t *testing.T) {
	svc, _, admin, g := runFixture(t)
	run := newRun(t, svc, admin, g)
	if err := svc.ArchiveRun(ctx, run.ID, true); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("archiving a running run: %v", err)
	}
	if _, err := svc.SetRunStatus(ctx, admin.ID, run.ID, "abandoned"); err != nil {
		t.Fatal(err)
	}
	if err := svc.ArchiveRun(ctx, run.ID, true); err != nil {
		t.Fatal(err)
	}
	if l, _ := svc.ListRuns(ctx, admin.ID); len(l) != 0 {
		t.Fatalf("archived run listed: %+v", l)
	}
	if l, _ := svc.ListRunsAdmin(ctx, false); len(l) != 0 {
		t.Fatalf("%+v", l)
	}
	if l, _ := svc.ListRunsAdmin(ctx, true); len(l) != 1 {
		t.Fatalf("%+v", l)
	}
	if err := svc.ArchiveRun(ctx, run.ID, false); err != nil {
		t.Fatal(err)
	}
	if err := svc.DeleteRun(ctx, run.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.GetRun(ctx, admin.ID, run.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if err := svc.DeleteRun(ctx, run.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
	if _, _, err := svc.ListRunEventsAdmin(ctx, run.ID, 0, 10); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("%v", err)
	}
}

func TestRunDeletedUserDisappearsFromRuns(t *testing.T) {
	svc, _, admin, g := runFixture(t)
	anna, _ := svc.CreateUser(ctx, "anna", "Anna")
	run, err := svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "Run", GameID: g.ID, InviteUserIDs: []string{anna.ID},
		Plugins: []hub.RunPluginRef{{PluginID: "net.example.board", Version: "1.0.0"}}})
	if err != nil {
		t.Fatal(err)
	}
	if err := svc.DeleteUser(ctx, anna.ID, admin.ID); err != nil {
		t.Fatal(err)
	}
	got, err := svc.GetRun(ctx, admin.ID, run.ID)
	if err != nil || len(got.Participants) != 1 {
		t.Fatalf("%v %+v", err, got.Participants)
	}
}

// The plugin store is the run plugin catalog, and a version used by a run that is still going cannot be removed.
func TestPluginStoreWiredToRuns(t *testing.T) {
	svc := pluginHub(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := svc.AddROM(ctx, strings.NewReader("homebrew-rom-dummy"), "demo.nds", "Demo", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	data := hubtest.PluginZip(t, "net.example.wired", "1.0.0", nil, nil)
	if _, err := svc.OfferStagedPlugin(ctx, stage(t, svc, data).StageID, nil, admin.ID); err != nil {
		t.Fatal(err)
	}
	in := func(id, v string) hub.CreateRunInput {
		return hub.CreateRunInput{UserID: admin.ID, Name: "Wired", GameID: g.ID, Plugins: []hub.RunPluginRef{{PluginID: id, Version: v}}}
	}
	// Only offered versions can be used; the run records the SHA-256 of the offered file.
	if _, err := svc.CreateRun(ctx, in("net.example.wired", "9.9.9")); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("not offered: %v", err)
	}
	run, err := svc.CreateRun(ctx, in("net.example.wired", "1.0.0"))
	if err != nil {
		t.Fatal(err)
	}
	if len(run.Plugins) != 1 || run.Plugins[0].SHA256 != pluginpkg.SHA256Hex(data) {
		t.Fatalf("%+v", run.Plugins)
	}
	// Removal is refused while the run is not finished (also while paused), allowed afterwards.
	if err := svc.RemovePlugin(ctx, "net.example.wired", "1.0.0"); !errors.Is(err, hub.ErrConflict) {
		t.Fatalf("starting: %v", err)
	}
	for _, st := range []string{hub.RunActive, hub.RunPaused} {
		if _, err := svc.SetRunStatus(ctx, admin.ID, run.ID, st); err != nil {
			t.Fatal(err)
		}
		if err := svc.RemovePlugin(ctx, "net.example.wired", "1.0.0"); !errors.Is(err, hub.ErrConflict) {
			t.Fatalf("%s: %v", st, err)
		}
	}
	for _, st := range []string{hub.RunActive, hub.RunFinished} {
		if _, err := svc.SetRunStatus(ctx, admin.ID, run.ID, st); err != nil {
			t.Fatal(err)
		}
	}
	if err := svc.RemovePlugin(ctx, "net.example.wired", "1.0.0"); err != nil {
		t.Fatalf("finished: %v", err)
	}
	if _, err := svc.CreateRun(ctx, in("net.example.wired", "1.0.0")); !errors.Is(err, hub.ErrBadRequest) {
		t.Fatalf("removed: %v", err)
	}
}

func offerPluginVersions(t *testing.T, svc *hub.Service, adminID, id string, versions ...string) {
	t.Helper()
	for _, v := range versions {
		if _, err := svc.OfferStagedPlugin(ctx, stage(t, svc, hubtest.PluginZip(t, id, v, nil, nil)).StageID, nil, adminID); err != nil {
			t.Fatal(err)
		}
	}
}

func pluginVersions(t *testing.T, svc *hub.Service, id string) []string {
	t.Helper()
	ps, err := svc.ListPlugins(ctx)
	if err != nil {
		t.Fatal(err)
	}
	var out []string
	for _, p := range ps {
		if p.PluginID == id {
			out = append(out, p.Version)
		}
	}
	return out
}

// A newer plugin version replaces older ones; a version an unfinished run pins stays until the run ends or is deleted.
func TestNewerPluginVersionSupersedesOlder(t *testing.T) {
	svc := pluginHub(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := svc.AddROM(ctx, strings.NewReader("homebrew-rom-dummy"), "demo.nds", "Demo", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	const id = "net.example.sup"
	eq := func(want ...string) {
		t.Helper()
		if got := pluginVersions(t, svc, id); strings.Join(got, ",") != strings.Join(want, ",") {
			t.Fatalf("versions %v, want %v", got, want)
		}
	}
	offerPluginVersions(t, svc, admin.ID, id, "1.0.0")
	offerPluginVersions(t, svc, admin.ID, id, "1.1.0")
	eq("1.1.0")
	// A deliberately uploaded older version removes nothing and is not removed itself.
	offerPluginVersions(t, svc, admin.ID, id, "0.9.0")
	eq("0.9.0", "1.1.0")
	svc.PruneOldPlugins(ctx)
	eq("1.1.0")

	mk := func() hub.Run {
		run, err := svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "R", GameID: g.ID, Plugins: []hub.RunPluginRef{{PluginID: id, Version: "1.1.0"}}})
		if err != nil {
			t.Fatal(err)
		}
		return run
	}
	// Starting run pins 1.1.0: 1.2.0 arrives, 1.1.0 stays; finishing the run removes it.
	run := mk()
	offerPluginVersions(t, svc, admin.ID, id, "1.2.0")
	eq("1.1.0", "1.2.0")
	if _, err := svc.SetRunStatus(ctx, admin.ID, run.ID, hub.RunActive); err != nil {
		t.Fatal(err)
	}
	eq("1.1.0", "1.2.0")
	if _, err := svc.SetRunStatus(ctx, admin.ID, run.ID, hub.RunFinished); err != nil {
		t.Fatal(err)
	}
	eq("1.2.0")
	// Deleting an unfinished run releases the pin too.
	offerPluginVersions(t, svc, admin.ID, id, "1.3.0")
	eq("1.3.0")
	run2, err := svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "R2", GameID: g.ID, Plugins: []hub.RunPluginRef{{PluginID: id, Version: "1.3.0"}}})
	if err != nil {
		t.Fatal(err)
	}
	offerPluginVersions(t, svc, admin.ID, id, "1.4.0")
	eq("1.3.0", "1.4.0")
	if err := svc.DeleteRun(ctx, run2.ID); err != nil {
		t.Fatal(err)
	}
	eq("1.4.0")
}
