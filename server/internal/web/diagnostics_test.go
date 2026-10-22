package web

import (
	"archive/zip"
	"bytes"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/config"
)

func TestDiagnosticsBundleDownload(t *testing.T) {
	dataDir := t.TempDir()
	logs := filepath.Join(dataDir, "logs")
	if err := os.MkdirAll(logs, 0o750); err != nil {
		t.Fatal(err)
	}
	planted := "time=x msg=turn turn_secret=PLANTEDSECRET1 password=PLANTEDPASS2 host=192.168.0.5\n"
	if err := os.WriteFile(filepath.Join(logs, "hub.log"), []byte(planted), 0o640); err != nil {
		t.Fatal(err)
	}
	e := newEnv(t, true, func(c *Config) { c.Net.Running = config.Config{DataDir: dataDir, Listen: ":8443"} })

	// Not signed in: redirected to the login page, nothing is built.
	anon := e.client()
	if rec := anon.postForm("/settings/diagnostics/bundle", url.Values{}, nil); rec.Code != 303 || location(rec) != "/login" {
		t.Fatalf("anonymous: %d -> %q", rec.Code, location(rec))
	}
	c := e.client()
	tok := c.login()

	// The settings section explains what is in the file and offers a POST form with the CSRF token.
	rec := c.get("/settings/diagnostics", nil)
	status(t, rec, 200)
	contains(t, rec, "Download diagnostics bundle", "contains IP addresses and host names, no passwords or tokens",
		`action="/settings/diagnostics/bundle"`, `name="_csrf"`)
	notContains(t, rec, "Stream")

	// A download is an action: GET is not served, POST needs the CSRF token.
	if rec := c.get("/settings/diagnostics/bundle", nil); rec.Code == 200 {
		t.Fatalf("GET must not deliver the bundle: %d", rec.Code)
	}
	status(t, c.postForm("/settings/diagnostics/bundle", url.Values{}, nil), 403)
	status(t, c.postForm("/settings/diagnostics/bundle", url.Values{"_csrf": {"wrong"}}, nil), 403)

	rec = postTok(c, tok, "/settings/diagnostics/bundle", nil)
	status(t, rec, 200)
	if ct := rec.Header().Get("Content-Type"); ct != "application/zip" {
		t.Fatalf("content type %q", ct)
	}
	cd := rec.Header().Get("Content-Disposition")
	if !regexp.MustCompile(`^attachment; filename="uzume-diagnostics-\d{8}T\d{6}Z\.zip"$`).MatchString(cd) {
		t.Fatalf("content disposition %q", cd)
	}
	if rec.Header().Get("Cache-Control") != "no-store" {
		t.Fatal("bundle must not be cached")
	}
	zr, err := zip.NewReader(bytes.NewReader(rec.Body.Bytes()), int64(rec.Body.Len()))
	if err != nil {
		t.Fatal(err)
	}
	got := map[string]string{}
	for _, f := range zr.File {
		rc, _ := f.Open()
		b, _ := io.ReadAll(rc)
		rc.Close()
		got[f.Name] = string(b)
	}
	for _, n := range []string{"README.txt", "hub-info.json", "logs/hub.log"} {
		if _, ok := got[n]; !ok {
			t.Errorf("missing %s", n)
		}
	}
	for n, c := range got {
		if strings.Contains(c, "PLANTEDSECRET1") || strings.Contains(c, "PLANTEDPASS2") {
			t.Errorf("%s contains a planted secret", n)
		}
	}
	if !strings.Contains(got["logs/hub.log"], "192.168.0.5") {
		t.Error("host address removed")
	}
}
