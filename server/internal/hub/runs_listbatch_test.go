package hub_test

import (
	"fmt"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

// PI-15: runs are loaded in batches (chunks of ids), not one by one; the result keeps order and content across
// the chunk boundary.
func TestListRunsBatchedAcrossChunks(t *testing.T) {
	svc, clk, admin, g := runFixture(t)
	const n = 410
	for i := 0; i < n; i++ {
		clk.Advance(1e9) // distinct updated_at: newest first
		run, err := svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: fmt.Sprintf("Run %d", i), GameID: g.ID,
			Plugins: []hub.RunPluginRef{{PluginID: "net.example.board", Version: "1.0.0"}}})
		if err != nil {
			t.Fatal(err)
		}
		if i == 0 || i == n-1 {
			if _, _, err := svc.PutRunState(ctx, admin.ID, "dev", run.ID, "net.example.board", 0, []byte(`{"i":1}`)); err != nil {
				t.Fatal(err)
			}
		}
	}
	runs, err := svc.ListRuns(ctx, admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	if len(runs) != n {
		t.Fatalf("%d runs, want %d", len(runs), n)
	}
	for i, r := range runs {
		want := fmt.Sprintf("Run %d", n-1-i)
		if r.Name != want || len(r.Plugins) != 1 || len(r.Participants) != 1 {
			t.Fatalf("runs[%d] = %q plugins %d participants %d, want %q", i, r.Name, len(r.Plugins), len(r.Participants), want)
		}
		withState := i == 0 || i == n-1
		if got := len(r.Participants[0].States) == 1; got != withState {
			t.Fatalf("runs[%d] (%s): state present %v, want %v", i, r.Name, got, withState)
		}
	}
}
