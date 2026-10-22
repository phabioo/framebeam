package web

import (
	"bytes"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"net/url"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/health"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/statusinfo"
	"github.com/phabioo/framebeam/server/internal/store"
	"github.com/phabioo/framebeam/server/internal/updates"
)

func rollbackFeed(t *testing.T, seed []byte, schemas map[string]int) string {
	t.Helper()
	dir := t.TempDir()
	var rels []updates.Release
	for v, schema := range schemas {
		name := "framebeam-hub_" + v + "_amd64.deb"
		data := []byte("dummy deb " + v)
		os.WriteFile(filepath.Join(dir, name), data, 0o644)
		sum := sha256.Sum256(data)
		rels = append(rels, updates.Release{Product: "hub", Channel: "stable", Version: v, PublishedAt: time.Now().UTC().Truncate(time.Second),
			ProtocolVersion: 1, MinProtocolVersion: 1, SchemaVersion: schema,
			Artifacts: []updates.Artifact{{Platform: "linux-amd64", Kind: "deb", Name: name, Size: int64(len(data)), SHA256: hex.EncodeToString(sum[:]),
				URL: updates.FileURL(filepath.Join(dir, name))}}})
	}
	b, err := updates.Marshal(updates.Index{Schema: 1, GeneratedAt: time.Now().UTC(), Releases: rels})
	if err != nil {
		t.Fatal(err)
	}
	sig, _ := corepkg.Sign(b, seed)
	p := filepath.Join(dir, "updates-index.json")
	os.WriteFile(p, b, 0o644)
	os.WriteFile(p+".sig", sig, 0o644)
	return updates.FileURL(p)
}

func rollbackEnv(t *testing.T, version string) (*env, string) {
	t.Helper()
	seed := bytes.Repeat([]byte{9}, 32)
	pub, _ := corepkg.PublicFromSeed(seed)
	known := store.KnownSchemaVersion()
	url := rollbackFeed(t, seed, map[string]int{"0.3.0": known, "0.2.0": known - 1, "0.1.0": 0})
	reqDir := t.TempDir()
	e := newEnvOpts(t, true, nil, func(o *hub.Options) {
		o.HubVersion, o.UpdateChannel = version, "stable"
		o.UpdateIndexURL, o.UpdateRequestDir, o.UpdatePlatform = url, reqDir, "linux-amd64"
		o.CoreTrustKeys = []ed25519.PublicKey{pub}
		o.Executable = updates.PackagedExecutable
	})
	return e, reqDir
}

func TestUpdatesEarlierVersionsPage(t *testing.T) {
	e, reqDir := rollbackEnv(t, "0.4.0")
	c := e.client()
	tok := c.login()

	// The plain page does not fetch the index; it offers the list.
	rec := c.get("/settings/updates", nil)
	status(t, rec, 200)
	contains(t, rec, "Earlier versions", "Show earlier versions")
	notContains(t, rec, "Back up and go back")

	rec = c.get("/settings/updates?earlier=1", nil)
	status(t, rec, 200)
	contains(t, rec, "0.3.0", "Data is kept as it is", "Back up and go back", "0.2.0", "Not possible:", "none exists", "0.1.0", "does not say", "A safety backup without ROMs is made first")

	// Posting needs the CSRF token.
	status(t, c.postForm("/settings/updates/rollback", url.Values{"version": {"0.3.0"}}, nil), 403)
	status(t, c.postForm("/settings/updates/resume", url.Values{}, nil), 403)
	if updates.RequestPending(reqDir) {
		t.Fatal("a request without token started a rollback")
	}

	// A refused version and an unknown one redirect with the reason and ask for nothing.
	rec = c.postForm("/settings/updates/rollback", url.Values{"_csrf": {tok}, "version": {"0.2.0"}, "confirm_loss": {"1"}}, nil)
	status(t, rec, 303)
	if loc := rec.Header().Get("Location"); loc != "/settings/updates?earlier=1&err=rollbackrefused" {
		t.Fatal(loc)
	}
	rec = c.postForm("/settings/updates/rollback", url.Values{"_csrf": {tok}, "version": {"9.9.9"}}, nil)
	if loc := rec.Header().Get("Location"); loc != "/settings/updates?earlier=1&err=rollbackversion" {
		t.Fatal(loc)
	}
	if updates.RequestPending(reqDir) {
		t.Fatal("nothing may be requested")
	}

	// Going back keeps the data.
	rec = c.postForm("/settings/updates/rollback", url.Values{"_csrf": {tok}, "version": {"0.3.0"}}, nil)
	status(t, rec, 303)
	if loc := rec.Header().Get("Location"); loc != "/settings/updates?ok=updateback" {
		t.Fatal(loc)
	}
	if !updates.RequestPending(reqDir) {
		t.Fatal("the root helper must be asked")
	}
	contains(t, c.get("/settings/updates?ok=updateback", nil), "Going back.")
}

func TestUpdatesPausedCardAndResume(t *testing.T) {
	e, _ := rollbackEnv(t, "0.3.0")
	pause := updates.Pause{From: "0.4.0", To: "0.3.0", At: time.Now().Add(-time.Hour).UTC(), By: "max", Backup: "framebeam-hub-x-20261022T100000Z-safety.tar", PrevAuto: "1"}
	for k, v := range map[string]string{updates.SettingPaused: updates.MarshalPause(pause), updates.SettingSkip: "0.4.0", updates.SettingAuto: "0"} {
		if _, err := e.db.Exec(`INSERT INTO settings(key, value) VALUES (?,?)`, k, v); err != nil {
			t.Fatal(err)
		}
	}
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/updates", nil)
	status(t, rec, 200)
	contains(t, rec, "Running 0.3.0", "automatic updates paused until you resume them", "Went back from 0.4.0", "by max", "Resume updates", "Paused after going back")

	// The same text is the banner problem of the Status page.
	if p := e.svc.UpdatePausedProblem(bg); p == nil || p.Title != "Updates paused" {
		t.Fatalf("%+v", p)
	}
	found := false
	for _, pr := range (&statusinfo.Source{Svc: e.svc}).Problems(bg) {
		found = found || (pr.ID == "update-paused" && pr.Severity == health.Warn && pr.ActionHref == "/settings/updates")
	}
	if !found {
		t.Fatal("the Status page problems must include the pause")
	}

	rec = c.postForm("/settings/updates/resume", url.Values{"_csrf": {tok}}, nil)
	status(t, rec, 303)
	if loc := rec.Header().Get("Location"); loc != "/settings/updates?ok=updateresume" {
		t.Fatal(loc)
	}
	notContains(t, c.get("/settings/updates", nil), "Resume updates")
	rec = c.postForm("/settings/updates/resume", url.Values{"_csrf": {tok}}, nil)
	if loc := rec.Header().Get("Location"); loc != "/settings/updates?err=resumefailed" {
		t.Fatal(loc)
	}
}
