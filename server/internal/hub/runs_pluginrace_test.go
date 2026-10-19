package hub_test

import (
	"errors"
	"strings"
	"sync"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// PI-12: CreateRun re-checks the catalog under the plugin lock, and prune/remove check "in use" under the same lock,
// so a run never pins a plugin version that is gone. Meant to run with -race.
func TestCreateRunVersusPruneAndRemove(t *testing.T) {
	svc := pluginHub(t, nil)
	admin, err := svc.CreateAdmin(ctx, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := svc.AddROM(ctx, strings.NewReader("homebrew-rom-dummy"), "demo.nds", "Demo", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 25; i++ {
		id := "net.example.race" + strings.Repeat("x", i)
		offerPluginVersions(t, svc, admin.ID, id, "1.0.0")
		var wg sync.WaitGroup
		var run hub.Run
		var createErr, removeErr error
		wg.Add(2)
		go func() { // offering 2.0.0 prunes 1.0.0 unless a run pins it
			defer wg.Done()
			offerPluginVersions(t, svc, admin.ID, id, "2.0.0")
		}()
		go func() {
			defer wg.Done()
			run, createErr = svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "R", GameID: g.ID,
				Plugins: []hub.RunPluginRef{{PluginID: id, Version: "1.0.0"}}})
		}()
		wg.Wait()
		if createErr != nil {
			if !errors.Is(createErr, hub.ErrBadRequest) {
				t.Fatalf("iteration %d: %v", i, createErr)
			}
			continue // pruned first: refused cleanly
		}
		if _, err := svc.GetPlugin(ctx, id, "1.0.0"); err != nil {
			t.Fatalf("iteration %d: run %s pins 1.0.0 but the version is gone: %v", i, run.ID, err)
		}

		// Remove versus create on 2.0.0.
		wg.Add(2)
		var run2 hub.Run
		go func() {
			defer wg.Done()
			removeErr = svc.RemovePlugin(ctx, id, "2.0.0")
		}()
		go func() {
			defer wg.Done()
			run2, createErr = svc.CreateRun(ctx, hub.CreateRunInput{UserID: admin.ID, Name: "R2", GameID: g.ID,
				Plugins: []hub.RunPluginRef{{PluginID: id, Version: "2.0.0"}}})
		}()
		wg.Wait()
		if createErr == nil && removeErr == nil {
			t.Fatalf("iteration %d: run %s was created on 2.0.0 and 2.0.0 was removed", i, run2.ID)
		}
		if createErr == nil {
			if _, err := svc.GetPlugin(ctx, id, "2.0.0"); err != nil {
				t.Fatalf("iteration %d: run %s pins 2.0.0 but it is gone: %v", i, run2.ID, err)
			}
			if !errors.Is(removeErr, hub.ErrConflict) {
				t.Fatalf("iteration %d: remove: %v", i, removeErr)
			}
		}
	}
}
