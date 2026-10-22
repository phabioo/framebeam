package httpapi

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"
)

func TestAdminBackupsAPI(t *testing.T) {
	folder := t.TempDir()
	s := newSessEnv(t, nil)
	adminTok := s.login(uuid.NewString())
	user := s.device(s.anna.ID, "Annas PC")

	routes := []struct{ method, path string }{
		{"GET", "/api/v1/admin/backups"}, {"POST", "/api/v1/admin/backups"}, {"GET", "/api/v1/admin/backups/status"},
		{"POST", "/api/v1/admin/backups/cancel"}, {"GET", "/api/v1/admin/backups/settings"}, {"DELETE", "/api/v1/admin/backups/restore-result"},
		{"DELETE", "/api/v1/admin/backups/framebeam-hub-x-20261014T010000Z.tar"},
		{"POST", "/api/v1/admin/backups/framebeam-hub-x-20261014T010000Z.tar/restore"},
	}
	for _, r := range routes {
		wantStatus(t, s.do(r.method, r.path, nil, opt{}), 401, "")
		wantStatus(t, s.do(r.method, r.path, nil, opt{token: user.tok}), 403, "")
	}
	wantStatus(t, s.do("PUT", "/api/v1/admin/backups/settings", map[string]any{"keep": 3}, opt{token: user.tok}), 403, "")
	wantStatus(t, s.do("GET", "/api/v1/admin/backups/framebeam-hub-x-20261014T010000Z.tar/download", nil, opt{token: user.tok, raw: true}), 403, "")
	wantStatus(t, s.do("GET", "/api/v1/admin/backups/framebeam-hub-x-20261014T010000Z.tar/download", nil, opt{raw: true}), 401, "")

	// Settings.
	set := decode[map[string]any](t, s.do("GET", "/api/v1/admin/backups/settings", nil, opt{token: adminTok}))
	if set["mode"] != "off" || set["keep"] != float64(7) || set["folder"] != "" {
		t.Fatalf("%v", set)
	}
	wantStatus(t, s.do("PUT", "/api/v1/admin/backups/settings", map[string]any{"keep": 0}, opt{token: adminTok, raw: true}), 400, "")
	wantStatus(t, s.do("PUT", "/api/v1/admin/backups/settings", map[string]any{"folder": "relative"}, opt{token: adminTok}), 400, "")
	rec := s.do("PUT", "/api/v1/admin/backups/settings", map[string]any{"mode": "daily", "time": "04:00", "keep": 2, "folder": folder}, opt{token: adminTok})
	wantStatus(t, rec, 200, "")
	set = decode[map[string]any](t, rec)
	if set["mode"] != "daily" || set["time"] != "04:00" || set["folder"] != folder || set["keep"] != float64(2) {
		t.Fatalf("%v", set)
	}

	// Start, follow, list, download.
	wantStatus(t, s.do("POST", "/api/v1/admin/backups", nil, opt{token: adminTok}), 202, "")
	var st map[string]any
	for i := 0; i < 300; i++ {
		st = decode[map[string]any](t, s.do("GET", "/api/v1/admin/backups/status", nil, opt{token: adminTok}))
		if st["running"] == nil {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	last, _ := st["last"].(map[string]any)
	if st["running"] != nil || last == nil || last["ok"] != true || st["folder_set"] != true || st["next"] == nil {
		t.Fatalf("%v", st)
	}
	list := decode[struct {
		Backups []struct {
			Name     string `json:"name"`
			Size     int64  `json:"size"`
			Safety   bool   `json:"safety"`
			Contents string `json:"contents"`
		} `json:"backups"`
	}](t, s.do("GET", "/api/v1/admin/backups", nil, opt{token: adminTok}))
	if len(list.Backups) != 1 || list.Backups[0].Name != last["name"] || list.Backups[0].Contents == "" {
		t.Fatalf("%+v", list)
	}
	name := list.Backups[0].Name
	rec = s.do("GET", "/api/v1/admin/backups/"+name+"/download", nil, opt{token: adminTok, raw: true})
	wantStatus(t, rec, 200, "")
	if rec.Body.Len() != int(list.Backups[0].Size) || !strings.Contains(rec.Header().Get("Content-Disposition"), name) {
		t.Fatalf("download %d bytes, header %q", rec.Body.Len(), rec.Header().Get("Content-Disposition"))
	}
	wantStatus(t, s.do("GET", "/api/v1/admin/backups/framebeam-hub-x-20261014T010000Z.tar/download", nil, opt{token: adminTok, raw: true}), 404, "")

	// Cancel with nothing running; restore without a restart function; a second backup while none runs is fine.
	wantStatus(t, s.do("POST", "/api/v1/admin/backups/cancel", nil, opt{token: adminTok}), 409, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/backups/"+name+"/restore", nil, opt{token: adminTok}), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/backups/framebeam-hub-x-20261014T010000Z.tar/restore", nil, opt{token: adminTok}), 404, "")

	// Delete.
	wantStatus(t, s.do("DELETE", "/api/v1/admin/backups/"+name, nil, opt{token: adminTok}), 204, "")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/backups/"+name, nil, opt{token: adminTok}), 404, "")
	if ents, _ := os.ReadDir(folder); len(ents) != 0 {
		t.Fatalf("%v", ents)
	}
	// A restore result can be dismissed.
	wantStatus(t, s.do("DELETE", "/api/v1/admin/backups/restore-result", nil, opt{token: adminTok}), 204, "")

	// A folder that cannot be written is reported, and starting a backup is refused.
	bad := filepath.Join(folder, "f")
	os.WriteFile(bad, []byte("x"), 0o600)
	wantStatus(t, s.do("PUT", "/api/v1/admin/backups/settings", map[string]any{"folder": filepath.Join(bad, "sub")}, opt{token: adminTok}), 200, "")
	st = decode[map[string]any](t, s.do("GET", "/api/v1/admin/backups/status", nil, opt{token: adminTok}))
	if st["write_error"] == nil {
		t.Fatalf("%v", st)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/backups", nil, opt{token: adminTok}), 400, "")
}
