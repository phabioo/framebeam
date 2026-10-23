package httpapi

import (
	"archive/zip"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/phabioo/uzume/server/internal/adminhost"
	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
)

// fakeHost stands in for the web interface server (process-level facts).
type fakeHost struct {
	stored    map[string]string
	importDir string
	restarted chan struct{}
	renewed   int
}

func newFakeHost(importDir string) *fakeHost {
	return &fakeHost{stored: map[string]string{}, importDir: importDir, restarted: make(chan struct{}, 1)}
}

func (h *fakeHost) cfg(withStored bool) config.Config {
	c := config.Config{Listen: ":8443", NetworkSharing: true, SaveKeepRecent: 10, SaveKeepDaily: 7, SaveKeepWeekly: 4}
	if withStored && h.stored[config.NetKeepRecent] != "" {
		fmt.Sscan(h.stored[config.NetKeepRecent], &c.SaveKeepRecent)
	}
	return c
}

func (h *fakeHost) NetState(context.Context) (adminhost.NetState, error) {
	st := adminhost.NetState{Base: h.cfg(false), Running: h.cfg(false), Desired: h.cfg(true), Stored: map[string]string{}, CanRestart: true}
	for k, v := range h.stored {
		st.Stored[k] = v
	}
	if h.stored[config.NetListenPort] != "" {
		st.Pending = []string{config.NetLabel(config.NetListenPort)}
	}
	return st, nil
}

func (h *fakeHost) SaveNet(_ context.Context, key string, form url.Values) error {
	if key == "nope" {
		return hub.ErrNotFound
	}
	v := form.Get("value")
	if key == config.NetListenPort && v == "80" {
		return &adminhost.ValidationError{Msg: "Port 80 is not allowed"}
	}
	h.stored[key] = v
	return nil
}

func (h *fakeHost) ResetNet(_ context.Context, key string) error {
	if key == "nope" {
		return hub.ErrNotFound
	}
	delete(h.stored, key)
	return nil
}

func (h *fakeHost) PrepareRestart(context.Context, string) (adminhost.Restart, func(), error) {
	return adminhost.Restart{URL: "https://hub.example:9443/settings/network", PortChanged: true}, func() { h.restarted <- struct{}{} }, nil
}

func (h *fakeHost) Security() adminhost.Security {
	return adminhost.Security{TLS: true, Fingerprint: "AA:BB", CertSource: "Self-generated", NotAfter: time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC), CanRenew: true}
}

func (h *fakeHost) RenewCert(string) (string, time.Time, error) {
	h.renewed++
	return "CC:DD", time.Date(2031, 1, 1, 0, 0, 0, 0, time.UTC), nil
}

func (h *fakeHost) ImportFolder() (string, int64) { return h.importDir, 0 }
func (h *fakeHost) RunningConfig() config.Config  { return h.cfg(false) }

func newPartBEnv(t *testing.T) (*sessEnv, *fakeHost, dev, dev) {
	t.Helper()
	h := newFakeHost(t.TempDir())
	s := newSessEnv(t, nil, WithHost(h))
	return s, h, s.device(s.admin.ID, "Admin PC"), s.device(s.anna.ID, "Annas PC")
}

