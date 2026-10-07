package hub_test

import (
	"errors"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestNetOverridesStoreAndReset(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	if ov, err := svc.NetOverrides(ctx); err != nil || len(ov) != 0 {
		t.Fatalf("%v %v", ov, err)
	}
	for k, v := range map[string]string{"listen_port": "9000", "public_host": "", "ice_servers": `["stun:stun.example.com:3478"]`} {
		if err := svc.SetNetOverride(ctx, k, v); err != nil {
			t.Fatal(err)
		}
	}
	svc.SetNetOverride(ctx, "listen_port", "9001") // overwrite
	ov, _ := svc.NetOverrides(ctx)
	if len(ov) != 3 || ov["listen_port"] != "9001" || ov["public_host"] != "" {
		t.Fatalf("%v", ov)
	}
	// Other settings are not part of the overrides.
	svc.SetAppearance(ctx, "dark")
	if ov, _ = svc.NetOverrides(ctx); len(ov) != 3 {
		t.Fatalf("%v", ov)
	}
	if err := svc.ResetNetOverride(ctx, "listen_port"); err != nil {
		t.Fatal(err)
	}
	if ov, _ = svc.NetOverrides(ctx); len(ov) != 2 || ov["listen_port"] != "" && len(ov["listen_port"]) > 0 {
		t.Fatalf("%v", ov)
	}
	if _, ok := ov["listen_port"]; ok {
		t.Fatal("not reset")
	}
	for _, bad := range []string{"", "Listen", "a.b", "x y"} {
		if err := svc.SetNetOverride(ctx, bad, "1"); !errors.Is(err, hub.ErrBadRequest) {
			t.Fatalf("%q: %v", bad, err)
		}
	}
}

func TestLiveRetentionAndICEServers(t *testing.T) {
	svc, _ := hubtest.New(t, func(o *hub.Options) {
		o.SaveKeepRecent, o.SaveKeepDaily, o.SaveKeepWeekly = 20, 30, 26
		o.ICEServers = []string{"stun:a.example.com:3478"}
	})
	if r, d, w := svc.SaveRetention(); r != 20 || d != 30 || w != 26 {
		t.Fatalf("%d %d %d", r, d, w)
	}
	svc.SetSaveRetention(5, 0, 7)
	if r, d, w := svc.SaveRetention(); r != 5 || d != 0 || w != 7 {
		t.Fatalf("%d %d %d", r, d, w)
	}
	svc.SetICEServers([]string{"stun:b.example.com:3478"})
	got := svc.ICEServers()
	if len(got) != 1 || got[0] != "stun:b.example.com:3478" {
		t.Fatalf("%v", got)
	}
	got[0] = "changed" // callers get a copy
	if svc.ICEServers()[0] != "stun:b.example.com:3478" {
		t.Fatal("ICEServers returned the internal slice")
	}
	svc.SetICEServers(nil)
	if len(svc.ICEServers()) != 0 {
		t.Fatal("not cleared")
	}
}
