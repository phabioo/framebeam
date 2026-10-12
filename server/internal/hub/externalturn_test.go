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
