package envcompat

import (
	"bytes"
	"log/slog"
	"strings"
	"testing"
)

func TestLegacyName(t *testing.T) {
	for in, want := range map[string]string{
		"UZUME_DATA_DIR":                "FRAMEBEAM_DATA_DIR",
		"UZUME_SERVER_UPDATE_INDEX_URL": "FRAMEBEAM_HUB_UPDATE_INDEX_URL",
		"UZUME_SERVER_CONTAINER":        "FRAMEBEAM_HUB_CONTAINER",
		"UZUME_APP_TRUST_KEYS":          "FRAMEBEAM_PLAYER_TRUST_KEYS",
		"UZUME_LOCAL_SERVER_PORT":       "FRAMEBEAM_LOCAL_HUB_PORT",
		"UZUME_TURN_EXTERNAL_1_SECRET":  "FRAMEBEAM_TURN_EXTERNAL_1_SECRET",
		"UZUME_SIGNING_KEY":             "FRAMEBEAM_SIGNING_KEY",
		"PATH":                          "",
		"FRAMEBEAM_DATA_DIR":            "",
		"UZUME_":                        "",
	} {
		if got := LegacyName(in); got != want {
			t.Errorf("LegacyName(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestWrapFallbackAndWarn(t *testing.T) {
	Reset()
	env := map[string]string{
		"UZUME_LISTEN":                   ":1",
		"FRAMEBEAM_LISTEN":               ":2", // new name wins, no hint
		"FRAMEBEAM_HUB_UPDATE_INDEX_URL": "https://old.example/i.json",
		"FRAMEBEAM_DATA_DIR":             "/old",
	}
	get := Wrap(func(k string) string { return env[k] })
	if get("UZUME_LISTEN") != ":1" || get("UZUME_SERVER_UPDATE_INDEX_URL") != "https://old.example/i.json" || get("UZUME_DATA_DIR") != "/old" {
		t.Fatal("wrong values")
	}
	if get("UZUME_NAME") != "" || get("HOME") != "" {
		t.Fatal("unset names must stay empty")
	}
	var buf bytes.Buffer
	log := slog.New(slog.NewTextHandler(&buf, nil))
	if n := WarnUsed(log); len(n) != 2 {
		t.Fatalf("reported %v", n)
	}
	out := buf.String()
	for _, w := range []string{"FRAMEBEAM_DATA_DIR is deprecated, use UZUME_DATA_DIR", "FRAMEBEAM_HUB_UPDATE_INDEX_URL is deprecated, use UZUME_SERVER_UPDATE_INDEX_URL"} {
		if !strings.Contains(out, w) {
			t.Errorf("missing %q in %s", w, out)
		}
	}
	if strings.Contains(out, "FRAMEBEAM_LISTEN") || strings.Count(out, "level=WARN") != 2 {
		t.Errorf("unexpected log: %s", out)
	}
	buf.Reset()
	get("UZUME_DATA_DIR")
	if n := WarnUsed(log); len(n) != 0 || buf.Len() != 0 {
		t.Fatalf("second call must not repeat: %v %s", n, buf.String())
	}
}
