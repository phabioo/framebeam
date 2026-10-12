package hub_test

import (
	"context"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestRunPluginIndexRefreshLoop(t *testing.T) {
	dir := t.TempDir()
	url := hubtest.PluginIndex(t, dir, false, hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.loop", "1.0.0", nil, nil)))
	svc := pluginHub(t, func(o *hub.Options) { o.PluginIndexURL = url })
	c, cancel := context.WithCancel(ctx)
	done := make(chan struct{})
	reports := make(chan error, 4)
	go func() {
		defer close(done)
		svc.RunPluginIndexRefresh(c, time.Hour, func(err error) { reports <- err })
	}()
	select {
	case err := <-reports:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("no refresh at start")
	}
	if st, _ := svc.PluginIndexStatus(ctx); len(st.Catalog) != 1 {
		t.Fatalf("%+v", st)
	}
	cancel()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("loop did not stop")
	}
	// Without an index the loop returns at once.
	off := pluginHub(t, nil)
	finished := make(chan struct{})
	go func() { off.RunPluginIndexRefresh(ctx, time.Hour, nil); close(finished) }()
	select {
	case <-finished:
	case <-time.After(5 * time.Second):
		t.Fatal("loop without index keeps running")
	}
}
