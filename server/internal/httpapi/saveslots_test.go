package httpapi

import (
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"testing"
)

func (s *sessEnv) uploadSave(d dev, slot string, expected int, data []byte) *httptest.ResponseRecorder {
	s.t.Helper()
	return s.do("POST", s.savePath(slot, "/upload"), nil, opt{token: d.tok, data: data, ctype: "application/octet-stream", raw: true,
		header: map[string]string{"X-FrameBeam-Expected-Revision": fmt.Sprint(expected), "X-FrameBeam-Content-SHA256": hexSHA(data)}})
}

func (s *sessEnv) deleteSlot(d dev, slot, confirm string) *httptest.ResponseRecorder {
	path := s.savePath(slot, "")
	if confirm != "" {
		path += "?confirm_revision=" + confirm
	}
	return s.do("DELETE", path, nil, opt{token: d.tok, raw: true}) // raw: also sends requests the spec forbids
}

// awaitReason waits for the next save_updated push with the given reason and returns its payload.
func awaitReason(c *wsClient, reason string) map[string]any {
	m := c.awaitWhere("save_updated", func(m wsMsg) bool {
		var q struct{ Reason string }
		json.Unmarshal(m.Payload, &q)
		return q.Reason == reason
	})
	var raw map[string]any
	json.Unmarshal(m.Payload, &raw)
	return raw
}

func TestRenameSaveSlotAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	d1, d2 := s.device(s.admin.ID, "Desktop"), s.device(s.admin.ID, "Laptop")
	wantStatus(t, s.putSave(d1, "default", 0, []byte("d"), "checkpoint"), 200, "")
	wantStatus(t, s.putSave(d1, "run", 0, []byte("one"), "final_session_end"), 200, "")
	wantStatus(t, s.putSave(d1, "run", 1, []byte("two"), "checkpoint"), 200, "")
	wantStatus(t, s.api(d1, "POST", s.savePath("run", "/snapshots"), map[string]any{"label": "keep"}), 201, "")
	rename := func(d dev, slot, name string) *httptest.ResponseRecorder {
		return s.do("POST", s.savePath(slot, "/rename"), map[string]any{"name": name}, opt{token: d.tok, raw: true}) // raw: also invalid names
	}
	histBefore := decode[struct{ Versions []versionResp }](t, s.api(d1, "GET", s.savePath("run", "/history"), nil)).Versions
	w1, w2, wb := s.dial(d1), s.dial(d2), s.dial(s.device(s.anna.ID, "Anna PC"))
	w1.settle()
	w2.settle()
	wb.settle()

	wantStatus(t, s.do("POST", s.savePath("run", "/rename"), map[string]any{"name": "x"}, opt{}), 401, "")
	wantStatus(t, rename(d1, "nothere", "x"), 404, "not_found")
	wantStatus(t, rename(s.device(s.anna.ID, "Anna PC 2"), "run", "x"), 404, "not_found") // another user sees no slot
	wantStatus(t, rename(d1, "default", "main"), 409, "save_slot_protected")
	wantStatus(t, rename(d1, "run", "default"), 409, "save_slot_exists")
	for _, bad := range []string{"", "Bad Name", "con", "nul", "com1", "lpt9", "run", "../x", "0123456789012345678901234567890123"} {
		wantStatus(t, rename(d1, "run", bad), 400, "bad_request")
	}
	wantStatus(t, s.do("POST", s.savePath("run", "/rename"), nil, opt{token: d1.tok, raw: true}), 400, "")

	rec := rename(d1, "run", "speedrun")
	wantStatus(t, rec, 200, "")
	sl := decode[slotResp](t, rec)
	if sl.Slot != "speedrun" || sl.Current.Revision != 2 || sl.Current.Sha256 != hexSHA([]byte("two")) {
		t.Fatalf("%+v", sl)
	}
	// The full history moved with the slot; the old name is gone; content is still readable.
	h := decode[struct{ Versions []versionResp }](t, s.api(d1, "GET", s.savePath("speedrun", "/history"), nil)).Versions
	if len(h) != len(histBefore) || len(h) < 2 {
		t.Fatalf("history changed: before %+v after %+v", histBefore, h)
	}
	wantStatus(t, s.api(d1, "GET", s.savePath("run", ""), nil), 404, "not_found")
	rec = s.api(d1, "GET", s.savePath("speedrun", "/content"), nil)
	wantStatus(t, rec, 200, "")
	if rec.Body.String() != "two" {
		t.Fatalf("content %q", rec.Body.String())
	}
	for _, v := range h {
		wantStatus(t, s.api(d1, "GET", s.savePath("speedrun", fmt.Sprintf("/history/%d/content", v.Version)), nil), 200, "")
	}
	// A new upload keeps working under the new name; the old name starts fresh.
	wantStatus(t, s.putSave(d1, "speedrun", 2, []byte("three"), "checkpoint"), 200, "")
	wantStatus(t, s.putSave(d1, "run", 0, []byte("fresh"), "checkpoint"), 200, "")

	// Device 2 is told (reason renamed, slot = new name, previous_slot = old); the origin and the other user are not.
	raw := awaitReason(w2, "renamed")
	if raw["slot"] != "speedrun" || raw["previous_slot"] != "run" || raw["revision"].(float64) != 2 || raw["sha256"] != hexSHA([]byte("two")) {
		t.Fatalf("%v", raw)
	}
	wb.quiet("save_updated")

	wantStatus(t, rename(d1, "speedrun", "speedrun"), 400, "bad_request")
	wantStatus(t, rename(d1, "speedrun", "run"), 409, "save_slot_exists")

	// Blocked while a Session of the game runs.
	sess := s.publish(d1, "private")
	wantStatus(t, rename(d2, "speedrun", "again"), 409, "session_running")
	wantStatus(t, s.api(d1, "DELETE", "/api/v1/sessions/"+sess.SessionID, nil), 204, "")
	wantStatus(t, rename(d2, "speedrun", "again"), 200, "")
}

