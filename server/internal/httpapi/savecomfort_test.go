package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func (s *sessEnv) putSave(d dev, slot string, base int, data []byte, reason string) *httptest.ResponseRecorder {
	s.t.Helper()
	return s.do("PUT", "/api/v1/games/"+s.game.ID+"/saves/"+slot, nil, opt{token: d.tok, data: data, ctype: "application/octet-stream",
		header: map[string]string{"X-FrameBeam-Base-Revision": fmt.Sprint(base), "X-FrameBeam-Content-SHA256": hexSHA(data), "X-FrameBeam-Sync-Reason": reason}})
}

func (s *sessEnv) savePath(slot, rest string) string {
	return "/api/v1/games/" + s.game.ID + "/saves/" + slot + rest
}

type versionResp struct {
	Version  int     `json:"version"`
	Revision int     `json:"revision"`
	Reason   string  `json:"reason"`
	Label    *string `json:"label"`
	DeviceID string  `json:"device_id"`
}

func TestRestoreAndSnapshotAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	d1, d2 := s.device(s.admin.ID, "Desktop"), s.device(s.admin.ID, "Laptop")
	wantStatus(t, s.putSave(d1, "default", 0, []byte("one"), "final_session_end"), 200, "")
	wantStatus(t, s.putSave(d1, "default", 1, []byte("two"), "checkpoint"), 200, "")
	restore := func(d dev, slot string, version, expected int) *httptest.ResponseRecorder {
		return s.api(d, "POST", s.savePath(slot, fmt.Sprintf("/history/%d/restore", version)), map[string]any{"expected_revision": expected})
	}

	wantStatus(t, restore(d2, "default", 1, 1), 409, "save_conflict_stale")
	wantStatus(t, restore(d2, "default", 99, 2), 404, "not_found")
	wantStatus(t, restore(d2, "nothere", 1, 1), 404, "not_found")
	wantStatus(t, s.do("POST", s.savePath("default", "/history/1/restore"), map[string]any{"expected_revision": 2}, opt{}), 401, "")
	// Another user's device sees no slot.
	wantStatus(t, restore(s.device(s.anna.ID, "Anna PC"), "default", 1, 2), 404, "not_found")

	rec := restore(d2, "default", 1, 2)
	wantStatus(t, rec, 200, "")
	sl := decode[slotResp](t, rec)
	if sl.Current.Revision != 3 || sl.Current.Reason != "restore" || sl.Current.DeviceID != d2.id || sl.Current.DeviceName != "Laptop" {
		t.Fatalf("%+v", sl.Current)
	}
	h := decode[struct{ Versions []versionResp }](t, s.api(d1, "GET", s.savePath("default", "/history"), nil)).Versions
	if len(h) != 2 || h[0].Reason != "before_restore" || h[0].Revision != 2 || h[0].Label != nil {
		t.Fatalf("%+v", h)
	}

	// Snapshots: label trimmed, 201 with the version; bad label 400; no checkpoint 404.
	snap := func(slot string, body any) *httptest.ResponseRecorder {
		return s.api(d1, "POST", s.savePath(slot, "/snapshots"), body)
	}
	rec = snap("default", map[string]any{"label": "  boss fight  "})
	wantStatus(t, rec, 201, "")
	v := decode[versionResp](t, rec)
	if v.Reason != "manual_snapshot" || v.Label == nil || *v.Label != "boss fight" || v.Revision != 3 || v.Version != 3 {
		t.Fatalf("%+v", v)
	}
	rec = snap("default", nil)
	wantStatus(t, rec, 201, "")
	if v := decode[versionResp](t, rec); v.Label != nil {
		t.Fatalf("%+v", v)
	}
	// The spec limits the label to 64 characters, so the over-long request is sent unvalidated.
	wantStatus(t, s.do("POST", s.savePath("default", "/snapshots"), map[string]any{"label": strings.Repeat("x", 65)}, opt{token: d1.tok, raw: true}), 400, "bad_request")
	wantStatus(t, snap("nothere", map[string]any{}), 404, "not_found")
	h = decode[struct{ Versions []versionResp }](t, s.api(d1, "GET", s.savePath("default", "/history"), nil)).Versions
	if len(h) != 4 || h[1].Label == nil || *h[1].Label != "boss fight" {
		t.Fatalf("%+v", h)
	}
}

