package web

import (
	"net/url"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

func TestRunsWaitingBannerInviteActionsAndStart(t *testing.T) {
	f := newRunsFixture(t)
	c := f.c
	rid := f.run.ID

	// A starting run with an invited player shows the banner, the participant actions and a cyan status.
	rec := c.get("/runs/"+rid, nil)
	contains(t, rec, "Waiting for <b>Anna</b> to join", "1 of 2 joined", "invited", `action="/runs/`+rid+`/resend"`,
		"Start without Anna", `action="/runs/`+rid+`/start-without"`, `action="/runs/`+rid+`/invites/`+f.anna.ID+`/resend"`,
		"Remove invite…", `class="pill lg info"`, "Finish run…", "Delete run…", "after start", "after finish")
	notContains(t, rec, "(s)")

	// Resend: the event log gets a Hub event without an author column.
	if l := f.post("/runs/" + rid + "/invites/" + f.anna.ID + "/resend"); l != "/runs/"+rid+"?ok=invitesent" {
		t.Fatalf("%s", l)
	}
	if l := f.post("/runs/" + rid + "/resend"); l != "/runs/"+rid+"?ok=invitesent" {
		t.Fatalf("%s", l)
	}
	rec = c.get("/runs/"+rid+"?ok=invitesent", nil)
	contains(t, rec, "Invite sent again.", "admin sent the invite to Anna again")
	notContains(t, rec, "<th>Author</th>", "<th>Source</th>")

	// Removing the invite asks first.
	rec = c.get("/runs/"+rid+"/invites/"+f.anna.ID+"/remove", nil)
	contains(t, rec, "Remove the invite of Anna?", `action="/runs/`+rid+`/invites/`+f.anna.ID+`/remove"`)
	if l := f.post("/runs/" + rid + "/invites/" + f.anna.ID + "/remove"); l != "/runs/"+rid+"?ok=inviteremoved" {
		t.Fatalf("%s", l)
	}
	rec = c.get("/runs/"+rid, nil)
	notContains(t, rec, "Waiting for", "Remove invite…")
	contains(t, rec, "admin removed the invite of Anna")
	// Nothing left to remove: a conflict-free redirect back with the generic message.
	if l := f.post("/runs/" + rid + "/invites/" + f.anna.ID + "/remove"); !strings.Contains(l, "err=") {
		t.Fatalf("second removal: %s", l)
	}

	// Start now (no one is waiting any more).
	if l := f.post("/runs/" + rid + "/start"); l != "/runs/"+rid+"?ok=runstarted" {
		t.Fatalf("%s", l)
	}
	run, _ := f.svc.GetRunAdmin(bg, rid)
	if run.Status != hub.RunActive {
		t.Fatal(run.Status)
	}
	contains(t, c.get("/runs/"+rid+"?ok=runstarted", nil), "Run started.", "admin started the run")

	// Finish asks first; a finished run offers Archive and the archived view is a link, not a chip.
	rec = c.get("/runs/"+rid+"/finish", nil)
	contains(t, rec, "Finish “Summer Duo”?", `action="/runs/`+rid+`/finish"`)
	if l := f.post("/runs/" + rid + "/finish"); l != "/runs/"+rid+"?ok=runfinished" {
		t.Fatalf("%s", l)
	}
	contains(t, c.get("/runs/"+rid+"?ok=runfinished", nil), "Run finished.", "Finished", `class="pill lg ok"`, `<button class="mi" type="submit">Archive</button>`)
	if l := f.post("/runs/" + rid + "/finish"); l != "/runs/"+rid+"?err=runstate" {
		t.Fatalf("finishing twice: %s", l)
	}
	f.post("/runs/" + rid + "/archive")
	rec = c.get("/runs", nil)
	contains(t, rec, "Show archived · 1")
	notContains(t, rec, ">Archived</a>")
	contains(t, c.get("/runs?filter=archived", nil), "Hide archived", "Summer Duo")
}

func TestRunsStartWithoutDropsInvitedPlayers(t *testing.T) {
	f := newRunsFixture(t)
	rid := f.run.ID
	if l := f.post("/runs/" + rid + "/start-without"); l != "/runs/"+rid+"?ok=runstarted" {
		t.Fatalf("%s", l)
	}
	run, _ := f.svc.GetRunAdmin(bg, rid)
	if run.Status != hub.RunActive || len(run.Participants) != 1 {
		t.Fatalf("%s %d", run.Status, len(run.Participants))
	}
	// Mutations need the CSRF token and a signed-in admin.
	for _, p := range []string{"start", "start-without", "finish", "resend", "invites/" + f.anna.ID + "/resend", "invites/" + f.anna.ID + "/remove"} {
		status(t, f.c.postForm("/runs/"+rid+"/"+p, url.Values{}, nil), 403)
		status(t, f.env.client().postForm("/runs/"+rid+"/"+p, url.Values{}, nil), 303)
	}
}

func TestRunsBoardTitleFallbackAndLogMinutes(t *testing.T) {
	f := newRunsFixture(t)
	f.putBoard(`{"type":"column","children":[]}`)
	rec := f.c.get("/runs/"+f.run.ID, nil)
	contains(t, rec, `data-tree=`)
	notContains(t, rec, "no board title set")
	f.putBoard(`{"type":"column","title":"Route 1","children":[]}`)
	rec = f.c.get("/runs/"+f.run.ID, nil)
	contains(t, rec, "Route 1")
	// Times in the log carry no seconds in the text; the full timestamp is in the tooltip.
	contains(t, rec, `class="event-time"><span title="`, "just now")
}

func TestRunStatusClasses(t *testing.T) {
	for st, want := range map[string]string{hub.RunStarting: "info", hub.RunActive: "info", hub.RunPaused: "amber", hub.RunFinished: "ok"} {
		if runStatusClasses[st] != want {
			t.Errorf("%s: %q, want %q", st, runStatusClasses[st], want)
		}
	}
}
