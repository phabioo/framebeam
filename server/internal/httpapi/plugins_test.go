package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"reflect"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
	"github.com/phabioo/uzume/server/internal/pluginpkg"
)

func TestPluginsAPI(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.CoreTrustKeys = hubtest.PluginKeys() })
	ctx := context.Background()
	d := s.device(s.anna.ID, "Anna PC")

	// No token: 401. Empty list: [] not null.
	wantStatus(t, s.do("GET", "/api/v1/plugins", nil, opt{}), 401, "unauthorized")
	wantStatus(t, s.do("GET", "/api/v1/plugins/net.example.tracker/1.2.0/file", nil, opt{}), 401, "unauthorized")
	rec := s.do("GET", "/api/v1/plugins", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	if !strings.Contains(rec.Body.String(), `"plugins":[]`) {
		t.Fatal(rec.Body.String())
	}

	signed := hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.tracker", "1.2.0", []string{"memory.read", "ui"}, nil))
	unsigned := hubtest.PluginZip(t, "net.example.notes", "0.2.0", nil, nil)
	for _, data := range [][]byte{signed, unsigned} {
		st, err := s.svc.StagePluginUpload(ctx, bytes.NewReader(data))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := s.svc.OfferStagedPlugin(ctx, st.StageID, st.Permissions, s.admin.ID); err != nil {
			t.Fatal(err)
		}
	}

	type pluginT struct {
		PluginID    string `json:"plugin_id"`
		Version     string
		Name        string
		Description string
		API         int `json:"api"`
		Origin      string
		Sha256      string
		Size        int64
		Permissions []string
		Requires    map[string]string
		Provides    []string
		Games       []struct {
			System string
			Codes  []string
		}
		CreatedAt string `json:"created_at"`
	}
	rec = s.do("GET", "/api/v1/plugins", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	list := decode[struct{ Plugins []pluginT }](t, rec).Plugins
	if len(list) != 2 || list[0].PluginID != "net.example.notes" || list[1].PluginID != "net.example.tracker" {
		t.Fatalf("%+v", list)
	}
	n, tr := list[0], list[1]
	if n.Origin != "unverified" || n.Permissions == nil || len(n.Permissions) != 0 || n.Sha256 != pluginpkg.SHA256Hex(unsigned) || n.Size != int64(len(unsigned)) ||
		n.API != 1 || n.Version != "0.2.0" || n.CreatedAt == "" || n.Description != "Test plugin net.example.notes" {
		t.Fatalf("%+v", n)
	}
	if tr.Origin != "official" || strings.Join(tr.Permissions, ",") != "memory.read,ui" || tr.Requires["net.example.base"] != "^1.0" ||
		len(tr.Provides) != 1 || tr.Provides[0] != "test.iface" || len(tr.Games) != 1 || tr.Games[0].System != "nds" || tr.Games[0].Codes[0] != "ABC*" {
		t.Fatalf("%+v", tr)
	}

	// Download: bytes, content type, ETag, Content-Length; If-None-Match answers 304.
	url := "/api/v1/plugins/net.example.tracker/1.2.0/file"
	rec = s.do("GET", url, nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	etag := `"` + pluginpkg.SHA256Hex(signed) + `"`
	if !bytes.Equal(rec.Body.Bytes(), signed) || rec.Header().Get("ETag") != etag || rec.Header().Get("Content-Type") != "application/zip" ||
		rec.Header().Get("Content-Length") == "" || tr.Sha256 != pluginpkg.SHA256Hex(rec.Body.Bytes()) {
		t.Fatalf("headers %v len %d", rec.Header(), rec.Body.Len())
	}
	for _, inm := range []string{etag, "W/" + etag, `"x", ` + etag, "*"} {
		rec = s.do("GET", url, nil, opt{token: d.tok, header: map[string]string{"If-None-Match": inm}})
		if rec.Code != 304 || rec.Body.Len() != 0 || rec.Header().Get("ETag") != etag {
			t.Fatalf("If-None-Match %q: %d %v", inm, rec.Code, rec.Header())
		}
	}
	rec = s.do("GET", url, nil, opt{token: d.tok, header: map[string]string{"If-None-Match": `"other"`}})
	wantStatus(t, rec, 200, "")

	// 404s: unknown id, unknown version, ids that are not plugin ids, a missing file.
	for _, u := range []string{
		"/api/v1/plugins/net.example.nope/1.2.0/file",
		"/api/v1/plugins/net.example.tracker/9.9.9/file",
		"/api/v1/plugins/net.example.tracker/..%2F1.2.0/file",
		"/api/v1/plugins/net.example.tracker/1.2/file",
	} {
		wantStatus(t, s.do("GET", u, nil, opt{token: d.tok}), 404, "not_found")
	}
	if err := s.svc.RemovePlugin(ctx, "net.example.tracker", "1.2.0"); err != nil {
		t.Fatal(err)
	}
	wantStatus(t, s.do("GET", url, nil, opt{token: d.tok}), 404, "not_found")
	rec = s.do("GET", "/api/v1/plugins", nil, opt{token: d.tok})
	if list = decode[struct{ Plugins []pluginT }](t, rec).Plugins; len(list) != 1 {
		t.Fatalf("%+v", list)
	}
}

func TestPluginsAPIRunSettings(t *testing.T) {
	s := newSessEnv(t, func(o *hub.Options) { o.CoreTrustKeys = hubtest.PluginKeys() })
	ctx := context.Background()
	d := s.device(s.anna.ID, "Anna PC")
	rs := []map[string]any{
		{"key": "mode", "type": "enum", "label": "run.mode", "hint": "run.mode_hint", "default": "soullink",
			"options": []map[string]any{{"value": "nuzlocke", "label": "run.mode.nuzlocke"}, {"value": "soullink", "label": "run.mode.soullink"}}},
		{"key": "rules.first_encounter_only", "type": "boolean", "label": "rules.first", "default": true, "ignored_field": 1},
	}
	data := hubtest.PluginZipWith(t, "net.example.soul", "1.0.0", nil, map[string]any{"run_settings": rs}, map[string]string{
		"i18n/en.json": `{"run":{"mode":"Mode","mode_hint":"Pick one","mode.nuzlocke":"Nuzlocke","mode.soullink":"SoulLink"},"rules.first":"First encounter only","other":"x"}`,
		"i18n/de.json": `{"run.mode":"Modus","rules.first":"Nur erste Begegnung"}`,
	})
	plain := hubtest.PluginZip(t, "net.example.plain", "1.0.0", nil, nil)
	for _, b := range [][]byte{data, plain} {
		st, err := s.svc.StagePluginUpload(ctx, bytes.NewReader(b))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := s.svc.OfferStagedPlugin(ctx, st.StageID, st.Permissions, s.admin.ID); err != nil {
			t.Fatal(err)
		}
	}
	for i := 0; i < 2; i++ { // second pass is served from the in-memory cache
		rec := s.do("GET", "/api/v1/plugins", nil, opt{token: d.tok})
		wantStatus(t, rec, 200, "")
		list := decode[struct{ Plugins []map[string]any }](t, rec).Plugins
		if len(list) != 2 {
			t.Fatalf("%v", list)
		}
		plainE, soul := list[0], list[1]
		if _, ok := plainE["run_settings"]; ok {
			t.Fatalf("plain plugin has run_settings: %v", plainE)
		}
		if _, ok := plainE["run_settings_strings"]; ok {
			t.Fatalf("plain plugin has run_settings_strings: %v", plainE)
		}
		eq := func(name string, got any, want string) {
			t.Helper()
			var w any
			if err := json.Unmarshal([]byte(want), &w); err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(got, w) {
				g, _ := json.Marshal(got)
				t.Fatalf("%s\n got %s\nwant %s", name, g, want)
			}
		}
		eq("run_settings", soul["run_settings"], `[{"key":"mode","type":"enum","label":"run.mode","hint":"run.mode_hint","default":"soullink","options":[{"value":"nuzlocke","label":"run.mode.nuzlocke"},{"value":"soullink","label":"run.mode.soullink"}]},`+
			`{"key":"rules.first_encounter_only","type":"boolean","label":"rules.first","default":true}]`)
		eq("run_settings_strings", soul["run_settings_strings"], `{"de":{"rules.first":"Nur erste Begegnung","run.mode":"Modus"},"en":{"rules.first":"First encounter only","run.mode":"Mode","run.mode.nuzlocke":"Nuzlocke","run.mode.soullink":"SoulLink","run.mode_hint":"Pick one"}}`)
	}

	// An invalid schema is rejected at upload.
	bad := hubtest.PluginZipWith(t, "net.example.bad", "1.0.0", nil, map[string]any{"run_settings": []map[string]any{
		{"key": "a", "type": "boolean", "label": "l", "default": "yes"}}}, nil)
	if _, err := s.svc.StagePluginUpload(ctx, bytes.NewReader(bad)); err == nil || !strings.Contains(err.Error(), "run_settings[0].default") {
		t.Fatalf("err %v", err)
	}
}
