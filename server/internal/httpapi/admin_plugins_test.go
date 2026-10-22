package httpapi

import (
	"context"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
)

func TestAdminPluginsRequireAdmin(t *testing.T) {
	s := newSessEnv(t, nil)
	member := s.device(s.anna.ID, "Annas PC")
	adminAuthzTable(t, s, member, [][2]string{
		{"GET", "/api/v1/admin/plugins"}, {"POST", "/api/v1/admin/plugins/stage"}, {"POST", "/api/v1/admin/plugins/stage/0123456789abcdef0123456789abcdef/offer"},
		{"DELETE", "/api/v1/admin/plugins/stage/0123456789abcdef0123456789abcdef"}, {"DELETE", "/api/v1/admin/plugins/net.example.x/1.0.0"},
		{"POST", "/api/v1/admin/plugin-index/refresh"}, {"POST", "/api/v1/admin/plugin-index/install"},
	})
}

type stageT struct {
	StageID        string `json:"stage_id"`
	PluginID       string `json:"plugin_id"`
	Name           string
	Version        string
	Permissions    []string
	Official       bool
	Signed         bool
	AlreadyOffered bool `json:"already_offered"`
	Conflict       bool
	Sha256         string
	Size           int64
}

func TestAdminPluginsAPI(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.CoreTrustKeys = hubtest.PluginKeys() })
	ctx := context.Background()
	tok := opt{token: s.device(s.admin.ID, "Admin PC").tok}
	stage := func(data []byte) *stageT {
		rec := s.do("POST", "/api/v1/admin/plugins/stage", nil, opt{token: tok.token, data: data, ctype: "application/octet-stream"})
		wantStatus(t, rec, 201, "")
		st := decode[stageT](t, rec)
		return &st
	}

	// Nothing offered yet; a file that is not a plugin is refused.
	l := decode[struct {
		Plugins []struct{ Source string }
		Index   struct{ Configured bool }
	}](t, s.do("GET", "/api/v1/admin/plugins", nil, tok))
	if len(l.Plugins) != 0 || l.Index.Configured {
		t.Fatalf("%+v", l)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/plugins/stage", nil, opt{token: tok.token, data: []byte("not a plugin"), ctype: "application/octet-stream"}), 400, "")

	// Stage: the review names the permissions; nothing is offered before the offer.
	signed := hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.tracker", "1.2.0", []string{"memory.read", "ui"}, nil))
	st := stage(signed)
	if st.StageID == "" || st.PluginID != "net.example.tracker" || st.Version != "1.2.0" || !st.Official || !st.Signed || st.AlreadyOffered || st.Conflict ||
		strings.Join(st.Permissions, ",") != "memory.read,ui" || len(st.Sha256) != 64 || st.Size != int64(len(signed)) {
		t.Fatalf("%+v", st)
	}
	if ps, _ := s.svc.ListPlugins(ctx); len(ps) != 0 {
		t.Fatalf("offered before the offer: %+v", ps)
	}

	// Offer: every permission must be granted; an unknown stage is refused.
	offer := "/api/v1/admin/plugins/stage/" + st.StageID + "/offer"
	wantStatus(t, s.do("POST", offer, map[string]any{"granted_permissions": []string{"ui"}}, tok), 400, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/plugins/stage/00000000000000000000000000000000/offer", map[string]any{"granted_permissions": []string{}}, tok), 400, "")
	rec := s.do("POST", offer, map[string]any{"granted_permissions": []string{"memory.read", "ui"}}, tok)
	wantStatus(t, rec, 201, "")
	offered := decode[struct {
		Info struct {
			PluginID string `json:"plugin_id"`
			Origin   string
		}
		Source string
	}](t, rec)
	if offered.Info.PluginID != "net.example.tracker" || offered.Info.Origin != "official" || offered.Source != "upload" {
		t.Fatalf("%+v", offered)
	}
	if ps, _ := s.svc.ListPlugins(ctx); len(ps) != 1 || ps[0].Version != "1.2.0" || ps[0].UploadedBy != s.admin.ID {
		t.Fatalf("%+v", ps)
	}
	wantStatus(t, s.do("POST", offer, map[string]any{"granted_permissions": []string{"memory.read", "ui"}}, tok), 400, "") // the stage is used up

	// Staging the same file again says so; a different file with the same version is a conflict.
	again := stage(signed)
	if !again.AlreadyOffered || again.Conflict {
		t.Fatalf("%+v", again)
	}
	other := stage(hubtest.PluginZip(t, "net.example.tracker", "1.2.0", nil, map[string]string{"extra.txt": "x"}))
	if !other.Conflict || other.Official {
		t.Fatalf("%+v", other)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/plugins/stage/"+other.StageID+"/offer", map[string]any{"granted_permissions": []string{}}, tok), 409, "")

	// Discard: idempotent, and the stage cannot be offered afterwards.
	disc := "/api/v1/admin/plugins/stage/" + other.StageID
	wantStatus(t, s.do("DELETE", disc, nil, tok), 204, "")
	wantStatus(t, s.do("DELETE", disc, nil, tok), 204, "")
	wantStatus(t, s.do("POST", "/api/v1/admin/plugins/stage/"+other.StageID+"/offer", map[string]any{"granted_permissions": []string{}}, tok), 400, "")

	// The admin list shows the offered version; the member list too.
	l2 := decode[struct{ Plugins []struct{ Source string } }](t, s.do("GET", "/api/v1/admin/plugins", nil, tok))
	if len(l2.Plugins) != 1 || l2.Plugins[0].Source != "upload" {
		t.Fatalf("%+v", l2)
	}

	// Remove.
	wantStatus(t, s.do("DELETE", "/api/v1/admin/plugins/net.example.tracker/9.9.9", nil, tok), 404, "")
	wantStatus(t, s.do("DELETE", "/api/v1/admin/plugins/net.example.tracker/1.2.0", nil, tok), 204, "")
	if ps, _ := s.svc.ListPlugins(ctx); len(ps) != 0 {
		t.Fatalf("%+v", ps)
	}
	wantStatus(t, s.do("DELETE", "/api/v1/admin/plugins/net.example.tracker/1.2.0", nil, tok), 404, "")
}

func TestAdminPluginIndexAPI(t *testing.T) {
	// Without an index: both calls answer 400, the list says "not configured".
	s0 := newSessEnv(t, nil)
	tok0 := opt{token: s0.device(s0.admin.ID, "Admin PC").tok}
	wantStatus(t, s0.do("POST", "/api/v1/admin/plugin-index/refresh", nil, tok0), 400, "")
	wantStatus(t, s0.do("POST", "/api/v1/admin/plugin-index/install", map[string]any{"id": "net.example.idx", "version": "1.0.0"}, tok0), 400, "")

	dir := t.TempDir()
	v1 := hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.idx", "1.0.0", []string{"ui"}, nil))
	url := hubtest.PluginIndex(t, dir, false, v1)
	s := newSessEnv(t, func(o *hub.Options) { o.CoreTrustKeys = hubtest.PluginKeys(); o.PluginIndexURL = url })
	ctx := context.Background()
	tok := opt{token: s.device(s.admin.ID, "Admin PC").tok}
	type indexT struct {
		Configured bool
		LastCheck  *string `json:"last_check"`
		LastError  *string `json:"last_error"`
		Catalog    []struct {
			ID               string
			State            string
			Version          string
			InstalledVersion *string `json:"installed_version"`
		}
	}

	rec := s.do("POST", "/api/v1/admin/plugin-index/refresh", nil, tok)
	wantStatus(t, rec, 200, "")
	idx := decode[indexT](t, rec)
	if !idx.Configured || idx.LastCheck == nil || idx.LastError != nil || len(idx.Catalog) != 1 || idx.Catalog[0].State != "install" {
		t.Fatalf("%+v", idx)
	}
	wantStatus(t, s.do("POST", "/api/v1/admin/plugin-index/install", map[string]any{"id": "net.example.idx", "version": "9.9.9"}, tok), 404, "")
	rec = s.do("POST", "/api/v1/admin/plugin-index/install", map[string]any{"id": "net.example.idx", "version": "1.0.0"}, tok)
	wantStatus(t, rec, 201, "")
	if src := decode[struct{ Source string }](t, rec).Source; src != "index" {
		t.Fatal(src)
	}
	if ps, _ := s.svc.ListPlugins(ctx); len(ps) != 1 || ps[0].Source != hub.PluginSourceIndex {
		t.Fatalf("%+v", ps)
	}
	l := decode[struct{ Index indexT }](t, s.do("GET", "/api/v1/admin/plugins", nil, tok))
	if len(l.Index.Catalog) != 1 || l.Index.Catalog[0].State != "installed" || l.Index.Catalog[0].InstalledVersion == nil || *l.Index.Catalog[0].InstalledVersion != "1.0.0" {
		t.Fatalf("%+v", l.Index)
	}
}
