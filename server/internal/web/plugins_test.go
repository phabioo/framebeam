package web

import (
	"bytes"
	"context"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/hub/hubtest"
	"github.com/phabioo/uzume/server/internal/pluginpkg"
)

func pluginEnv(t *testing.T, indexURL string) (*env, *client, string) {
	t.Helper()
	e := newEnvOpts(t, true, nil, func(o *hub.Options) {
		o.CoreTrustKeys = hubtest.PluginKeys()
		o.PluginIndexURL = indexURL
	})
	c := e.client()
	return e, c, c.login()
}

var stageRe = regexp.MustCompile(`name="stage" value="([0-9a-f]{32})"`)

func uploadPlugin(c *client, tok string, data []byte) string {
	rec := c.multipartPost("/plugins/upload", map[string]string{"_csrf": tok}, "x.fbplugin", data)
	if m := stageRe.FindStringSubmatch(rec.Body.String()); m != nil {
		return m[1]
	}
	return ""
}

func TestPluginsPageAdminOnly(t *testing.T) {
	e := newEnv(t, true, nil)
	anon := e.client()
	for _, p := range []string{"/plugins", "/plugins/net.example.a/1.0.0", "/plugins/net.example.a/1.0.0/download", "/plugins/net.example.a/1.0.0/remove"} {
		if rec := anon.get(p, nil); rec.Code != http.StatusSeeOther || location(rec) != "/login" {
			t.Fatalf("%s: %d -> %q", p, rec.Code, location(rec))
		}
	}
	for _, p := range []string{"/plugins/net.example.a/1.0.0/remove", "/plugins/offer", "/plugins/discard", "/plugins/index/refresh", "/plugins/index/install"} {
		if rec := anon.postForm(p, url.Values{}, nil); rec.Code != http.StatusSeeOther || location(rec) != "/login" {
			t.Fatalf("POST %s: %d -> %q", p, rec.Code, location(rec))
		}
	}
	body, ct := multipartBody(map[string]string{"_csrf": "x"}, "a.fbplugin", hubtest.PluginZip(t, "net.example.a", "1.0.0", nil, nil))
	if rec := anon.do("POST", "/plugins/upload", body, map[string]string{"Content-Type": ct}); rec.Code != http.StatusSeeOther || location(rec) != "/login" {
		t.Fatalf("upload anonymous: %d", rec.Code)
	}
	// The nav has the entry.
	c := e.client()
	c.login()
	contains(t, c.get("/library", nil), `href="/plugins"`, ">Plugins<")
}