func TestAdminPartBRequiresAdmin(t *testing.T) {
	s, _, _, member := newPartBEnv(t)
	g, u, c := s.game.ID, s.anna.ID, uuid.NewString()
	sv := "/api/v1/admin/users/" + u + "/games/" + g + "/saves/default"
	r := "/api/v1/admin/runs/" + c
	adminAuthzTable(t, s, member, [][2]string{
		{"GET", "/api/v1/admin/library/rescan"}, {"POST", "/api/v1/admin/library/rescan"},
		{"PATCH", "/api/v1/admin/games/" + g}, {"DELETE", "/api/v1/admin/games/" + g},
		{"GET", "/api/v1/admin/saves"}, {"GET", "/api/v1/admin/users/" + u + "/saves/export"},
		{"GET", sv}, {"GET", sv + "/history"}, {"GET", sv + "/content"}, {"GET", sv + "/history/1/content"}, {"DELETE", sv + "/history/1"},
		{"POST", sv + "/history/1/restore"}, {"POST", sv + "/snapshots"}, {"POST", sv + "/conflicts/x/resolve"}, {"POST", sv + "/slots"},
		{"POST", sv + "/rename"}, {"DELETE", sv + "?confirm_revision=1"},
		{"GET", "/api/v1/admin/runs"}, {"GET", r}, {"POST", r + "/pause"}, {"POST", r + "/resume"}, {"POST", r + "/archive"},
		{"POST", r + "/unarchive"}, {"POST", r + "/start"}, {"POST", r + "/finish"}, {"POST", r + "/resend"},
		{"POST", r + "/invites/" + u + "/resend"}, {"DELETE", r + "/invites/" + u}, {"GET", r + "/export"}, {"DELETE", r},
		{"GET", "/api/v1/admin/settings"}, {"PATCH", "/api/v1/admin/settings"},
		{"PUT", "/api/v1/admin/settings/network/listen_port"}, {"DELETE", "/api/v1/admin/settings/network/listen_port"},
		{"POST", "/api/v1/admin/settings/network/restart"},
		{"PUT", "/api/v1/admin/settings/external-turn"}, {"DELETE", "/api/v1/admin/settings/external-turn?name=x"},
		{"POST", "/api/v1/admin/settings/external-turn/reset"},
		{"POST", "/api/v1/admin/updates/check"}, {"POST", "/api/v1/admin/updates/install"},
		{"POST", "/api/v1/admin/security/renew-cert"}, {"POST", "/api/v1/admin/diagnostics/bundle"},
	})
	// The upload needs its headers before the token is looked at.
	up := func(o opt) int {
		o.raw, o.data, o.ctype = true, []byte("x"), "application/octet-stream"
		o.header = map[string]string{"X-Uzume-Expected-Revision": "0", "X-Uzume-Content-SHA256": hexSHA([]byte("x"))}
		return s.do("POST", sv+"/upload", nil, o).Code
	}
	if c := up(opt{}); c != 401 {
		t.Fatal(c)
	}
	if c := up(opt{token: member.tok}); c != 403 {
		t.Fatal(c)
	}
	if _, err := s.svc.GetGame(context.Background(), g); err != nil {
		t.Fatal(err)
	}
}

