package hub_test

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

const extSecret = "shared-secret-for-tests"

func TestExternalTURNSecretModeKnownVector(t *testing.T) {
	svc, _ := hubtest.New(t, nil) // clock 2026-10-05 12:00:00 UTC, credentials valid 12 h
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartExternalTURN(ctx, []hub.ExternalTURN{{Name: "vps", Mode: hub.ExternalTURNSecret, Secret: extSecret,
		URLs: []string{"turn:turn.example.org:3478?transport=udp", "turns:turn.example.org:5349?transport=tcp"}}}, hub.ExternalTURNOptions{})
	if !svc.RelayEnabled() {
		t.Fatal("turn_v1 must be announced while an external TURN server is on")
	}
	got := svc.ExternalCredentialsForTest("dev-1")
	if len(got) != 1 {
		t.Fatalf("%+v", got)
	}
	// Reference value: printf '1791244800:dev-1' | openssl dgst -sha1 -hmac <secret> -binary | base64
	if got[0].Username != "1791244800:dev-1" || got[0].Credential != "RyUulBRhnlPj0a8byYb0cMeD0ho=" ||
		got[0].ExpiresAt.Unix() != 1791244800 || len(got[0].URLs) != 2 {
		t.Fatalf("%+v", got[0])
	}
	if o := svc.ExternalCredentialsForTest("dev-2"); o[0].Username == got[0].Username || o[0].Credential == got[0].Credential {
		t.Fatal("credentials must be per device")
	}
}

func TestExternalTURNStaticModeAndStatusWithoutSecrets(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartExternalTURN(ctx, []hub.ExternalTURN{
		{Name: "static", Mode: hub.ExternalTURNStatic, Username: "turnuser", Password: "static-pw-0123", URLs: []string{"turn:turn.example.org:3478"}},
		{Name: "shared", Mode: hub.ExternalTURNSecret, Secret: extSecret, URLs: []string{"turn:turn2.example.org:3478"}},
	}, hub.ExternalTURNOptions{})
	got := svc.ExternalCredentialsForTest("dev-1")
	if len(got) != 2 || got[0].Username != "turnuser" || got[0].Credential != "static-pw-0123" || !got[0].ExpiresAt.IsZero() {
		t.Fatalf("%+v", got)
	}
	st := svc.ExternalTURNStatus()
	if len(st) != 2 || !st[0].SecretSet || st[0].Mode != "static" || st[1].Mode != "secret" {
		t.Fatalf("%+v", st)
	}
	for _, s := range st {
		if strings.Contains(strings.Join(s.URLs, " ")+s.Name, "static-pw-0123") || strings.Contains(strings.Join(s.URLs, " "), extSecret) {
			t.Fatal("status holds a secret")
		}
	}
}

func TestExternalTURNStopsWithContext(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	svc.StartExternalTURN(ctx, []hub.ExternalTURN{{Name: "a", Mode: hub.ExternalTURNSecret, Secret: extSecret,
		URLs: []string{"turn:turn.example.org:3478"}}}, hub.ExternalTURNOptions{Refresh: 5 * time.Millisecond})
	cancel()
	waitFor(t, "stopped", func() bool { return !svc.RelayEnabled() })
}

func startExt(t *testing.T, flags []hub.ExternalTURN) (*hub.Service, context.Context) {
	t.Helper()
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	svc.StartExternalTURN(ctx, flags, hub.ExternalTURNOptions{Refresh: time.Hour})
	return svc, ctx
}