func TestSaveUpdatedPush(t *testing.T) {
	s := newSessEnv(t, nil)
	a1, a2, a3 := s.device(s.admin.ID, "Desktop"), s.device(s.admin.ID, "Laptop"), s.device(s.admin.ID, "Deck")
	b1 := s.device(s.anna.ID, "Anna PC")
	w1, w2, wb := s.dial(a1), s.dial(a2), s.dial(b1)
	_ = a3 // paired but offline: gets nothing

	type pushed struct {
		GameID     string `json:"game_id"`
		Slot       string `json:"slot"`
		Revision   int    `json:"revision"`
		SHA256     string `json:"sha256"`
		DeviceID   string `json:"device_id"`
		DeviceName string `json:"device_name"`
		Reason     string `json:"reason"`
	}
	await := func(c *wsClient) pushed {
		t.Helper()
		var p pushed
		if err := json.Unmarshal(c.await("save_updated").Payload, &p); err != nil {
			t.Fatal(err)
		}
		return p
	}
	settleAll := func() { w1.settle(); w2.settle(); wb.settle() }

	// Upload on device 1: device 2 is told, device 1 (origin) and the other user are not.
	settleAll()
	wantStatus(t, s.putSave(a1, "default", 0, []byte("one"), "checkpoint"), 200, "")
	p := await(w2)
	if p.GameID != s.game.ID || p.Slot != "default" || p.Revision != 1 || p.SHA256 != hexSHA([]byte("one")) || p.DeviceID != a1.id ||
		p.DeviceName != "Desktop" || p.Reason != "checkpoint" {
		t.Fatalf("%+v", p)
	}
	w1.quiet("save_updated")
	wb.quiet("save_updated")

	// An idempotent retry and a conflict change nothing and push nothing.
	wantStatus(t, s.putSave(a1, "default", 0, []byte("one"), "checkpoint"), 200, "")
	wantStatus(t, s.putSave(a2, "default", 0, []byte("zero"), "checkpoint"), 409, "save_conflict")
	w1.quiet("save_updated")
	w2.quiet("save_updated")

	// Restore by device 2: device 1 is told with reason restore; device 2 is not.
	wantStatus(t, s.putSave(a1, "default", 1, []byte("two"), "final_session_end"), 200, "")
	await(w2)
	settleAll()
	wantStatus(t, s.api(a2, "POST", s.savePath("default", "/history/2/restore"), map[string]any{"expected_revision": 2}), 200, "")
	p = await(w1)
	if p.Revision != 3 || p.Reason != "restore" || p.DeviceID != a2.id || p.DeviceName != "Laptop" || p.SHA256 != hexSHA([]byte("two")) { // v2 is the session end of Rev 2 (v1 is the secured conflict upload)
		t.Fatalf("%+v", p)
	}
	w2.quiet("save_updated")
	wb.quiet("save_updated")

	// Conflict resolution use_local by device 1 pushes to device 2 (and the uploader) with reason conflict_resolution.
	rec := s.putSave(a2, "default", 1, []byte("local"), "checkpoint") // stale base 1 (current is Rev 3)
	wantStatus(t, rec, 409, "save_conflict")
	cid := decode[struct {
		Conflict struct{ ID string }
	}](t, rec).Conflict.ID
	settleAll()
	wantStatus(t, s.api(a1, "POST", s.savePath("default", "/conflicts/"+cid+"/resolve"),
		map[string]any{"resolution": "use_local", "expected_revision": 3}), 200, "")
	p = await(w2)
	if p.Revision != 4 || p.Reason != "conflict_resolution" || p.DeviceID != a2.id || p.SHA256 != hexSHA([]byte("local")) {
		t.Fatalf("%+v", p)
	}
	w1.quiet("save_updated")
	wb.quiet("save_updated")

	// use_hub changes no checkpoint: no push.
	rec = s.putSave(a1, "default", 1, []byte("again"), "checkpoint")
	wantStatus(t, rec, 409, "save_conflict")
	cid = decode[struct {
		Conflict struct{ ID string }
	}](t, rec).Conflict.ID
	settleAll()
	wantStatus(t, s.api(a2, "POST", s.savePath("default", "/conflicts/"+cid+"/resolve"),
		map[string]any{"resolution": "use_hub", "expected_revision": 4}), 200, "")
	w1.quiet("save_updated")
	w2.quiet("save_updated")

	// A change made in the web interface goes to every connected device of the user, with the nil device.
	settleAll()
	if _, err := s.svc.RestoreSaveVersion(context.Background(), hub.RestoreInput{UserID: s.admin.ID, DeviceID: hub.WebDeviceID(s.admin.ID),
		GameID: s.game.ID, Slot: "default", Version: 1, ExpectedRevision: 4}); err != nil {
		t.Fatal(err)
	}
	for _, w := range []*wsClient{w1, w2} {
		p = await(w)
		if p.Revision != 5 || p.Reason != "restore" || p.DeviceID != "00000000-0000-0000-0000-000000000000" || p.DeviceName != "Hub web interface" {
			t.Fatalf("%+v", p)
		}
	}
	wb.quiet("save_updated")
}