func TestAdminLibraryAPI(t *testing.T) {
	s, h, adm, _ := newPartBEnv(t)
	tok := opt{token: adm.tok}
	s.confirm(adm.tok)

	// An admin uploads a ROM with the member API; there is no admin variant.
	rom := []byte("homebrew-rom-uploaded-by-admin")
	rec := s.do("POST", "/api/v1/games?filename=up.nds", nil, opt{token: adm.tok, data: rom, ctype: "application/octet-stream", raw: true})
	if rec.Code != 201 && rec.Code != 200 {
		t.Fatalf("%d %s", rec.Code, rec.Body)
	}

	// Rescan: a file in the import folder is imported in the background.
	if err := os.WriteFile(filepath.Join(h.importDir, "scanned.nds"), []byte("homebrew-from-folder"), 0o600); err != nil {
		t.Fatal(err)
	}
	rec = s.do("POST", "/api/v1/admin/library/rescan", nil, tok)
	wantStatus(t, rec, 202, "")
	deadline := time.Now().Add(5 * time.Second)
	var scan struct {
		Running, Finished bool
		Added             int
		Summary           *struct{ Added int }
	}
	for {
		scan = decode[struct {
			Running, Finished bool
			Added             int
			Summary           *struct{ Added int }
		}](t, s.do("GET", "/api/v1/admin/library/rescan", nil, tok))
		if scan.Finished && !scan.Running {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("scan did not finish: %+v", scan)
		}
		time.Sleep(10 * time.Millisecond)
	}
	if scan.Summary == nil || scan.Summary.Added != 1 {
		t.Fatalf("%+v", scan)
	}
	// A missing folder is a 400.
	h.importDir = filepath.Join(h.importDir, "gone")
	wantStatus(t, s.do("POST", "/api/v1/admin/library/rescan", nil, tok), 400, "bad_request")

	// Rename and delete.
	rec = s.do("PATCH", "/api/v1/admin/games/"+s.game.ID, map[string]any{"title": "Renamed Demo"}, tok)
	wantStatus(t, rec, 200, "")
	if g, _ := s.svc.GetGame(context.Background(), s.game.ID); g.Title != "Renamed Demo" {
		t.Fatalf("%+v", g)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/games/"+s.game.ID, map[string]any{"title": "  "}, tok), 400, "bad_request")
	wantStatus(t, s.do("PATCH", "/api/v1/admin/games/"+uuid.NewString(), map[string]any{"title": "X"}, tok), 404, "not_found")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/games/"+s.game.ID, nil, tok), 204, "")
	if _, err := s.svc.GetGame(context.Background(), s.game.ID); err == nil {
		t.Fatal("game still there")
	}
	wantStatus(t, s.do("DELETE", "/api/v1/admin/games/"+s.game.ID, nil, tok), 404, "not_found")
}

func TestAdminLibraryRescanWithoutHostIs404(t *testing.T) {
	s := newSessEnv(t, nil)
	adm := s.device(s.admin.ID, "Admin PC")
	wantStatus(t, s.do("POST", "/api/v1/admin/library/rescan", nil, opt{token: adm.tok, raw: true}), 404, "not_found")
}

type slotT struct {
	Slot    string
	Current struct {
		Revision int
		Sha256   string
		Reason   string
	}
	OpenConflicts []struct {
		ID string
	} `json:"open_conflicts"`
}

func TestAdminSavesAPI(t *testing.T) {
	s, _, adm, _ := newPartBEnv(t)
	annaDev, bobDev := s.device(s.anna.ID, "Anna PC"), s.device(s.bob.ID, "Bob PC")
	tok := opt{token: adm.tok}
	s.confirm(adm.tok)
	put := func(d dev, slot string, base int, data []byte) {
		t.Helper()
		rec := s.do("PUT", "/api/v1/games/"+s.game.ID+"/saves/"+slot, nil, opt{token: d.tok, data: data, ctype: "application/octet-stream",
			header: map[string]string{"X-Uzume-Base-Revision": fmt.Sprint(base), "X-Uzume-Content-SHA256": hexSHA(data), "X-Uzume-Sync-Reason": "final"}})
		wantStatus(t, rec, 200, "")
	}
	put(annaDev, "default", 0, []byte("anna-1"))
	put(annaDev, "default", 1, []byte("anna-2"))
	put(bobDev, "default", 0, []byte("bob-1"))
	base := "/api/v1/admin/users/" + s.anna.ID + "/games/" + s.game.ID + "/saves/default"

	// List with filters.
	type sum struct {
		Saves []struct {
			UserID            string `json:"user_id"`
			Username          string
			GameID            string `json:"game_id"`
			GameTitle         string `json:"game_title"`
			Slot              string
			OpenConflictCount int `json:"open_conflict_count"`
		}
	}
	all := decode[sum](t, s.do("GET", "/api/v1/admin/saves", nil, tok))
	if len(all.Saves) != 2 || all.Saves[0].GameTitle != "Demo Homebrew" {
		t.Fatalf("%+v", all)
	}
	if f := decode[sum](t, s.do("GET", "/api/v1/admin/saves?user_id="+s.bob.ID, nil, tok)); len(f.Saves) != 1 || f.Saves[0].Username != "bob" {
		t.Fatalf("%+v", f)
	}
	if f := decode[sum](t, s.do("GET", "/api/v1/admin/saves?game_id="+uuid.NewString(), nil, tok)); len(f.Saves) != 0 {
		t.Fatalf("%+v", f)
	}

	// Slot, history, downloads.
	sl := decode[slotT](t, s.do("GET", base, nil, tok))
	if sl.Current.Revision != 2 {
		t.Fatalf("%+v", sl)
	}
	wantStatus(t, s.do("GET", "/api/v1/admin/users/"+s.anna.ID+"/games/"+s.game.ID+"/saves/nope", nil, tok), 404, "not_found")
	wantStatus(t, s.do("POST", base+"/snapshots", map[string]any{"label": "keep"}, tok), 201, "")
	put(annaDev, "default", 2, []byte("anna-3"))
	hist := decode[struct {
		Versions []struct{ Version, Revision int }
	}](t, s.do("GET", base+"/history", nil, tok)).Versions
	if len(hist) != 1 {
		t.Fatalf("%+v", hist)
	}
	if rec := s.do("GET", base+"/content", nil, tok); rec.Code != 200 || rec.Body.String() != "anna-3" || rec.Header().Get("X-Uzume-Save-Revision") != "3" {
		t.Fatalf("%d %q %v", rec.Code, rec.Body, rec.Header())
	}
	if rec := s.do("GET", fmt.Sprintf("%s/history/%d/content", base, hist[0].Version), nil, tok); rec.Code != 200 || rec.Body.String() != "anna-2" {
		t.Fatalf("%d %q", rec.Code, rec.Body)
	}
	wantStatus(t, s.do("GET", base+"/history/99/content", nil, tok), 404, "not_found")

	// Restore: the expected revision must match; the restore is a new revision, nothing is lost.
	rs := fmt.Sprintf("%s/history/%d/restore", base, hist[0].Version)
	wantStatus(t, s.do("POST", rs, map[string]any{"expected_revision": 2}, tok), 409, "")
	sl = decode[slotT](t, s.do("POST", rs, map[string]any{"expected_revision": 3}, tok))
	if sl.Current.Revision != 4 || sl.Current.Reason != "restore" {
		t.Fatalf("%+v", sl)
	}
	if rec := s.do("GET", base+"/content", nil, tok); rec.Body.String() != "anna-2" {
		t.Fatalf("%q", rec.Body)
	}

	// Snapshot, then delete a version.
	rec := s.do("POST", base+"/snapshots", map[string]any{"label": "before boss"}, tok)
	wantStatus(t, rec, 201, "")
	snap := decode[struct {
		Version int
		Label   *string
	}](t, rec)
	if snap.Label == nil || *snap.Label != "before boss" {
		t.Fatalf("%+v", snap)
	}
	wantStatus(t, s.do("DELETE", fmt.Sprintf("%s/history/%d", base, snap.Version), nil, tok), 204, "")
	wantStatus(t, s.do("DELETE", fmt.Sprintf("%s/history/%d", base, snap.Version), nil, tok), 404, "not_found")

	// New slot (copy), rename, upload and delete with the revision confirmed.
	rec = s.do("POST", base+"/slots", map[string]any{"name": "copy"}, tok)
	wantStatus(t, rec, 201, "")
	if c := decode[slotT](t, rec); c.Slot != "copy" || c.Current.Revision != 1 {
		t.Fatalf("%+v", c)
	}
	wantStatus(t, s.do("POST", base+"/slots", map[string]any{"name": "copy"}, tok), 409, "")
	wantStatus(t, s.do("POST", base+"/slots", map[string]any{"name": "Bad Name"}, opt{token: adm.tok, raw: true}), 400, "")
	cp := "/api/v1/admin/users/" + s.anna.ID + "/games/" + s.game.ID + "/saves/copy"
	wantStatus(t, s.do("POST", cp+"/rename", map[string]any{"name": "renamed"}, tok), 200, "")
	rn := "/api/v1/admin/users/" + s.anna.ID + "/games/" + s.game.ID + "/saves/renamed"
	data := []byte("uploaded-by-admin")
	up := func(rev int, sha string) int {
		return s.do("POST", rn+"/upload", nil, opt{token: adm.tok, data: data, ctype: "application/octet-stream", raw: true,
			header: map[string]string{"X-Uzume-Expected-Revision": fmt.Sprint(rev), "X-Uzume-Content-SHA256": sha}}).Code
	}
	if c := up(0, hexSHA(data)); c != 409 { // stale: the slot is at revision 1
		t.Fatal(c)
	}
	if c := up(1, hexSHA([]byte("other"))); c != 400 {
		t.Fatal(c)
	}
	if c := up(1, hexSHA(data)); c != 200 {
		t.Fatal(c)
	}
	if sl, err := s.svc.GetSaveSlot(context.Background(), s.anna.ID, s.game.ID, "renamed"); err != nil || sl.Current.Revision != 2 || sl.Current.DeviceID != hub.WebDeviceID(s.admin.ID) {
		t.Fatalf("%v %+v", err, sl)
	}
	wantStatus(t, s.do("DELETE", rn, nil, opt{token: adm.tok, raw: true}), 400, "")
	wantStatus(t, s.do("DELETE", rn+"?confirm_revision=1", nil, tok), 409, "")
	wantStatus(t, s.do("DELETE", rn+"?confirm_revision=2", nil, tok), 204, "")
	wantStatus(t, s.do("GET", rn, nil, tok), 404, "not_found")
}

func TestAdminSavesConflictResolve(t *testing.T) {
	s, _, adm, _ := newPartBEnv(t)
	d1, d2 := s.device(s.anna.ID, "Anna PC"), s.device(s.anna.ID, "Anna Deck")
	tok := opt{token: adm.tok}
	put := func(d dev, base int, data []byte) int {
		return s.do("PUT", "/api/v1/games/"+s.game.ID+"/saves/default", nil, opt{token: d.tok, data: data, ctype: "application/octet-stream", raw: true,
			header: map[string]string{"X-Uzume-Base-Revision": fmt.Sprint(base), "X-Uzume-Content-SHA256": hexSHA(data), "X-Uzume-Sync-Reason": "final"}}).Code
	}
	if c := put(d1, 0, []byte("one")); c != 200 {
		t.Fatal(c)
	}
	if c := put(d2, 0, []byte("two")); c != 409 { // stale base: secured as a conflict, nothing overwritten
		t.Fatal(c)
	}
	base := "/api/v1/admin/users/" + s.anna.ID + "/games/" + s.game.ID + "/saves/default"
	sl := decode[slotT](t, s.do("GET", base, nil, tok))
	if len(sl.OpenConflicts) != 1 || sl.Current.Revision != 1 {
		t.Fatalf("%+v", sl)
	}
	res := base + "/conflicts/" + sl.OpenConflicts[0].ID + "/resolve"
	wantStatus(t, s.do("POST", res, map[string]any{"resolution": "use_hub", "expected_revision": 5}, tok), 409, "")
	wantStatus(t, s.do("POST", base+"/conflicts/nope/resolve", map[string]any{"resolution": "use_hub", "expected_revision": 1}, tok), 404, "")
	sl = decode[slotT](t, s.do("POST", res, map[string]any{"resolution": "use_local", "expected_revision": 1}, tok))
	if len(sl.OpenConflicts) != 0 || sl.Current.Revision != 2 {
		t.Fatalf("%+v", sl)
	}
}

type adminRunT struct {
	Archived bool
	Run      runResp
	Boards   []struct {
		PluginID string `json:"plugin_id"`
	}
}

func TestAdminRunsAPI(t *testing.T) {
	r := newRunEnv(t)
	adm, anna, bob := r.device(r.admin.ID, "Admin PC"), r.device(r.anna.ID, "Anna PC"), r.device(r.bob.ID, "Bob PC")
	tok := opt{token: adm.tok}
	run := r.create(anna, "Duo", r.bob.ID)
	id := run.RunID
	base := "/api/v1/admin/runs/" + id
	_ = bob

	// List and detail, archived runs included.
	list := decode[struct{ Runs []adminRunT }](t, r.do("GET", "/api/v1/admin/runs", nil, tok)).Runs
	if len(list) != 1 || list[0].Run.RunID != id || list[0].Archived {
		t.Fatalf("%+v", list)
	}
	d := decode[adminRunT](t, r.do("GET", base, nil, tok))
	if d.Run.Status != "starting" || len(d.Run.Participants) != 2 {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, r.do("GET", "/api/v1/admin/runs/"+uuid.NewString(), nil, tok), 404, "not_found")

	// Start now without the player who has not joined (bob stays invited).
	wantStatus(t, r.do("POST", base+"/pause", nil, tok), 409, "conflict")
	d = decode[adminRunT](t, r.do("POST", base+"/start", map[string]any{"without_missing": true}, tok))
	if d.Run.Status != "active" || d.Run.StatusBy != r.admin.ID || len(d.Run.Participants) != 1 {
		t.Fatalf("%+v", d)
	}
	if d = decode[adminRunT](t, r.do("POST", base+"/pause", nil, tok)); d.Run.Status != "paused" {
		t.Fatalf("%+v", d)
	}
	if d = decode[adminRunT](t, r.do("POST", base+"/resume", nil, tok)); d.Run.Status != "active" {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, r.do("POST", base+"/archive", nil, tok), 409, "conflict") // only a finished run
	if d = decode[adminRunT](t, r.do("POST", base+"/finish", nil, tok)); d.Run.Status != "finished" {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, r.do("POST", base+"/finish", nil, tok), 409, "conflict")

	// Export: valid JSON with the events, as an attachment.
	rec := r.do("GET", base+"/export", nil, opt{token: adm.tok, raw: true})
	wantStatus(t, rec, 200, "")
	if cd := rec.Header().Get("Content-Disposition"); !strings.Contains(cd, "run-"+id+"-events.json") {
		t.Fatal(cd)
	}
	var ex struct {
		RunID  string `json:"run_id"`
		Events []json.RawMessage
		Error  string
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &ex); err != nil || ex.RunID != id || len(ex.Events) == 0 || ex.Error != "" {
		t.Fatalf("%v %s", err, rec.Body)
	}
	wantStatus(t, r.do("GET", "/api/v1/admin/runs/"+uuid.NewString()+"/export", nil, opt{token: adm.tok, raw: true}), 404, "not_found")

	// Archive hides it from the app and the member list, not from the admin list.
	if d = decode[adminRunT](t, r.do("POST", base+"/archive", nil, tok)); !d.Archived {
		t.Fatalf("%+v", d)
	}
	if l := decode[struct{ Runs []adminRunT }](t, r.do("GET", "/api/v1/admin/runs", nil, tok)).Runs; len(l) != 1 || !l[0].Archived {
		t.Fatalf("%+v", l)
	}
	if d = decode[adminRunT](t, r.do("POST", base+"/unarchive", nil, tok)); d.Archived {
		t.Fatalf("%+v", d)
	}

	// Delete.
	wantStatus(t, r.do("DELETE", base, nil, tok), 204, "")
	wantStatus(t, r.do("GET", base, nil, tok), 404, "not_found")
	wantStatus(t, r.do("DELETE", base, nil, tok), 404, "not_found")
}

func TestAdminRunInvitesAPI(t *testing.T) {
	r := newRunEnv(t)
	adm, anna := r.device(r.admin.ID, "Admin PC"), r.device(r.anna.ID, "Anna PC")
	tok := opt{token: adm.tok}
	run := r.create(anna, "Trio", r.bob.ID, r.carol.ID)
	base := "/api/v1/admin/runs/" + run.RunID
	wantStatus(t, r.do("POST", base+"/invites/"+r.bob.ID+"/resend", nil, tok), 204, "")
	wantStatus(t, r.do("POST", base+"/invites/"+r.admin.ID+"/resend", nil, tok), 404, "") // not invited
	d := decode[adminRunT](t, r.do("POST", base+"/resend", nil, tok))
	if len(d.Run.Participants) != 3 {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, r.do("DELETE", base+"/invites/"+r.bob.ID, nil, tok), 204, "")
	if d = decode[adminRunT](t, r.do("GET", base, nil, tok)); len(d.Run.Participants) != 2 {
		t.Fatalf("%+v", d)
	}
	wantStatus(t, r.do("DELETE", base+"/invites/"+r.bob.ID, nil, tok), 404, "")
}

func TestAdminSettingsAPI(t *testing.T) {
	s, h, adm, _ := newPartBEnv(t)
	tok := opt{token: adm.tok}
	s.confirm(adm.tok)
	type setting struct {
		Key, Label, Value, Source string
		RunningValue              string `json:"running_value"`
		DefaultValue              string `json:"default_value"`
		RestartPending            bool   `json:"restart_pending"`
		NeedsRestart              bool   `json:"needs_restart"`
	}
	type network struct {
		Settings       []setting
		PendingRestart []string `json:"pending_restart"`
		CanRestart     bool     `json:"can_restart"`
		ExternalTurn   struct {
			Source  string
			Servers []struct {
				Name      string
				SecretSet bool `json:"secret_set"`
			}
		} `json:"external_turn"`
	}
	type settings struct {
		HubName          string `json:"hub_name"`
		AllowUserUploads bool   `json:"allow_user_uploads"`
		Network          network
		Security         struct {
			TLS          bool `json:"tls"`
			Fingerprint  string
			CanRenewCert bool `json:"can_renew_cert"`
		}
		Updates struct {
			CurrentVersion string `json:"current_version"`
			Channel        string
			Auto           bool
			ChannelIsSet   bool `json:"channel_is_set"`
		}
	}
	get := func() settings {
		t.Helper()
		rec := s.do("GET", "/api/v1/admin/settings", nil, tok)
		wantStatus(t, rec, 200, "")
		return decode[settings](t, rec)
	}
	st := get()
	if st.HubName == "" || st.AllowUserUploads || !st.Security.TLS || st.Security.Fingerprint != "AA:BB" || !st.Security.CanRenewCert || len(st.Network.Settings) != len(config.NetKeys) {
		t.Fatalf("%+v", st)
	}

	// General: name and uploads; a bad value changes nothing.
	wantStatus(t, s.do("PATCH", "/api/v1/admin/settings", map[string]any{"name": "Living room", "allow_user_uploads": true}, tok), 200, "")
	if st = get(); st.HubName != "Living room" || !st.AllowUserUploads {
		t.Fatalf("%+v", st)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/settings", map[string]any{"name": "Kept", "update_channel": "nightly"}, opt{token: adm.tok, raw: true}), 400, "bad_request")
	wantStatus(t, s.do("PATCH", "/api/v1/admin/settings", map[string]any{"name": " "}, tok), 400, "bad_request")
	if st = get(); st.HubName != "Living room" {
		t.Fatalf("%+v", st)
	}
	// Updates: channel and automatic install.
	wantStatus(t, s.do("PATCH", "/api/v1/admin/settings", map[string]any{"update_channel": "stable", "update_auto": true}, tok), 200, "")
	if st = get(); st.Updates.Channel != "stable" || !st.Updates.Auto || !st.Updates.ChannelIsSet {
		t.Fatalf("%+v", st.Updates)
	}
	wantStatus(t, s.do("PATCH", "/api/v1/admin/settings", map[string]any{"update_auto": false}, tok), 200, "")
	if st = get(); st.Updates.Channel != "stable" || st.Updates.Auto {
		t.Fatalf("%+v", st.Updates)
	}

	// Network: save, restart pending, reset; a refused value is a 400 with the reason.
	rec := s.do("PUT", "/api/v1/admin/settings/network/listen_port", map[string]any{"value": "9443"}, tok)
	wantStatus(t, rec, 200, "")
	nw := decode[network](t, rec)
	var lp setting
	for _, x := range nw.Settings {
		if x.Key == config.NetListenPort {
			lp = x
		}
	}
	if lp.Source != "web" && lp.Source != "env" || !lp.RestartPending || len(nw.PendingRestart) != 1 || h.stored[config.NetListenPort] != "9443" {
		t.Fatalf("%+v %+v", lp, nw.PendingRestart)
	}
	rec = s.do("PUT", "/api/v1/admin/settings/network/listen_port", map[string]any{"value": "80"}, tok)
	wantStatus(t, rec, 400, "bad_request")
	if !strings.Contains(rec.Body.String(), "Port 80 is not allowed") {
		t.Fatal(rec.Body)
	}
	wantStatus(t, s.do("PUT", "/api/v1/admin/settings/network/save_keep_recent", map[string]any{"value": "5"}, tok), 200, "")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/settings/network/listen_port", nil, tok), 200, "")
	if _, ok := h.stored[config.NetListenPort]; ok {
		t.Fatal("not reset")
	}
	wantStatus(t, s.do("PUT", "/api/v1/admin/settings/network/nope", map[string]any{"value": "1"}, opt{token: adm.tok, raw: true}), 404, "not_found")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/settings/network/nope", nil, opt{token: adm.tok, raw: true}), 404, "not_found")

	// Restart: the answer first, then the restart hook.
	rec = s.do("POST", "/api/v1/admin/settings/network/restart", nil, tok)
	wantStatus(t, rec, 202, "")
	rr := decode[struct {
		URL         string `json:"url"`
		PortChanged bool   `json:"port_changed"`
	}](t, rec)
	if rr.URL != "https://hub.example:9443" || !rr.PortChanged {
		t.Fatalf("%+v", rr)
	}
	select {
	case <-h.restarted:
	case <-time.After(time.Second):
		t.Fatal("no restart")
	}

	// Certificate.
	rec = s.do("POST", "/api/v1/admin/security/renew-cert", nil, tok)
	wantStatus(t, rec, 200, "")
	if c := decode[struct{ Fingerprint string }](t, rec); c.Fingerprint != "CC:DD" || h.renewed != 1 {
		t.Fatalf("%+v", c)
	}

	// Updates: a check starts at once; install without a package or an update is a 400.
	wantStatus(t, s.do("POST", "/api/v1/admin/updates/check", nil, tok), 202, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/updates/install", nil, tok), 400, "bad_request")
}

func TestAdminExternalTurnAPI(t *testing.T) {
	s, _, adm, _ := newPartBEnv(t)
	tok := opt{token: adm.tok}
	s.confirm(adm.tok)
	type ext struct {
		Source  string
		Active  bool
		Servers []struct {
			Name      string
			Mode      string
			URLs      []string `json:"urls"`
			SecretSet bool     `json:"secret_set"`
			Username  *string
		}
	}
	in := map[string]any{"name": "relay", "mode": "secret", "urls": []string{"turn:turn.example.org:3478"}, "secret": "placeholder-secret"}
	rec := s.do("PUT", "/api/v1/admin/settings/external-turn", in, tok)
	wantStatus(t, rec, 200, "")
	if strings.Contains(rec.Body.String(), "placeholder-secret") {
		t.Fatal("secret returned")
	}
	e := decode[ext](t, rec)
	if e.Source != "web" || len(e.Servers) != 1 || e.Servers[0].Name != "relay" || !e.Servers[0].SecretSet {
		t.Fatalf("%+v", e)
	}
	// The secret never shows in the settings either.
	rec = s.do("GET", "/api/v1/admin/settings", nil, tok)
	if strings.Contains(rec.Body.String(), "placeholder-secret") {
		t.Fatal("secret in settings")
	}
	// Edit (empty secret keeps it), then refuse a bad one.
	in2 := map[string]any{"original": "relay", "name": "relay2", "mode": "secret", "urls": []string{"turn:turn.example.org:3478"}}
	if e = decode[ext](t, s.do("PUT", "/api/v1/admin/settings/external-turn", in2, tok)); len(e.Servers) != 1 || e.Servers[0].Name != "relay2" || !e.Servers[0].SecretSet {
		t.Fatalf("%+v", e)
	}
	wantStatus(t, s.do("PUT", "/api/v1/admin/settings/external-turn", map[string]any{"name": "bad", "mode": "static", "urls": []string{"http://x"}}, tok), 400, "bad_request")
	if e = decode[ext](t, s.do("DELETE", "/api/v1/admin/settings/external-turn?name=relay2", nil, tok)); len(e.Servers) != 0 {
		t.Fatalf("%+v", e)
	}
	e = decode[ext](t, s.do("POST", "/api/v1/admin/settings/external-turn/reset", nil, tok))
	if e.Source != "flags" {
		t.Fatalf("%+v", e)
	}
}

func TestAdminDiagnosticsBundleAPI(t *testing.T) {
	s, _, adm, _ := newPartBEnv(t)
	rec := s.do("POST", "/api/v1/admin/diagnostics/bundle", nil, opt{token: adm.tok, raw: true})
	wantStatus(t, rec, 200, "")
	if ct := rec.Header().Get("Content-Type"); ct != "application/zip" || !strings.Contains(rec.Header().Get("Content-Disposition"), "attachment") {
		t.Fatalf("%v", rec.Header())
	}
	zr, err := zip.NewReader(bytes.NewReader(rec.Body.Bytes()), int64(rec.Body.Len()))
	if err != nil || len(zr.File) == 0 {
		t.Fatalf("%v", err)
	}
}
