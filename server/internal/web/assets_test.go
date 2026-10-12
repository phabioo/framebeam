package web

import (
	"net/http/httptest"
	"strings"
	"testing"
)

func TestContentHashChangesWithContent(t *testing.T) {
	a, b := contentHash([]byte("a{}")), contentHash([]byte("b{}"))
	if len(a) != 10 || a == b || a != contentHash([]byte("a{}")) {
		t.Fatalf("unexpected hashes %q %q", a, b)
	}
}

func TestAssetURLAndCacheControl(t *testing.T) {
	u := assetURL("app.css")
	if !strings.HasPrefix(u, "/static/app.css?v=") || len(u) != len("/static/app.css?v=")+10 {
		t.Fatalf("assetURL = %q", u)
	}
	cc := func(target string) string { return staticCacheControl(httptest.NewRequest("GET", target, nil)) }
	if got := cc(u); got != "public, max-age=31536000, immutable" {
		t.Errorf("versioned: %q", got)
	}
	for _, target := range []string{"/static/app.css", "/static/app.css?v=deadbeef00", "/static/missing.css?v=" + strings.Split(u, "=")[1]} {
		if got := cc(target); got != "no-cache" {
			t.Errorf("%s: %q", target, got)
		}
	}
}

func TestLayoutUsesVersionedAssets(t *testing.T) {
	c := newEnv(t, true, nil).client()
	c.login()
	rec := c.get("/library", nil)
	contains(t, rec, `href="`+assetURL("app.css")+`"`, `src="`+assetURL("app.js")+`"`)
	if !strings.Contains(assetURL("app.css"), "?v=") {
		t.Fatal("no version")
	}
	css := c.get(assetURL("app.css"), nil)
	if got := css.Header().Get("Cache-Control"); got != "public, max-age=31536000, immutable" {
		t.Errorf("versioned Cache-Control = %q", got)
	}
	plain := c.get("/static/app.css", nil)
	if got := plain.Header().Get("Cache-Control"); got != "no-cache" {
		t.Errorf("plain Cache-Control = %q", got)
	}
}

// The row menu (.menu-pop) is absolutely positioned inside the table; overflow hidden on it would clip the menu.
func TestTabpanelTableDoesNotClipRowMenu(t *testing.T) {
	css := appCSS(t)
	for _, line := range strings.Split(css, "\n") {
		if strings.HasPrefix(line, ".tabpanel table.list {") && strings.Contains(line, "overflow") {
			t.Fatalf("rule clips the row menu: %s", line)
		}
	}
}

// The header "Invite" jump link only shows where the Invites panel stacks below the table.
func TestInviteJumpLinkOnlyWhenPanelStacks(t *testing.T) {
	css := appCSS(t)
	if !strings.Contains(css, ".invite-jump { display: none; }") {
		t.Fatal("invite-jump is not hidden by default")
	}
	i := strings.Index(css, "@media (max-width: 1023px) {\n  .users {")
	if i < 0 || !strings.Contains(css[i:], ".invite-jump { display: inline-flex; }") {
		t.Fatal("invite-jump not shown in the stacking breakpoint")
	}
}

// Round 3 sets .card to overflow visible; the event log card must still scroll inside its box.
func TestEventsScrollOverridesCardOverflow(t *testing.T) {
	css := appCSS(t)
	if !strings.Contains(css, ".card.events-scroll { overflow-y: auto;") {
		t.Fatal("missing .card.events-scroll override")
	}
}

// appCSS returns app.css with LF line endings (a Windows checkout may contain CRLF).
func appCSS(t *testing.T) string {
	t.Helper()
	data, err := staticFS.ReadFile("static/app.css")
	if err != nil {
		t.Fatal(err)
	}
	return normalizeNewlines(string(data))
}

func normalizeNewlines(s string) string { return strings.ReplaceAll(s, "\r\n", "\n") }

func TestNormalizeNewlines(t *testing.T) {
	if got := normalizeNewlines("a {\r\n  b\r\n}"); got != "a {\n  b\n}" {
		t.Fatalf("got %q", got)
	}
}