func TestPluginUploadFlowWeb(t *testing.T) {
	e, c, tok := pluginEnv(t, "")
	contains(t, c.get("/plugins", nil), "No plugins are offered yet", "Upload plugin", "No plugin index is configured", "import-plugins")

	// CSRF: without a token nothing is staged.
	body, ct := multipartBody(map[string]string{}, "a.fbplugin", hubtest.PluginZip(t, "net.example.notes", "0.2.0", nil, nil))
	status(t, c.do("POST", "/plugins/upload", body, map[string]string{"Content-Type": ct}), http.StatusForbidden)

	data := hubtest.PluginZip(t, "net.example.notes", "0.2.0", []string{"ui", "run.state", "memory.read"}, nil)
	rec := c.multipartPost("/plugins/upload", map[string]string{"_csrf": tok}, "notes.fbplugin", data)
	status(t, rec, 200)
	contains(t, rec, "Offer Plugin net.example.notes 0.2.0 to apps?", "Upload · not signed", "Server upload · unverified", "It asks to:",
		"Show panels in game", "Share run progress with your server", "Read game memory", "Tick every permission to confirm you reviewed it.", "Offer to apps", `action="/plugins/offer"`)
	notContains(t, rec, "<no value>")
	m := stageRe.FindStringSubmatch(rec.Body.String())
	if m == nil {
		t.Fatal("no stage id")
	}
	stage := m[1]
	if ps, _ := e.svc.ListPlugins(bg); len(ps) != 0 {
		t.Fatal("offered before confirmation")
	}

	// Server-side tick check: every combination that misses one permission is refused.
	for _, perms := range [][]string{nil, {"ui"}, {"ui", "run.state"}, {"ui", "run.state", "tool.exec"}} {
		v := url.Values{"_csrf": {tok}, "stage": {stage}}
		for _, p := range perms {
			v.Add("perm", p)
		}
		rec = c.postForm("/plugins/offer", v, nil)
		status(t, rec, http.StatusBadRequest)
		contains(t, rec, "Tick every permission to confirm you reviewed it.", `name="stage" value="`+stage+`"`)
	}
	if ps, _ := e.svc.ListPlugins(bg); len(ps) != 0 {
		t.Fatal("offered without all ticks")
	}
	// CSRF on the confirmation.
	status(t, c.postForm("/plugins/offer", url.Values{"stage": {stage}, "perm": {"ui", "run.state", "memory.read"}}, nil), http.StatusForbidden)

	rec = c.postForm("/plugins/offer", url.Values{"_csrf": {tok}, "stage": {stage}, "perm": {"ui", "run.state", "memory.read"}}, nil)
	status(t, rec, http.StatusSeeOther)
	if location(rec) != "/plugins?ok=pluginoffered" {
		t.Fatal(location(rec))
	}
	ps, _ := e.svc.ListPlugins(bg)
	if len(ps) != 1 || ps[0].Origin != "unverified" || ps[0].UploadedByName != "admin" {
		t.Fatalf("%+v", ps)
	}
	rec = c.get("/plugins?ok=pluginoffered", nil)
	contains(t, rec, "Plugin offered to apps.", "Plugin net.example.notes", "0.2.0", "Server upload · unverified", `href="/plugins/net.example.notes/0.2.0"`)

	// Detail, download, remove.
	rec = c.get("/plugins/net.example.notes/0.2.0", nil)
	contains(t, rec, "Not signed by Uzume", "Permissions · confirmed by admin", "Read game memory", "uploaded by admin", pluginpkg.SHA256Hex(data),
		`href="/plugins/net.example.notes/0.2.0/download"`, "Remove from server", "Apps keep their copy until they uninstall it.")
	rec = c.get("/plugins/net.example.notes/0.2.0/download", nil)
	status(t, rec, 200)
	if !bytes.Equal(rec.Body.Bytes(), data) || !strings.Contains(rec.Header().Get("Content-Disposition"), "net.example.notes-0.2.0.fbplugin") {
		t.Fatalf("download %v", rec.Header())
	}
	status(t, c.get("/plugins/net.example.notes/9.9.9/download", nil), 404)
	rec = c.get("/plugins/net.example.notes/9.9.9", nil)
	status(t, rec, 404)
	contains(t, rec, "Page not found", "Back to Library")
	contains(t, c.get("/plugins/net.example.notes/0.2.0/remove", nil), "Remove “Plugin net.example.notes 0.2.0” from the server?")

}

func TestPluginUploadRulesWeb(t *testing.T) {
	e, c, tok := pluginEnv(t, "")
	// Not a plugin, empty submit, same id+version with another file.
	rec := c.multipartPost("/plugins/upload", map[string]string{"_csrf": tok}, "a.fbplugin", []byte("nope"))
	status(t, rec, http.StatusBadRequest)
	contains(t, rec, "This is not a valid plugin file")
	a := hubtest.PluginZip(t, "net.example.same", "1.0.0", nil, nil)
	b := hubtest.PluginZip(t, "net.example.same", "1.0.0", nil, map[string]string{"x.txt": "y"})
	stageA := uploadPlugin(c, tok, a)
	status(t, c.postForm("/plugins/offer", url.Values{"_csrf": {tok}, "stage": {stageA}}, nil), http.StatusSeeOther)
	rec = c.multipartPost("/plugins/upload", map[string]string{"_csrf": tok}, "b.fbplugin", b)
	status(t, rec, 200)
	contains(t, rec, "A different file with this version is already offered")
	notContains(t, rec, "Offer to apps</button>")
	stageB := stageRe.FindStringSubmatch(rec.Body.String())[1]
	rec = c.postForm("/plugins/offer", url.Values{"_csrf": {tok}, "stage": {stageB}}, nil)
	status(t, rec, http.StatusConflict)
	contains(t, rec, "A different file with this version is already offered")
	if ps, _ := e.svc.ListPlugins(bg); len(ps) != 1 || ps[0].SHA256 != pluginpkg.SHA256Hex(a) {
		t.Fatalf("%+v", ps)
	}
	// Discard removes the staged file; offering it afterwards fails.
	status(t, c.postForm("/plugins/discard", url.Values{"_csrf": {tok}, "stage": {stageB}}, nil), http.StatusSeeOther)
	rec = c.postForm("/plugins/offer", url.Values{"_csrf": {tok}, "stage": {stageB}}, nil)
	status(t, rec, http.StatusBadRequest)
	contains(t, rec, "no longer available")
	// Too large.
	big := make([]byte, pluginpkg.MaxCompressedBytes+1)
	rec = c.multipartPost("/plugins/upload", map[string]string{"_csrf": tok}, "big.fbplugin", big)
	status(t, rec, http.StatusRequestEntityTooLarge)
	contains(t, rec, "The file is too large (maximum 32 MiB).")
}

