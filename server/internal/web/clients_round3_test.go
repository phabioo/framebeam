package web

import (
	"net/url"
	"testing"

	"github.com/google/uuid"
)

func TestClientsRenameRevokeConfirmAndDenied(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	users, _ := e.svc.ListUsers(bg)
	dev := pairTestDevice(t, e, users[0].ID, "Living room PC")

	// Row actions: one visible button plus the menu; no red text link in the row.
	rec := c.get("/clients", nil)
	contains(t, rec, ">Rename</a>", "Copy device ID", "Revoke access…", "Delete device…", `data-copy-link="`+dev+`"`, "Never connected")
	notContains(t, rec, `class="btn-link"`)

	// Inline rename: the form opens on the row; a bad name keeps it open with the reason; success renames.
	rec = c.get("/clients?rename="+dev, hxHdr("clients-body"))
	status(t, rec, 200)
	contains(t, rec, `action="/clients/devices/`+dev+`/rename"`, `name="name" value="Living room PC"`, "Save")
	notContains(t, rec, `hx-trigger="fb:clients from:body"`) // the region does not reload under the open form
	rec = c.postForm("/clients/devices/"+dev+"/rename", url.Values{"name": {"  "}}, map[string]string{"X-CSRF-Token": tok, "HX-Request": "true", "HX-Target": "clients-body"})
	status(t, rec, 400)
	contains(t, rec, "Device name must be 1 to 100 characters long", `name="name"`)
	rec = c.postForm("/clients/devices/"+dev+"/rename", url.Values{"name": {"Den PC"}, "_csrf": {tok}}, nil)
	status(t, rec, 303)
	if location(rec) != "/clients?ok=devrenamed" {
		t.Fatalf("location %q", location(rec))
	}
	contains(t, c.get("/clients?ok=devrenamed", nil), "Device renamed.", "Den PC")
	status(t, c.postForm("/clients/devices/"+dev+"/rename", url.Values{"name": {"X"}}, nil), 403) // CSRF
	status(t, c.postForm("/clients/devices/nope/rename", url.Values{"name": {"X"}, "_csrf": {tok}}, nil), 303)

	// Revoke goes through a confirmation that says what happens; the POST from it redirects.
	rec = c.get("/clients/devices/"+dev+"/revoke", nil)
	status(t, rec, 200)
	contains(t, rec, "Revoke access for Den PC?", "loses access at once", "Saves stay on the Hub", `action="/clients/devices/`+dev+`/revoke"`, "Revoke access</button>")
	rec = c.postForm("/clients/devices/"+dev+"/revoke", url.Values{"_csrf": {tok}}, nil)
	status(t, rec, 303)
	contains(t, c.get("/clients?ok=devrevoked", nil), "Access revoked.", "Revoked", "Details")
	// Revoked devices offer Details, not Rename; the details row explains the state.
	rec = c.get("/clients?details="+dev, nil)
	contains(t, rec, "Device ID", dev, "Access was revoked")

	// A denied request stays visible as a grey "Denied" row that never connected.
	pr := pending(t, e, uuid.NewString())
	if err := e.svc.DenyPairing(bg, pr.RequestID); err != nil {
		t.Fatal(err)
	}
	rec = c.get("/clients", nil)
	contains(t, rec, "Lena Gaming PC", ">Denied</span>", "Never connected")
	notContains(t, rec, "Pending requests · 1")
	rec = c.get("/clients?details="+pr.RequestID, nil)
	contains(t, rec, "Request ID", "The request was denied")
}