func TestRenameSaveSlotKeepsOpenConflict(t *testing.T) {
	s := newSessEnv(t, nil)
	d1, d2 := s.device(s.admin.ID, "Desktop"), s.device(s.admin.ID, "Laptop")
	wantStatus(t, s.putSave(d1, "run", 0, []byte("one"), "checkpoint"), 200, "")
	wantStatus(t, s.putSave(d2, "run", 0, []byte("zero"), "checkpoint"), 409, "save_conflict")
	wantStatus(t, s.api(d1, "POST", s.savePath("run", "/rename"), map[string]any{"name": "moved"}), 200, "")
	sl := decode[slotResp](t, s.api(d1, "GET", s.savePath("moved", ""), nil))
	if len(sl.OpenConflicts) != 1 {
		t.Fatalf("%+v", sl)
	}
}

func TestDeleteSaveSlotAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	d1, d2 := s.device(s.admin.ID, "Desktop"), s.device(s.admin.ID, "Laptop")
	wantStatus(t, s.putSave(d1, "default", 0, []byte("d"), "checkpoint"), 200, "")
	wantStatus(t, s.putSave(d1, "run", 0, []byte("one"), "final_session_end"), 200, "")
	wantStatus(t, s.putSave(d1, "run", 1, []byte("two"), "checkpoint"), 200, "")
	w2, wb := s.dial(d2), s.dial(s.device(s.anna.ID, "Anna PC"))
	w2.settle()
	wb.settle()

	wantStatus(t, s.do("DELETE", s.savePath("run", "?confirm_revision=2"), nil, opt{}), 401, "")
	wantStatus(t, s.deleteSlot(d1, "nothere", "1"), 404, "not_found")
	wantStatus(t, s.deleteSlot(s.device(s.anna.ID, "Anna PC 2"), "run", "2"), 404, "not_found")
	wantStatus(t, s.deleteSlot(d1, "default", "1"), 409, "save_slot_protected")
	// Confirmation is mandatory and must match the current checkpoint.
	wantStatus(t, s.deleteSlot(d1, "run", ""), 400, "")
	wantStatus(t, s.deleteSlot(d1, "run", "abc"), 400, "")
	wantStatus(t, s.deleteSlot(d1, "run", "0"), 400, "")
	wantStatus(t, s.deleteSlot(d1, "run", "1"), 409, "save_conflict_stale")
	wantStatus(t, s.deleteSlot(d1, "run", "3"), 409, "save_conflict_stale")
	wantStatus(t, s.api(d1, "GET", s.savePath("run", ""), nil), 200, "") // still there

	// Not while a Session of the game runs.
	sess := s.publish(d1, "private")
	wantStatus(t, s.deleteSlot(d2, "run", "2"), 409, "session_running")
	wantStatus(t, s.api(d1, "DELETE", "/api/v1/sessions/"+sess.SessionID, nil), 204, "")

	// Not with an open conflict.
	wantStatus(t, s.putSave(d2, "run", 0, []byte("zero"), "checkpoint"), 409, "save_conflict")
	wantStatus(t, s.deleteSlot(d1, "run", "2"), 409, "conflict")
	wantStatus(t, s.putSave(d1, "other", 0, []byte("o"), "checkpoint"), 200, "")

	// Resolve the conflict (use the Hub version), then delete.
	sl := decode[slotResp](t, s.api(d1, "GET", s.savePath("run", ""), nil))
	wantStatus(t, s.api(d1, "POST", s.savePath("run", "/conflicts/"+sl.OpenConflicts[0].ID+"/resolve"),
		map[string]any{"resolution": "use_hub", "expected_revision": 2}), 200, "")
	w2.settle()
	wantStatus(t, s.deleteSlot(d1, "run", "2"), 204, "")
	wantStatus(t, s.api(d1, "GET", s.savePath("run", ""), nil), 404, "not_found")
	wantStatus(t, s.deleteSlot(d1, "run", "2"), 404, "not_found")
	// Other slots are untouched.
	wantStatus(t, s.api(d1, "GET", s.savePath("default", ""), nil), 200, "")
	wantStatus(t, s.api(d1, "GET", s.savePath("other", ""), nil), 200, "")

	// Device 2 is told with reason deleted; the other user is not.
	raw := awaitReason(w2, "deleted")
	if raw["slot"] != "run" || raw["revision"].(float64) != 2 || raw["game_id"] != s.game.ID {
		t.Fatalf("%v", raw)
	}
	wb.quiet("save_updated")

	// The name can be used again from scratch (revision 1).
	rec := s.putSave(d1, "run", 0, []byte("again"), "checkpoint")
	wantStatus(t, rec, 200, "")
	if r := decode[slotResp](t, rec).Current.Revision; r != 1 {
		t.Fatalf("revision %d", r)
	}
}

