package main

import (
	"bytes"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/hub"
)

func TestExternalTURNServersNeverLogSecrets(t *testing.T) {
	dir := t.TempDir()
	pwFile := filepath.Join(dir, "pw")
	os.WriteFile(pwFile, []byte("file-password-0123456789\n"), 0o600)
	t.Setenv("FRAMEBEAM_TURN_EXTERNAL_1_SECRET", "env-secret-0123456789")
	cfg := &config.Config{TURNExternal: []string{
		"name=vps;urls=turn:turn.example.org:3478?transport=udp",
		"name=prov;mode=static;username=turn-user-0123;urls=turns:turn2.example.org:5349;password-file=" + pwFile,
		"name=broken;urls=turn:turn3.example.org:3478", // no secret anywhere
	}}
	specs, err := cfg.ExternalTURNSpecs()
	if err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	got := externalTURNServers(cfg, specs, slog.New(slog.NewTextHandler(&buf, nil)))
	if len(got) != 2 || got[0].Mode != hub.ExternalTURNSecret || got[0].Secret != "env-secret-0123456789" ||
		got[1].Mode != hub.ExternalTURNStatic || got[1].Username != "turn-user-0123" || got[1].Password != "file-password-0123456789" {
		t.Fatalf("%+v", got)
	}
	for _, s := range []string{"env-secret-0123456789", "file-password-0123456789"} {
		if strings.Contains(buf.String(), s) {
			t.Fatalf("log contains a secret: %s", buf.String())
		}
	}
	if !strings.Contains(buf.String(), "name=broken") || !strings.Contains(buf.String(), "FRAMEBEAM_TURN_EXTERNAL_3_SECRET") {
		t.Fatalf("log: %s", buf.String())
	}
}