func TestCoresIndexEndpoints(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", bytes.Repeat([]byte("core!"), 100))
	s := newSessEnv(t, func(o *hub.Options) { src.Apply(o) })
	d := s.device(s.anna.ID, "Anna PC")

	// No verified index yet.
	wantStatus(t, s.do("GET", "/api/v1/cores/index", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", "/api/v1/cores/index.sig", nil, opt{token: d.tok}), 404, "core_package_not_found")
	wantStatus(t, s.do("GET", "/api/v1/cores/index", nil, opt{}), 401, "")
	wantStatus(t, s.do("GET", "/api/v1/cores/index.sig", nil, opt{}), 401, "")

	// A tampered index is not stored.
	src.Tamper()
	if _, err := s.svc.SyncCores(context.Background()); err == nil {
		t.Fatal("tampered index accepted")
	}
	wantStatus(t, s.do("GET", "/api/v1/cores/index", nil, opt{token: d.tok}), 404, "core_package_not_found")
	src.Publish(t)

	if _, err := s.svc.SyncCores(context.Background()); err != nil {
		t.Fatal(err)
	}
	idx, sig := src.Index()
	rec := s.do("GET", "/api/v1/cores/index", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	if !bytes.Equal(rec.Body.Bytes(), idx) || rec.Header().Get("Content-Type") != "application/json" {
		t.Fatalf("index differs (%d bytes, want %d), type %q", rec.Body.Len(), len(idx), rec.Header().Get("Content-Type"))
	}
	rec = s.do("GET", "/api/v1/cores/index.sig", nil, opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	if !bytes.Equal(rec.Body.Bytes(), sig) || !strings.HasPrefix(rec.Header().Get("Content-Type"), "text/plain") {
		t.Fatalf("signature differs, type %q", rec.Header().Get("Content-Type"))
	}
	// The files are served from the data directory, not from memory: they survive in place.
	if b, err := os.ReadFile(filepath.Join(s.svc.DataDir(), "cores", "index.json")); err != nil || !bytes.Equal(b, idx) {
		t.Fatalf("index.json in the data dir: %v", err)
	}
	// "index" is not a core id: the package routes still work.
	wantStatus(t, s.do("GET", "/api/v1/cores/melonds_ds/packages/1.4.0/linux-x64", nil, opt{token: d.tok}), 200, "")
	wantStatus(t, s.do("GET", "/api/v1/cores/index/packages/1.4.0/linux-x64", nil, opt{token: d.tok}), 404, "core_package_not_found")
}

func TestHandshakeAndHelloAckAdvertiseSavesV2(t *testing.T) {
	s := newSessEnv(t, nil)
	d := s.device(s.admin.ID, "Desktop")
	rec := s.do("POST", "/api/v1/handshake", handshakeBody(1, 1), opt{token: d.tok})
	wantStatus(t, rec, 200, "")
	f := strings.Join(decode[struct{ Features []string }](t, rec).Features, ",")
	if !strings.Contains(f, "saves_v2") || !strings.Contains(f, "saves_v3") || !strings.Contains(f, "saves_v4") || !strings.Contains(f, "cores_index_v1") {
		t.Fatal(f)
	}
}

func TestDeleteSaveSnapshotAPI(t *testing.T) {
	s := newSessEnv(t, nil)
	d := s.device(s.admin.ID, "Desktop")
	wantStatus(t, s.putSave(d, "default", 0, []byte("one"), "final_session_end"), 200, "")
	wantStatus(t, s.putSave(d, "default", 1, []byte("two"), "checkpoint"), 200, "")
	rec := s.api(d, "POST", s.savePath("default", "/snapshots"), map[string]any{"label": "keep me"})
	wantStatus(t, rec, 201, "")
	snap := decode[versionResp](t, rec).Version
	vpath := func(v int) string { return s.savePath("default", fmt.Sprintf("/history/%d", v)) }

	wantStatus(t, s.do("DELETE", vpath(snap), nil, opt{}), 401, "")
	// Another user's device sees no slot.
	wantStatus(t, s.api(s.device(s.anna.ID, "Anna PC"), "DELETE", vpath(snap), nil), 404, "not_found")
	// The auto-captured version is not a snapshot.
	wantStatus(t, s.api(d, "DELETE", vpath(1), nil), 409, "save_not_snapshot")
	wantStatus(t, s.api(d, "DELETE", vpath(99), nil), 404, "not_found")
	wantStatus(t, s.api(d, "DELETE", s.savePath("nothere", "/history/1"), nil), 404, "not_found")

	wantStatus(t, s.api(d, "DELETE", vpath(snap), nil), 204, "")
	wantStatus(t, s.api(d, "DELETE", vpath(snap), nil), 404, "not_found")
	h := decode[struct{ Versions []versionResp }](t, s.api(d, "GET", s.savePath("default", "/history"), nil)).Versions
	for _, v := range h {
		if v.Version == snap {
			t.Fatalf("snapshot still listed: %+v", h)
		}
	}
	sl := decode[slotResp](t, s.api(d, "GET", s.savePath("default", ""), nil))
	if sl.Current.Revision != 2 {
		t.Fatalf("%+v", sl.Current)
	}
}
