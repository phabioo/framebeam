package web

import (
	"bytes"
	"context"
	"encoding/json"
	"html"
	"net/url"
	"os"
	"regexp"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

type oneOffered struct{}

func (oneOffered) OfferedPlugin(_ context.Context, id, version string) (string, bool, error) {
	return strings.Repeat("d", 64), id == "net.example.board" && version == "1.0.0", nil
}

type runsFixture struct {
	*env
	c     *client
	admin hub.User
	anna  hub.User
	game  hub.Game
	run   hub.Run
}

func newRunsFixture(t *testing.T) *runsFixture {
	t.Helper()
	e := newEnv(t, true, nil)
	e.svc.SetPluginCatalog(oneOffered{})
	c := e.client()
	c.login()
	admin, err := e.svc.VerifyPassword(bg, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	anna, err := e.svc.CreateUser(bg, "anna", "Anna")
	if err != nil {
		t.Fatal(err)
	}
	g, err := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	run, err := e.svc.CreateRun(bg, hub.CreateRunInput{UserID: admin.ID, Name: "Summer Duo", GameID: g.ID, InviteUserIDs: []string{anna.ID},
		Plugins: []hub.RunPluginRef{{PluginID: "net.example.board", Version: "1.0.0"}}})
	if err != nil {
		t.Fatal(err)
	}
	return &runsFixture{env: e, c: c, admin: admin, anna: anna, game: g, run: run}
}

func (f *runsFixture) post(path string) string {
	f.t.Helper()
	rec := f.c.postForm(path, url.Values{"_csrf": {f.c.csrf()}}, nil)
	status(f.t, rec, 303)
	return location(rec)
}

func (f *runsFixture) putBoard(tree string) {
	f.t.Helper()
	if _, err := f.svc.PutRunBoard(bg, f.admin.ID, f.run.ID, "net.example.board", json.RawMessage(tree)); err != nil {
		f.t.Fatal(err)
	}
}

func TestRunsPageListDetailAndActions(t *testing.T) {
	f := newRunsFixture(t)
	c := f.c

	// Empty state without runs is covered by the second environment below; here: list and detail of a starting run.
	rec := c.get("/runs", nil)
	status(t, rec, 200)
	contains(t, rec, "Runs · 1", "Summer Duo", "Starting", "1 of 2 joined", `class="runs-item sel"`, "Participants · 2", "Lumen Drift", "Joined", "Invited",
		"Plugin content", "net.example.board 1.0.0", "No board yet", "Event log · 2", "Run created by admin", "admin joined", "Export JSON", "Start now", "Waiting for", "Anna", "Resend invite", "Start without Anna",
		`hx-trigger="fb:runs from:body"`, `id="runs-view"`, "/static/plugin-blocks.js", `href="/runs"`)
	notContains(t, rec, `action="/runs/`+f.run.ID+`/pause"`, "Resume run", ">Archive<", "Stream", "<no value>", "Author")
	if inlineRe.MatchString(rec.Body.String()) {
		t.Fatalf("inline script/style: %s", inlineRe.FindString(rec.Body.String()))
	}
	contains(t, c.get("/runs/"+f.run.ID, nil), "Summer Duo", "Delete run…")
	status(t, c.get("/runs/00000000-0000-4000-8000-000000000000", nil), 303)
	if l := location(c.get("/runs/nope", nil)); l != "/runs?err=norun" {
		t.Fatalf("%s", l)
	}
	contains(t, c.get("/runs?err=norun", nil), "Run not found.")

	// The live region as fragment for htmx.
	rec = c.get("/runs/"+f.run.ID, hxHdr("runs-view"))
	status(t, rec, 200)
	contains(t, rec, `id="runs-view"`, "Summer Duo")
	notContains(t, rec, "<html", "<aside class=\"sidebar\"")

	// Pause is possible once the run is active; the page then offers Resume and shows the banner.
	if _, err := f.svc.SetRunStatus(bg, f.admin.ID, f.run.ID, "active"); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/runs/"+f.run.ID, nil)
	contains(t, rec, "Pause run", "1 active")
	notContains(t, rec, "Resume run")
	if l := f.post("/runs/" + f.run.ID + "/pause"); l != "/runs/"+f.run.ID+"?ok=runpaused" {
		t.Fatalf("%s", l)
	}
	rec = c.get("/runs/"+f.run.ID+"?ok=runpaused", nil)
	contains(t, rec, "Resume run", "Paused", "Paused by admin", "events still visible, no new ones recorded", `role="status"`, "Run paused.", "admin paused the run")
	notContains(t, rec, `action="/runs/`+f.run.ID+`/pause"`)
	// A paused run refuses writes from Players (409 run_not_active) while it is paused.
	if _, _, err := f.svc.AppendRunEvent(bg, hub.AppendRunEventInput{UserID: f.admin.ID, DeviceID: "d", RunID: f.run.ID,
		ClientID: "6f1d2c3b-4a5e-4b6c-8d7e-9f0a1b2c3d4e", PluginID: "net.example.board", Source: "manual", Type: "x"}); err == nil {
		t.Fatal("event accepted while paused")
	}
	// Pausing again is a conflict shown as message.
	if l := f.post("/runs/" + f.run.ID + "/pause"); l != "/runs/"+f.run.ID+"?err=runstate" {
		t.Fatalf("%s", l)
	}
	contains(t, c.get("/runs/"+f.run.ID+"?err=runstate", nil), "The run changed in the meantime")
	if l := f.post("/runs/" + f.run.ID + "/resume"); l != "/runs/"+f.run.ID+"?ok=runresumed" {
		t.Fatalf("%s", l)
	}
	contains(t, c.get("/runs/"+f.run.ID, nil), "admin resumed the run", "Pause run")

	// Archive only for runs that are over; the filter views follow.
	if l := f.post("/runs/" + f.run.ID + "/archive"); l != "/runs/"+f.run.ID+"?err=runstate" {
		t.Fatalf("archiving an active run: %s", l)
	}
	if _, err := f.svc.SetRunStatus(bg, f.admin.ID, f.run.ID, "finished"); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/runs", nil)
	contains(t, rec, "Finished", "Archive", "Finished by admin")
	notContains(t, rec, `action="/runs/`+f.run.ID+`/pause"`, "Resume run")
	f.post("/runs/" + f.run.ID + "/archive")
	rec = c.get("/runs", nil)
	contains(t, rec, "Runs · 0", "No runs in this view.", "No run selected")
	rec = c.get("/runs?filter=archived", nil)
	contains(t, rec, "Runs · 1", "Summer Duo", "Unarchive")
	contains(t, c.get("/runs?filter=finished", nil), "Runs · 0")
	f.post("/runs/" + f.run.ID + "/unarchive")
	contains(t, c.get("/runs?filter=finished", nil), "Runs · 1")
	contains(t, c.get("/runs?filter=bogus", nil), "Runs · 1") // unknown filter = All

	// Delete asks first (names what is removed), then removes everything.
	rec = c.get("/runs/"+f.run.ID+"/delete", nil)
	status(t, rec, 200)
	contains(t, rec, "Delete “Summer Duo”?", "2 participants", "The event log (", "/runs/"+f.run.ID+"/delete", "Delete run")
	if l := f.post("/runs/" + f.run.ID + "/delete"); l != "/runs?ok=rundeleted" {
		t.Fatalf("%s", l)
	}
	contains(t, c.get("/runs?ok=rundeleted", nil), "Run deleted.", "No run selected")
	if _, err := f.svc.GetRunAdmin(bg, f.run.ID); err == nil {
		t.Fatal("run still there")
	}
}

func TestRunsMutationsNeedCSRFAndAdmin(t *testing.T) {
	f := newRunsFixture(t)
	for _, p := range []string{"pause", "resume", "archive", "unarchive", "delete"} {
		rec := f.c.postForm("/runs/"+f.run.ID+"/"+p, url.Values{}, nil)
		status(t, rec, 403)
	}
	anon := f.env.client()
	for _, p := range []string{"/runs", "/runs/" + f.run.ID, "/runs/" + f.run.ID + "/export", "/runs/" + f.run.ID + "/delete"} {
		rec := anon.get(p, nil)
		status(t, rec, 303)
		if l := location(rec); l != "/login" {
			t.Fatalf("%s -> %s", p, l)
		}
	}
	rec := anon.postForm("/runs/"+f.run.ID+"/delete", url.Values{}, nil)
	status(t, rec, 303)
	if _, err := f.svc.GetRunAdmin(bg, f.run.ID); err != nil {
		t.Fatalf("an anonymous request deleted the run: %v", err)
	}
}

var dataTreeRe = regexp.MustCompile(`data-tree="([^"]*)"`)

func TestRunsBoardJSONIsEmbeddedSafely(t *testing.T) {
	f := newRunsFixture(t)
	hostile := []string{
		`</script><script>alert(1)</script>`,
		`"><img src=x onerror=alert(1)>`,
		`<!-- <script>`,
		`'; alert(1); //`,
		"line\nbreak sep",
		`{{.CSRF}}`,
		`&lt;b&gt; &amp;`,
	}
	children := []map[string]any{}
	for _, h := range hostile {
		children = append(children, map[string]any{"type": "text", "text": h}, map[string]any{"type": "button", "label": h, "action": "x"})
	}
	tree := map[string]any{"type": "column", "children": children, "title": hostile[0]}
	raw, _ := json.Marshal(tree)
	f.putBoard(string(raw))

	rec := f.c.get("/runs/"+f.run.ID, nil)
	status(t, rec, 200)
	body := rec.Body.String()
	for _, bad := range []string{"<script>alert", "</script><script>alert", "<img src=x", "<!-- <script>", `onerror=alert(1)>`} {
		if strings.Contains(body, bad) {
			t.Fatalf("raw plugin string %q reached the page", bad)
		}
	}
	// Only the Hub's own static scripts are on the page, none inline.
	scripts := regexp.MustCompile(`<script[^>]*>`).FindAllString(body, -1)
	for _, s := range scripts {
		if !strings.Contains(s, `src="/static/`) {
			t.Fatalf("unexpected script element %q", s)
		}
	}
	// The tree travels in one attribute and decodes back to exactly the published JSON.
	m := dataTreeRe.FindStringSubmatch(body)
	if m == nil {
		t.Fatal("no data-tree attribute")
	}
	var got, want any
	if err := json.Unmarshal([]byte(html.UnescapeString(m[1])), &got); err != nil {
		t.Fatalf("attribute is not the JSON: %v", err)
	}
	json.Unmarshal(raw, &want)
	gb, _ := json.Marshal(got)
	wb, _ := json.Marshal(want)
	if string(gb) != string(wb) {
		t.Fatalf("round trip differs:\n%s\n%s", gb, wb)
	}
	contains(t, rec, `class="plugin-board"`, "Updated ")

	// The plugin-authored run name, participant name and event text are escaped as well.
	if _, _, err := f.svc.AppendRunEvent(bg, hub.AppendRunEventInput{UserID: f.admin.ID, DeviceID: "d", RunID: f.run.ID,
		ClientID: "0b8e4f6a-1c2d-4e3f-9a4b-5c6d7e8f9a0b", PluginID: "net.example.board", Source: "automatic", Type: "x", Text: `<script>alert(2)</script>`}); err != nil {
		t.Fatal(err)
	}
	rec = f.c.get("/runs/"+f.run.ID, nil)
	contains(t, rec, "&lt;script&gt;alert(2)&lt;/script&gt;", "automatic · admin&#39;s Player")
	notContains(t, rec, "<script>alert(2)")
}

func TestRunsExport(t *testing.T) {
	f := newRunsFixture(t)
	for i := 0; i < 5; i++ {
		if _, _, err := f.svc.AppendRunEvent(bg, hub.AppendRunEventInput{UserID: f.admin.ID, DeviceID: "d", RunID: f.run.ID,
			ClientID: "0b8e4f6a-1c2d-4e3f-9a4b-5c6d7e8f9a0" + string(rune('a'+i)), PluginID: "net.example.board", Source: "manual", Type: "tick", Text: "t"}); err != nil {
			t.Fatal(err)
		}
	}
	rec := f.c.get("/runs/"+f.run.ID+"/export", nil)
	status(t, rec, 200)
	if ct := rec.Header().Get("Content-Type"); ct != "application/json" {
		t.Fatal(ct)
	}
	if cd := rec.Header().Get("Content-Disposition"); !strings.HasPrefix(cd, "attachment;") || !strings.Contains(cd, f.run.ID) {
		t.Fatal(cd)
	}
	var doc struct {
		RunID   string `json:"run_id"`
		Name    string `json:"name"`
		LastSeq int64  `json:"last_seq"`
		Events  []struct {
			Seq  int64  `json:"seq"`
			Type string `json:"type"`
		} `json:"events"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &doc); err != nil {
		t.Fatalf("%v: %s", err, rec.Body.String())
	}
	if doc.RunID != f.run.ID || doc.Name != "Summer Duo" || doc.LastSeq != 7 || len(doc.Events) != 7 || doc.Events[0].Type != "run.created" || doc.Events[6].Seq != 7 {
		t.Fatalf("%+v", doc)
	}
	status(t, f.c.get("/runs/00000000-0000-4000-8000-000000000000/export", nil), 303)
}

func TestRunsEmptyAndNav(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	c.login()
	rec := c.get("/runs", nil)
	status(t, rec, 200)
	contains(t, rec, "Runs · 0", "No run selected", `class="active" aria-current="page" hx-get="/runs"`)
	notContains(t, rec, "active</span>")
	contains(t, c.get("/nav-fragment", map[string]string{"HX-Current-URL": "http://hub/runs/abc"}), `href="/runs" class="active"`)
}

// The renderer is plain JavaScript without a build step or a JS test setup; these checks pin its safety properties.
func TestPluginBlocksScriptRules(t *testing.T) {
	b, err := os.ReadFile("static/plugin-blocks.js")
	if err != nil {
		t.Fatal(err)
	}
	src := string(b)
	for _, banned := range []string{"innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function", "createContextualFragment",
		"fetch(", "XMLHttpRequest", "WebSocket", "setAttribute(\"style\"", "javascript:"} {
		if strings.Contains(src, banned) {
			t.Errorf("plugin-blocks.js uses %q", banned)
		}
	}
	// The whole catalog of client/plugins/plugin_ui.cpp is drawn.
	for _, typ := range []string{"column", "row", "grid", "tabs", "scroll", "card", "section", "text", "chip", "badge", "avatar", "image", "bar", "stat",
		"list", "table", "button", "toggle", "select", "segmented", "textfield", "empty", "location", "map"} {
		if !strings.Contains(src, "R."+typ+" = function") {
			t.Errorf("block type %q has no renderer", typ)
		}
	}
	for _, must := range []string{"Unsupported block", "data:image\\/png;base64", "textContent"} {
		if !strings.Contains(src, must) {
			t.Errorf("plugin-blocks.js lacks %q", must)
		}
	}
	// Interactive blocks are rendered disabled.
	for _, typ := range []string{"button", "toggle", "select", "segmented", "textfield"} {
		i := strings.Index(src, "R."+typ+" = function")
		j := strings.Index(src[i:], "\n  };")
		if !strings.Contains(src[i:i+j], "disabled(") {
			t.Errorf("%s is not rendered disabled", typ)
		}
	}
	rec := newEnv(t, true, nil).client().get("/static/plugin-blocks.js", nil)
	status(t, rec, 200)
	if strings.Contains(rec.Body.String(), "http://") || strings.Contains(rec.Body.String(), "https://") {
		t.Error("external URL in plugin-blocks.js")
	}
}

// The page names plugins by their display name from the plugin store (the id when the store does not hold the version).
func TestRunsShowPluginDisplayNames(t *testing.T) {
	e := newEnvOpts(t, true, nil, func(o *hub.Options) { o.CoreTrustKeys = hubtest.PluginKeys() })
	c := e.client()
	c.login()
	admin, err := e.svc.VerifyPassword(bg, "admin", "secret-12345")
	if err != nil {
		t.Fatal(err)
	}
	g, err := e.svc.AddROM(bg, bytes.NewReader([]byte("homebrew-dummy-rom")), "lumen.nds", "Lumen Drift", "", admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	st, err := e.svc.StagePluginUpload(bg, bytes.NewReader(hubtest.PluginZip(t, "net.example.named", "1.0.0", nil, nil)))
	if err != nil {
		t.Fatal(err)
	}
	p, err := e.svc.OfferStagedPlugin(bg, st.StageID, nil, admin.ID)
	if err != nil {
		t.Fatal(err)
	}
	run, err := e.svc.CreateRun(bg, hub.CreateRunInput{UserID: admin.ID, Name: "Named", GameID: g.ID,
		Plugins: []hub.RunPluginRef{{PluginID: "net.example.named", Version: "1.0.0"}}})
	if err != nil {
		t.Fatal(err)
	}
	rec := c.get("/runs/"+run.ID, nil)
	status(t, rec, 200)
	contains(t, rec, html.EscapeString(p.Name+" 1.0.0"))
	// Removing the offer (the run is not finished, so first end it) falls back to the id.
	if _, err := e.svc.SetRunStatus(bg, admin.ID, run.ID, hub.RunActive); err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.SetRunStatus(bg, admin.ID, run.ID, hub.RunFinished); err != nil {
		t.Fatal(err)
	}
	if err := e.svc.RemovePlugin(bg, "net.example.named", "1.0.0"); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/runs/"+run.ID, nil)
	contains(t, rec, "net.example.named 1.0.0")
	notContains(t, rec, html.EscapeString(p.Name+" 1.0.0"))
}
