package web

import (
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSettingsRound3Network(t *testing.T) {
	e, _ := netEnv(t, nil)
	c := e.client()
	tok := c.login()
	rec := c.get("/settings/network", nil)
	// Cloudflare TURN is a pill, not bare text; the external TURN form is always there with an "Add" button.
	contains(t, rec, `class="pill nowrap">Off</span>`, "Add server", ">Add</button>")
	notContains(t, rec, "+ Add server", "<summary>+")
	// STUN uses the inline field plus "Add".
	rec = postTok(c, tok, "/settings/network/ice_servers", url.Values{"op": {"add"}, "url": {"stun:stun.example.com:3478"}})
	status(t, rec, 303)
	rec = c.get("/settings/network", nil)
	contains(t, rec, "stun:stun.example.com:3478", "Remove server…", ">Add</button>")

	// A long import path keeps its full text in the title and offers Copy.
	long := filepath.Join(t.TempDir(), "imports", "with", "a", "very", "long", "path", "that", "would", "not", "fit", "into", "one", "input", "field")
	if err := os.MkdirAll(long, 0o755); err != nil {
		t.Fatal(err)
	}
	status(t, postTok(c, tok, "/settings/network/import_dir", url.Values{"value": {long}}), 303)
	rec = c.get("/settings/network", nil)
	contains(t, rec, `title="`+long+`"`, `data-copy="`+long+`"`)
}

func TestSettingsRound3SavesAndSecurity(t *testing.T) {
	e := newEnv(t, true, func(c *Config) { c.UseTLS, c.CertFingerprint, c.CertSource = true, "AA:BB:CC", "Self-generated" })
	c := e.client()
	c.login()
	// The nav has Saves with its status line.
	rec := c.get("/settings/general", nil)
	contains(t, rec, `href="/settings/saves"`, "Name, theme, uploads", "Self-signed certificate")
	// The Security card has no second Sign out; the sidebar keeps the one.
	sec := c.get("/settings/security", nil).Body.String()
	area := sec[strings.Index(sec, `id="settings-area"`):]
	if strings.Contains(area, `action="/logout"`) {
		t.Fatal("Security still has its own Sign out")
	}
	contains(t, c.get("/settings/diagnostics", nil), `btn small primary nowrap`)
}