func TestPluginSignedUploadWeb(t *testing.T) {
	e, c, tok := pluginEnv(t, "")
	signed := hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.signed", "1.0.0", []string{"ui"}, nil))
	rec := c.multipartPost("/plugins/upload", map[string]string{"_csrf": tok}, "s.fbplugin", signed)
	contains(t, rec, "Upload · signed by Uzume", "Signed by Uzume. Apps see it as “Official”.")
	stage := stageRe.FindStringSubmatch(rec.Body.String())[1]
	status(t, c.postForm("/plugins/offer", url.Values{"_csrf": {tok}, "stage": {stage}, "perm": {"ui"}}, nil), http.StatusSeeOther)
	ps, _ := e.svc.ListPlugins(bg)
	if len(ps) != 1 || ps[0].Origin != "official" {
		t.Fatalf("%+v", ps)
	}
	rec = c.get("/plugins/net.example.signed/1.0.0", nil)
	contains(t, rec, "✓ Official", "Permissions")
	notContains(t, rec, "Not signed by Uzume")
}

func TestPluginRemoveWeb(t *testing.T) {
	e, c, tok := pluginEnv(t, "")
	// 1.0.0 is pinned by a run, so it is kept when 1.1.0 supersedes it.
	e.svc.SetPluginInUseCheck(func(_ context.Context, _, v string) (bool, error) { return v == "1.0.0", nil })
	for _, v := range []string{"1.0.0", "1.1.0"} {
		st, err := e.svc.StagePluginUpload(bg, bytes.NewReader(hubtest.PluginZip(t, "net.example.rm", v, nil, nil)))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := e.svc.OfferStagedPlugin(bg, st.StageID, nil, ""); err != nil {
			t.Fatal(err)
		}
	}
	// Newest version first in the table.
	body := c.get("/plugins", nil).Body.String()
	if !strings.Contains(body, "1.1.0") || strings.Index(body, "1.1.0") > strings.Index(body, "1.0.0") {
		t.Fatal("version order")
	}
	if strings.Count(body, "Older version · kept while a run uses it") != 1 {
		t.Fatal("older version help line")
	}
	rec := c.postForm("/plugins/net.example.rm/1.0.0/remove", url.Values{"_csrf": {tok}}, nil)
	status(t, rec, http.StatusConflict)
	contains(t, rec, "still going uses this plugin version")
	rec = c.postForm("/plugins/net.example.rm/1.1.0/remove", url.Values{"_csrf": {tok}}, nil)
	status(t, rec, http.StatusSeeOther)
	if location(rec) != "/plugins?ok=pluginremoved" {
		t.Fatal(location(rec))
	}
	ps, _ := e.svc.ListPlugins(bg)
	if len(ps) != 1 || ps[0].Version != "1.0.0" {
		t.Fatalf("%+v", ps)
	}
	if rec = c.postForm("/plugins/net.example.rm/1.1.0/remove", url.Values{"_csrf": {tok}}, nil); location(rec) != "/plugins?err=noplugin" {
		t.Fatalf("again: %d %q", rec.Code, location(rec))
	}
	status(t, c.postForm("/plugins/net.example.rm/1.0.0/remove", url.Values{}, nil), http.StatusForbidden)
}

