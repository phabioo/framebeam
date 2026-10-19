package httpapi

import (
	"testing"
)

// Decision (repo scan PI-11): invited, declined and left users see only a run's title, game and participant list;
// events and plugin states are for joined participants only (REST and WSS alike).
func TestRunAuthzNonJoinedSeeNoEventsOrStates(t *testing.T) {
	r := newRunEnv(t)
	d1, a1, b1, c1, e1 := r.device(r.admin.ID, "Desktop"), r.device(r.anna.ID, "Anna PC"), r.device(r.bob.ID, "Bob PC"),
		r.device(r.carol.ID, "Carol PC"), r.device(r.dave.ID, "Dave PC")
	run := r.create(d1, "Authz", r.anna.ID, r.bob.ID, r.carol.ID)
	id := run.RunID
	r.mustJoin(a1, id)
	wantStatus(t, r.post(c1, id, "decline", nil), 200, "")
	wantStatus(t, r.putState(d1, id, 0, map[string]any{"team": []any{"secret"}}), 200, "")
	wantStatus(t, r.putState(a1, id, 0, map[string]any{"team": []any{"anna"}}), 200, "")
	wantStatus(t, r.event(d1, id, "catch"), 201, "")

	noStates := func(who string, got runResp) {
		t.Helper()
		if got.Name != "Authz" || len(got.Participants) != 4 || got.part(r.admin.ID).GameID == nil {
			t.Fatalf("%s: title, game and participants must stay visible: %+v", who, got)
		}
		for _, p := range got.Participants {
			if len(p.States) != 0 {
				t.Fatalf("%s sees the state of %s", who, p.UserID)
			}
		}
	}
	list := func(d dev) []runResp {
		rec := r.api(d, "GET", "/api/v1/runs", nil)
		wantStatus(t, rec, 200, "")
		return decode[struct{ Runs []runResp }](t, rec).Runs
	}
	eventsPath := "/api/v1/runs/" + id + "/events"

	// Joined: events and every participant's state.
	got := r.get(d1, id)
	if len(got.part(r.admin.ID).States) != 1 || len(got.part(r.anna.ID).States) != 1 {
		t.Fatalf("joined creator: %+v", got.Participants)
	}
	if len(r.events(a1, id, 0)) < 3 {
		t.Fatal("joined participant must read events")
	}
	if l := list(a1); len(l) != 1 || len(l[0].part(r.admin.ID).States) != 1 {
		t.Fatalf("joined list: %+v", l)
	}

	// Invited (bob), declined (carol): run without states, events refused with 403.
	for who, d := range map[string]dev{"invited": b1, "declined": c1} {
		noStates(who, r.get(d, id))
		if l := list(d); len(l) != 1 {
			t.Fatalf("%s list: %+v", who, l)
		} else {
			noStates(who+" list", l[0])
		}
		wantStatus(t, r.api(d, "GET", eventsPath, nil), 403, "forbidden")
	}
	// The membership answers of a user who is not joined afterwards carry no states either.
	noStates("declined (decline again)", decode[runResp](t, r.post(c1, id, "decline", nil)))

	// Left (anna): the state she wrote stays, but she no longer reads states or events.
	left := decode[runResp](t, r.post(a1, id, "leave", nil))
	noStates("left (leave response)", left)
	noStates("left", r.get(a1, id))
	wantStatus(t, r.api(a1, "GET", eventsPath, nil), 403, "forbidden")

	// Joining again restores access (full view in the response and on GET).
	if j := r.mustJoin(a1, id); len(j.part(r.admin.ID).States) != 1 {
		t.Fatalf("rejoined: %+v", j.Participants)
	}
	if len(r.events(a1, id, 0)) < 3 {
		t.Fatal("rejoined participant must read events")
	}

	// A stranger is still 404.
	wantStatus(t, r.api(e1, "GET", eventsPath, nil), 404, "not_found")
	wantStatus(t, r.api(e1, "GET", "/api/v1/runs/"+id, nil), 404, "not_found")
}

func TestRunAuthzWSRunUpdatedStatesOnlyForJoined(t *testing.T) {
	r := newRunEnv(t)
	d1, a1, b1 := r.device(r.admin.ID, "Desktop"), r.device(r.anna.ID, "Anna PC"), r.device(r.bob.ID, "Bob PC")
	run := r.create(d1, "Authz WS", r.anna.ID, r.bob.ID)
	id := run.RunID
	r.mustJoin(a1, id)
	wantStatus(t, r.putState(d1, id, 0, map[string]any{"team": []any{"secret"}}), 200, "")

	// Hello catch-up: the joined get the state, the invited user the run without states.
	wa, wb := r.dial(a1), r.dial(b1)
	gotA := runUpdated(wa.awaitWhere("run_updated", isRunUpdated(id, "starting")))
	if len(gotA.part(r.admin.ID).States) != 1 {
		t.Fatalf("joined catch-up lacks the state: %+v", gotA.Participants)
	}
	gotB := runUpdated(wb.awaitWhere("run_updated", isRunUpdated(id, "starting")))
	if len(gotB.Participants) != 3 || gotB.part(r.admin.ID).GameID == nil {
		t.Fatalf("invited catch-up lost title/game/participants: %+v", gotB)
	}
	for _, p := range gotB.Participants {
		if len(p.States) != 0 {
			t.Fatalf("invited user got the state of %s in the catch-up", p.UserID)
		}
	}

	// Live run_updated (status change): same split.
	wantStatus(t, r.status(d1, id, "active"), 200, "")
	gotA = runUpdated(wa.awaitWhere("run_updated", isRunUpdated(id, "active")))
	gotB = runUpdated(wb.awaitWhere("run_updated", isRunUpdated(id, "active")))
	if len(gotA.part(r.admin.ID).States) != 1 {
		t.Fatalf("joined live update lacks the state: %+v", gotA.Participants)
	}
	for _, p := range gotB.Participants {
		if len(p.States) != 0 {
			t.Fatalf("invited user got the state of %s in run_updated", p.UserID)
		}
	}
	// And no run_event / run_state reaches the invited user.
	wb.quiet("run_event", "run_state")
}
