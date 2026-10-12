package diag

import (
	"bytes"
	"context"
	"encoding/json"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestBundleExternalTURNWithoutSecrets(t *testing.T) {
	const secret, pw, user = "bundle-secret-0123456789", "bundle-pw-0123456789", "bundle-user-0123"
	svc, _ := hubtest.New(t, nil)
	o := Options{Config: config.Config{DataDir: t.TempDir()}, LogDir: t.TempDir(), NoJournal: true}

	// Not configured: reported as such.
	info, err := Collect(context.Background(), svc, o)
	if err != nil {
		t.Fatal(err)
	}
	if e := info.Network.ExternalTURN; e == nil || e.Configured || len(e.Servers) != 0 {
		t.Fatalf("%+v", e)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartExternalTURN(ctx, []hub.ExternalTURN{
		{Name: "vps", Mode: hub.ExternalTURNSecret, Secret: secret, URLs: []string{"turn:turn.example.org:3478?transport=udp"}},
		{Name: "provider", Mode: hub.ExternalTURNStatic, Username: user, Password: pw, URLs: []string{"turns:turn2.example.org:5349?transport=tcp"}},
	}, hub.ExternalTURNOptions{})
	info, err = Collect(context.Background(), svc, o)
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	if err := Write(context.Background(), &buf, info, o); err != nil {
		t.Fatal(err)
	}
	m := readZip(t, buf.Bytes())
	for name, c := range m {
		for _, s := range []string{secret, pw, user} {
			if strings.Contains(c, s) {
				t.Errorf("%s contains an external TURN secret", name)
			}
		}
	}
	var got map[string]any
	if err := json.Unmarshal([]byte(m[EntryInfo]), &got); err != nil {
		t.Fatalf("%v: %.120s", err, m[EntryInfo])
	}
	ext := got["network"].(map[string]any)["external_turn"].(map[string]any)
	servers := ext["servers"].([]any)
	s0 := servers[0].(map[string]any)
	if ext["configured"] != true || len(servers) != 2 || s0["mode"] != "secret" || s0["auth_configured"] != true ||
		s0["urls"].([]any)[0] != "turn:turn.example.org:3478?transport=udp" || servers[1].(map[string]any)["mode"] != "static" {
		t.Errorf("external_turn: %v", ext)
	}
}

func TestBundleExternalTURNShowsWebSettingsSourceWithoutSecrets(t *testing.T) {
	const secret = "saved-secret-0123456789"
	svc, _ := hubtest.New(t, nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc.StartExternalTURN(ctx, nil, hub.ExternalTURNOptions{})
	if err := svc.SaveExternalTURN(ctx, hub.ExternalTURNInput{Name: "web", Mode: "secret", Secret: secret, URLs: []string{"turn:turn.example.org:3478"}}); err != nil {
		t.Fatal(err)
	}
	o := Options{Config: config.Config{DataDir: t.TempDir()}, LogDir: t.TempDir(), NoJournal: true}
	info, err := Collect(ctx, svc, o)
	if err != nil {
		t.Fatal(err)
	}
	e := info.Network.ExternalTURN
	if e == nil || e.Source != "web" || !e.Active || len(e.Servers) != 1 || !e.Servers[0].SecretSet {
		t.Fatalf("%+v", e)
	}
	var buf bytes.Buffer
	if err := Write(ctx, &buf, info, o); err != nil {
		t.Fatal(err)
	}
	for name, c := range readZip(t, buf.Bytes()) {
		if strings.Contains(c, secret) {
			t.Errorf("%s contains the saved secret", name)
		}
	}
}
