package pluginpkg

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// TestManifestCorpus runs the shared manifest corpus (testdata/manifests/*.json). valid-*.json must parse,
// invalid-*.json must be rejected. The app test (client/plugins/tests) is meant to run the same files through
// parseManifest, so the server never offers a plugin an app refuses to install.
func TestManifestCorpus(t *testing.T) {
	files, err := filepath.Glob(filepath.Join("testdata", "manifests", "*.json"))
	if err != nil || len(files) == 0 {
		t.Fatalf("no corpus files found: %v", err)
	}
	for _, f := range files {
		name := filepath.Base(f)
		var wantValid bool
		switch {
		case strings.HasPrefix(name, "valid-"):
			wantValid = true
		case strings.HasPrefix(name, "invalid-"):
		default:
			t.Errorf("%s: file name must start with valid- or invalid-", name)
			continue
		}
		t.Run(strings.TrimSuffix(name, ".json"), func(t *testing.T) {
			raw, err := os.ReadFile(f)
			if err != nil {
				t.Fatal(err)
			}
			_, err = ParseManifest(raw)
			if wantValid && err != nil {
				t.Fatalf("want valid, got %v", err)
			}
			if !wantValid && err == nil {
				t.Fatal("want invalid, got nil error")
			}
		})
	}
}
