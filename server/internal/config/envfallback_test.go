package config

import (
	"bytes"
	"log/slog"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/envcompat"
)

// The old FRAMEBEAM_ names still configure the server when the UZUME_ name is unset, and each one is reported once.
func TestOldEnvNamesFallBackAndAreReported(t *testing.T) {
	envcompat.Reset()
	t.Cleanup(envcompat.Reset)
	c := parse(t, map[string]string{
		"FRAMEBEAM_LISTEN":               ":9100",
		"FRAMEBEAM_HUB_UPDATE_INDEX_URL": "file:///tmp/updates-index.json",
		"FRAMEBEAM_HUB_CORE_TRUST_KEYS":  "",
		"FRAMEBEAM_SAVE_KEEP_RECENT":     "7",
		"UZUME_DATA_DIR":                 "/new",
		"FRAMEBEAM_DATA_DIR":             "/old", // the new name wins and is not reported
	})
	if c.Listen != ":9100" || c.UpdateIndexURL != "file:///tmp/updates-index.json" || c.SaveKeepRecent != 7 || c.DataDir != "/new" {
		t.Fatalf("%+v", c)
	}
	var buf bytes.Buffer
	reported := envcompat.WarnUsed(slog.New(slog.NewTextHandler(&buf, nil)))
	if len(reported) != 3 {
		t.Fatalf("reported %v", reported)
	}
	for _, want := range []string{
		"FRAMEBEAM_LISTEN is deprecated, use UZUME_LISTEN",
		"FRAMEBEAM_HUB_UPDATE_INDEX_URL is deprecated, use UZUME_SERVER_UPDATE_INDEX_URL",
		"FRAMEBEAM_SAVE_KEEP_RECENT is deprecated, use UZUME_SAVE_KEEP_RECENT",
	} {
		if !strings.Contains(buf.String(), want) {
			t.Errorf("missing %q in %s", want, buf.String())
		}
	}
	if strings.Contains(buf.String(), "FRAMEBEAM_DATA_DIR") {
		t.Errorf("a name whose UZUME_ counterpart is set must not be reported: %s", buf.String())
	}
}

func TestOldSecretEnvNamesAreReadToo(t *testing.T) {
	envcompat.Reset()
	t.Cleanup(envcompat.Reset)
	c := parse(t, nil)
	env := map[string]string{"FRAMEBEAM_TURN_EXTERNAL_1_SECRET": "old-secret-0123456789", "FRAMEBEAM_CLOUDFLARE_TURN_API_TOKEN": "tok"}
	get := func(k string) string { return env[k] }
	got, err := c.ExternalTURNSecret(ExternalTURNSpec{Name: "a", Mode: ExternalModeSecret}, 1, get)
	if err != nil || got != "old-secret-0123456789" {
		t.Fatalf("%q %v", got, err)
	}
	if tok, err := c.CloudflareTURNToken(get); err != nil || tok != "tok" {
		t.Fatalf("%q %v", tok, err)
	}
	u := envcompat.Used()
	if u["FRAMEBEAM_TURN_EXTERNAL_1_SECRET"] != "UZUME_TURN_EXTERNAL_1_SECRET" || u["FRAMEBEAM_CLOUDFLARE_TURN_API_TOKEN"] != "UZUME_CLOUDFLARE_TURN_API_TOKEN" {
		t.Fatalf("%v", u)
	}
}

func TestContainerDefaultDataDir(t *testing.T) {
	envcompat.Reset()
	t.Cleanup(envcompat.Reset)
	if c := parse(t, map[string]string{"UZUME_SERVER_CONTAINER": "1"}); c.DataDir != "/data" {
		t.Fatalf("container: %q", c.DataDir)
	}
	if c := parse(t, map[string]string{"FRAMEBEAM_HUB_CONTAINER": "1"}); c.DataDir != "/data" {
		t.Fatalf("old container name: %q", c.DataDir)
	}
	if c := parse(t, map[string]string{"UZUME_SERVER_CONTAINER": "1", "FRAMEBEAM_DATA_DIR": "/x"}); c.DataDir != "/x" {
		t.Fatalf("override: %q", c.DataDir)
	}
	if c := parse(t, map[string]string{"UZUME_SERVER_CONTAINER": "1", "UZUME_DATA_DIR": "/y"}); c.DataDir != "/y" {
		t.Fatalf("override: %q", c.DataDir)
	}
	if c := parse(t, map[string]string{"UZUME_SERVER_CONTAINER": "1"}); c.Listen != ":8443" {
		t.Fatalf("listen %q", c.Listen)
	}
}