func TestSavedExternalTURNOverridesFlagsAndKeepsFlagSecrets(t *testing.T) {
	flag := hub.ExternalTURN{Name: "vps", Mode: hub.ExternalTURNSecret, Secret: extSecret, URLs: []string{"turn:turn.example.org:3478"}}
	svc, ctx := startExt(t, []hub.ExternalTURN{flag})
	if svc.ExternalTURNSource() != hub.ExternalTURNFromFlags || len(svc.ExternalCredentialsForTest("d")) != 1 {
		t.Fatal("flags must apply while nothing is saved")
	}
	// First edit seeds the saved list from the flags; the env/file secret keeps working for the entry.
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Original: "vps", Name: "vps-renamed", Mode: "secret",
		URLs: []string{"turns:turn.example.org:5349?transport=tcp"}}); err != nil {
		t.Fatal(err)
	}
	if svc.ExternalTURNSource() != hub.ExternalTURNFromWeb {
		t.Fatal("saved list must win")
	}
	got := svc.ExternalCredentialsForTest("dev-1")
	if len(got) != 1 || got[0].URLs[0] != "turns:turn.example.org:5349?transport=tcp" || got[0].Credential != "RyUulBRhnlPj0a8byYb0cMeD0ho=" {
		t.Fatalf("flag secret lost after edit: %+v", got)
	}
	// Add: needs a secret; edit with empty secret keeps the stored one; clear removes it and stops using the server.
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Name: "prov", Mode: "static", Username: "u", URLs: []string{"turn:p.example.org"}}); err == nil {
		t.Fatal("add without password accepted")
	}
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Name: "prov", Mode: "static", Username: "u", Secret: "pw-1", URLs: []string{"turn:p.example.org"}}); err != nil {
		t.Fatal(err)
	}
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Original: "prov", Name: "prov", Mode: "static", Username: "u2", URLs: []string{"turn:p.example.org"}}); err != nil {
		t.Fatal(err)
	}
	if c := svc.ExternalCredentialsForTest("d"); len(c) != 2 || c[1].Username != "u2" || c[1].Credential != "pw-1" {
		t.Fatalf("%+v", c)
	}
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Original: "prov", Name: "prov", Mode: "static", Username: "u2", URLs: []string{"turn:p.example.org"}, ClearSecret: true}); err != nil {
		t.Fatal(err)
	}
	if c := svc.ExternalCredentialsForTest("d"); len(c) != 1 {
		t.Fatalf("server without secret must not be used: %+v", c)
	}
	if st := svc.ExternalTURNStatus(); len(st) != 2 || st[1].SecretSet {
		t.Fatalf("%+v", st)
	}
	// Reset: flags apply again.
	if err := svc.ResetExternalTURN(ctx); err != nil || svc.ExternalTURNSource() != hub.ExternalTURNFromFlags || len(svc.ExternalTURNStatus()) != 1 {
		t.Fatalf("reset: %v", err)
	}
}

func TestSaveExternalTURNValidation(t *testing.T) {
	svc, ctx := startExt(t, nil)
	ok := hub.ExternalTURNInput{Name: "a", Mode: "secret", Secret: "s", URLs: []string{"turn:turn.example.org:3478"}}
	if err := svc.SaveExternalTURN(ctx, ok); err != nil {
		t.Fatal(err)
	}
	bad := map[string]func(*hub.ExternalTURNInput){
		"url scheme":  func(i *hub.ExternalTURNInput) { i.URLs = []string{"http://turn.example.org"} },
		"url query":   func(i *hub.ExternalTURNInput) { i.URLs = []string{"turn:turn.example.org?transport=sctp"} },
		"no urls":     func(i *hub.ExternalTURNInput) { i.URLs = nil },
		"name":        func(i *hub.ExternalTURNInput) { i.Name = "a b" },
		"duplicate":   func(i *hub.ExternalTURNInput) { i.Name = "a" },
		"mode":        func(i *hub.ExternalTURNInput) { i.Mode = "oauth" },
		"static user": func(i *hub.ExternalTURNInput) { i.Name, i.Mode = "b", "static" },
		"newline":     func(i *hub.ExternalTURNInput) { i.Name, i.Secret = "b", "x\ny" },
	}
	for what, mod := range bad {
		in := ok
		mod(&in)
		if err := svc.SaveExternalTURN(ctx, in); err == nil {
			t.Errorf("%s accepted", what)
		}
	}
	if len(svc.ExternalTURNStatus()) != 1 {
		t.Fatalf("%+v", svc.ExternalTURNStatus())
	}
}

func TestSavedExternalTURNOffWhileDisabled(t *testing.T) {
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartExternalTURN(ctx, nil, hub.ExternalTURNOptions{Disabled: true})
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Name: "a", Mode: "secret", Secret: "s", URLs: []string{"turn:turn.example.org"}}); err != nil {
		t.Fatal(err)
	}
	if svc.RelayEnabled() || svc.ExternalTURNActive() || len(svc.ExternalTURNStatus()) != 1 {
		t.Fatal("configured but must not be handed out while disabled")
	}
}
