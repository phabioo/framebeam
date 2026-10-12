package web

import (
	"net/url"
	"regexp"
	"strings"
	"testing"
	"time"
)

func TestUsersCreateRenameRoleAndCode(t *testing.T) {
	e := newEnv(t, true, nil)
	c := e.client()
	tok := c.login()
	admin, _ := e.svc.VerifyPassword(bg, "admin", "secret-12345")

	// The Create user form opens on demand; the page offers Create user, Invite and the 7-day expiry.
	rec := c.get("/users", nil)
	contains(t, rec, `>Create user</a>`, `>Invite</a>`, "7 days", `type="radio" name="expiry"`, `class="check-card"`)
	notContains(t, rec, `id="create-user"`, `<select name="expiry"`)
	contains(t, c.get("/users?create=1", hxHdr("users-table")), `id="create-user"`, `action="/users/create"`)

	// Create: the pairing code appears once, in this response only; a taken name is refused in the form.
	rec = postTok(c, tok, "/users/create", url.Values{"name": {"Sam"}})
	status(t, rec, 200)
	code := regexp.MustCompile(`FB-[A-Z0-9]{4}-[A-Z0-9]{4}`).FindString(rec.Body.String())
	if code == "" {
		t.Fatalf("no code in: %.400s", rec.Body.String())
	}
	contains(t, rec, "Pairing code for Sam", "Shown once", "expires in 24 h", "Copy link")
	notContains(t, c.get("/users", nil), code)
	rec = postTok(c, tok, "/users/create", url.Values{"name": {"sam"}})
	status(t, rec, 400)
	contains(t, rec, "Display name is already taken", `id="create-user"`, `value="sam"`)
	status(t, postTok(c, tok, "/users/create", url.Values{"name": {" "}}), 400)
	status(t, e.client().postForm("/users/create", url.Values{"name": {"X"}}, nil), 303) // not signed in
	users, _ := e.svc.ListUsers(bg)
	var sam string
	for _, u := range users {
		if u.DisplayName == "Sam" {
			sam = u.ID
		}
	}
	if sam == "" {
		t.Fatal("Sam not created")
	}

	// Rename inline; a taken name keeps the form open.
	contains(t, c.get("/users?rename="+sam, nil), `action="/users/`+sam+`/rename"`, `name="name" value="Sam"`)
	rec = postTok(c, tok, "/users/"+sam+"/rename", url.Values{"name": {"admin"}})
	status(t, rec, 400)
	contains(t, rec, "Display name is already taken", `name="name"`)
	rec = postTok(c, tok, "/users/"+sam+"/rename", url.Values{"name": {"Samuel"}})
	if rec.Code != 303 || location(rec) != "/users?ok=userrenamed" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	contains(t, c.get("/users?ok=userrenamed", nil), "User renamed.", "Samuel")

	// Menu: regular users get Make admin, a new pairing code, Disable and Delete; the own row has no menu.
	rec = c.get("/users", nil)
	contains(t, rec, "Make admin", "New pairing code", "Disable user", "Delete user…", "(you)")
	if strings.Contains(rec.Body.String(), "/users/"+admin.ID+"/rename") {
		t.Fatal("own row offers Rename")
	}
	rec = postTok(c, tok, "/users/"+sam+"/code", nil)
	status(t, rec, 200)
	contains(t, rec, "Pairing code for Samuel", "Shown once")

	// Make admin asks for a password; a short one is refused on the form, a good one makes the role change.
	rec = c.get("/users/"+sam+"/admin", nil)
	contains(t, rec, "Make Samuel an admin?", `type="password"`, `action="/users/`+sam+`/admin"`, "at least 12 characters")
	rec = postTok(c, tok, "/users/"+sam+"/admin", url.Values{"password": {"short"}})
	status(t, rec, 400)
	contains(t, rec, "Password must be at least 12 characters long")
	rec = postTok(c, tok, "/users/"+sam+"/admin", url.Values{"password": {"correct-horse-battery"}})
	if rec.Code != 303 || location(rec) != "/users?ok=userrole" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	if u, _ := e.svc.GetUser(bg, sam); u.Role != "admin" {
		t.Fatalf("role %q", u.Role)
	}
	contains(t, c.get("/users?ok=userrole", nil), "Role changed.", "Make regular user")
	// Back to a regular user; the own account and the last admin cannot be demoted.
	rec = postTok(c, tok, "/users/"+sam+"/role", url.Values{"role": {"user"}})
	if rec.Code != 303 || location(rec) != "/users?ok=userrole" {
		t.Fatalf("%d %q", rec.Code, location(rec))
	}
	rec = postTok(c, tok, "/users/"+admin.ID+"/role", url.Values{"role": {"user"}})
	if location(rec) != "/users?err=rolekept" {
		t.Fatalf("%q", location(rec))
	}
	contains(t, c.get("/users?err=rolekept", nil), "The role cannot be changed")
	status(t, c.postForm("/users/"+sam+"/role", url.Values{"role": {"user"}}, nil), 403) // CSRF

	// Revoking an invite goes through a confirmation.
	postTok(c, tok, "/users/invites", url.Values{"expiry": {"7d"}, "authorize": {"1"}})
	invs, _ := e.svc.ListInvites(bg, 0)
	var open string
	for _, i := range invs {
		if i.Status == "active" && i.ForUser == "" {
			open = i.ID
			if d := i.ExpiresAt.Sub(i.CreatedAt); d != 7*24*time.Hour {
				t.Fatalf("expiry %v", d)
			}
		}
	}
	if open == "" {
		t.Fatal("no open invite")
	}
	contains(t, c.get("/users", nil), "Pairing code for Samuel", "expires in 24 h", "Revoke invite…", "expires in 7")
	contains(t, c.get("/users/invites/"+open+"/revoke", nil), "Revoke this invite?", `action="/users/invites/`+open+`/revoke"`)
}