func TestPluginCatalogWeb(t *testing.T) {
	dir := t.TempDir()
	v1 := hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.cat", "1.0.0", []string{"ui"}, nil))
	idxURL := hubtest.PluginIndex(t, dir, false, v1)
	e, c, tok := pluginEnv(t, idxURL)

	rec := c.get("/plugins", nil)
	contains(t, rec, "Official catalog", "not checked yet", "Check now")
	rec = c.postForm("/plugins/index/refresh", url.Values{"_csrf": {tok}}, nil)
	if location(rec) != "/plugins?ok=pluginindex" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	rec = c.get("/plugins?ok=pluginindex", nil)
	contains(t, rec, "Plugin catalog checked.", "Plugin net.example.cat", "✓ Official", "signed · checked", "1 plugin", `action="/plugins/index/install"`, ">Install</button>")
	rec = c.postForm("/plugins/index/install", url.Values{"_csrf": {tok}, "id": {"net.example.cat"}, "version": {"1.0.0"}}, nil)
	if location(rec) != "/plugins/net.example.cat/1.0.0?ok=plugininstalled" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	rec = c.get(location(rec), nil)
	contains(t, rec, "Plugin installed.", "from the plugin index", ">Installed</span>")
	notContains(t, rec, ">Install</button>")

	// A newer version in the index turns the button into Update.
	v2 := hubtest.SignPlugin(t, hubtest.PluginZip(t, "net.example.cat", "1.1.0", []string{"ui"}, nil))
	hubtest.PluginIndex(t, dir, false, v1, v2)
	c.postForm("/plugins/index/refresh", url.Values{"_csrf": {tok}}, nil)
	contains(t, c.get("/plugins", nil), ">Update</button>")

	// A broken index shows its reason and keeps the catalog.
	os.Remove(filepath.Join(dir, "plugin-index.json.sig"))
	rec = c.postForm("/plugins/index/refresh", url.Values{"_csrf": {tok}}, nil)
	if location(rec) != "/plugins?err=indexfailed" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	rec = c.get("/plugins?err=indexfailed", nil)
	contains(t, rec, "Could not load the catalog", "The plugin index could not be checked", "Retry", ">Update</button>")

	// Unknown version / missing index.
	rec = c.postForm("/plugins/index/install", url.Values{"_csrf": {tok}, "id": {"net.example.cat"}, "version": {"9.9.9"}}, nil)
	if location(rec) != "/plugins?err=noplugin" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	_ = e
	e2, c2, tok2 := pluginEnv(t, "")
	_ = e2
	if rec = c2.postForm("/plugins/index/refresh", url.Values{"_csrf": {tok2}}, nil); location(rec) != "/plugins?err=noindex" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
}

func TestMonogram(t *testing.T) {
	for in, want := range map[string]string{
		"SoulLink & Nuzlocke": "SN", "SoulLink Core": "SC", "Cheats": "CH", "& Solo": "SO", "Plugin net.example.a": "PN", "7 Days": "7D", "": "?",
	} {
		if got := monogram(in); got != want {
			t.Errorf("monogram(%q) = %q, want %q", in, got, want)
		}
	}
	if rangeText("^0.3") != "0.3 or newer" || rangeText(">=1.2") != "1.2 or newer" || rangeText("*") != "any version" {
		t.Fatal("rangeText")
	}
}

func TestPluginsListNeedsUsedByAndOrigin(t *testing.T) {
	_, c, tok := pluginEnv(t, "")
	base := hubtest.PluginZipWith(t, "net.example.base", "1.0.0", nil, map[string]any{"name": "Core Base", "requires": map[string]string{}}, nil)
	user := hubtest.PluginZipWith(t, "net.example.user", "0.3.2", []string{"ui"}, map[string]any{"name": "Soul & Link"}, nil)
	for _, z := range [][]byte{base, user} {
		stage := uploadPlugin(c, tok, z)
		if stage == "" {
			t.Fatal("no stage")
		}
		rec := c.postForm("/plugins/offer", url.Values{"_csrf": {tok}, "stage": {stage}, "perm": {"ui"}}, nil)
		if rec.Code != http.StatusSeeOther {
			t.Fatalf("offer %d: %.300s", rec.Code, rec.Body.String())
		}
	}
	rec := c.get("/plugins", nil)
	contains(t, rec, ">SL<", "Used by Soul &amp; Link", "Needs Core Base", `class="pill sm amber"`)
	notContains(t, rec, `pill sm warn`, "(s)")
	rec = c.get("/plugins/net.example.user/0.3.2", nil)
	contains(t, rec, "Core Base 1.0 or newer", "✓ on this server", "Remove from server…", "Copy")
	rec = c.get("/plugins/net.example.base/1.0.0", nil)
	contains(t, rec, "Used by", "Soul &amp; Link")
}