func TestUploadSaveBlockedBySession(t *testing.T) {
	s := newSessEnv(t, nil)
	d1, d2 := s.device(s.admin.ID, "Desktop"), s.device(s.admin.ID, "Laptop")
	anna := s.device(s.anna.ID, "Anna PC")
	wantStatus(t, s.uploadSave(d1, "default", 0, []byte("one")), 200, "")
	sess := s.publish(d1, "hub_users")
	// The user's other device is blocked too; nothing changed.
	wantStatus(t, s.uploadSave(d2, "default", 1, []byte("two")), 409, "session_running")
	wantStatus(t, s.uploadSave(d1, "newslot", 0, []byte("two")), 409, "session_running")
	if r := decode[slotResp](t, s.api(d1, "GET", s.savePath("default", ""), nil)).Current.Revision; r != 1 {
		t.Fatalf("revision %d", r)
	}
	// Another user viewing the Session is not blocked from uploading their own save.
	s.mustJoin(anna, sess.SessionID)
	wantStatus(t, s.uploadSave(anna, "default", 0, []byte("annas")), 200, "")
	// After the Session ended the upload works again.
	wantStatus(t, s.api(d1, "DELETE", "/api/v1/sessions/"+sess.SessionID, nil), 204, "")
	wantStatus(t, s.uploadSave(d2, "default", 1, []byte("two")), 200, "")
}
