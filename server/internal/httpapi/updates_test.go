package httpapi

import (
	"bytes"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/phabioo/uzume/server/internal/corepkg"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/store"
	"github.com/phabioo/uzume/server/internal/updates"
)

// writeUpdateFeed publishes a signed index with hub releases (version -> schema) for linux-amd64 and returns its URL.
func writeUpdateFeed(t *testing.T, seed []byte, schemas map[string]int) string {
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

func TestAdminUpdateRollbackAPI(t *testing.T) {
	seed := bytes.Repeat([]byte{5}, 32)
	pub, _ := corepkg.PublicFromSeed(seed)
	known := store.KnownSchemaVersion()
	url := writeUpdateFeed(t, seed, map[string]int{"0.3.0": known, "0.2.0": known - 1, "0.1.0": 0})
	reqDir := t.TempDir()
	s := newSessEnv(t, func(o *hub.Options) {
		o.HubVersion, o.UpdateChannel = "0.4.0", "stable"
		o.UpdateIndexURL, o.UpdateRequestDir, o.UpdatePlatform = url, reqDir, "linux-amd64"
		o.CoreTrustKeys = []ed25519.PublicKey{pub}
		o.Executable = updates.PackagedExecutable
	})
	adminTok := s.login(uuid.NewString())
	user := s.device(s.anna.ID, "Annas PC")

	for _, r := range []struct{ method, path string }{
		{"GET", "/api/v1/admin/updates/versions"}, {"POST", "/api/v1/admin/updates/rollback"}, {"POST", "/api/v1/admin/updates/resume"},
	} {
		body := any(nil)
		if r.method == "POST" && strings.HasSuffix(r.path, "rollback") {
			body = map[string]any{"version": "0.3.0"}
		}
		wantStatus(t, s.do(r.method, r.path, body, opt{}), 401, "")
		wantStatus(t, s.do(r.method, r.path, body, opt{token: user.tok}), 403, "")
	}
	if updates.RequestPending(reqDir) {
		t.Fatal("a non-admin must not trigger anything")
	}

	// The list.
	list := decode[struct {
		Current     string `json:"current"`
		CanRollback bool   `json:"can_rollback"`
		Versions    []struct {
			Version  string `json:"version"`
			Data     string `json:"data"`
			Reason   string `json:"reason"`
			DbBackup string `json:"db_backup"`
		} `json:"versions"`
	}](t, s.do("GET", "/api/v1/admin/updates/versions", nil, opt{token: adminTok}))
	if list.Current != "0.4.0" || !list.CanRollback || len(list.Versions) != 3 {
		t.Fatalf("%+v", list)
	}
	if list.Versions[0].Version != "0.3.0" || list.Versions[0].Data != "keep" ||
		list.Versions[1].Version != "0.2.0" || list.Versions[1].Data != "refuse" || !strings.Contains(list.Versions[1].Reason, "none exists") ||
		list.Versions[2].Data != "refuse" || !strings.Contains(list.Versions[2].Reason, "does not say") {
		t.Fatalf("%+v", list.Versions)
	}

	// Bad requests and refusals do nothing.
	wantStatus(t, s.do("POST", "/api/v1/admin/updates/rollback", map[string]any{"version": "9.9.9"}, opt{token: adminTok}), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/updates/rollback", map[string]any{"version": "0.2.0", "confirm_data_loss": true}, opt{token: adminTok}), 409, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/updates/resume", nil, opt{token: adminTok}), 409, "")
	if updates.RequestPending(reqDir) {
		t.Fatal("nothing may be requested")
	}

	// Going back keeps the data: 202, the helper is asked, updates are paused.
	rec := s.do("POST", "/api/v1/admin/updates/rollback", map[string]any{"version": "0.3.0"}, opt{token: adminTok})
	wantStatus(t, rec, 202, "")
	res := decode[map[string]any](t, rec)
	if res["version"] != "0.3.0" || res["data"] != "keep" || res["safety_backup"] == "" || res["safety_folder"] == "" {
		t.Fatalf("%v", res)
	}
	if !updates.RequestPending(reqDir) {
		t.Fatal("the root helper must be asked")
	}
}
