package httpapi

import (
	"bytes"
	"image"
	"image/png"
	"net/http/httptest"
	"strconv"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Screenshots over REST (0.19 package 5). Pictures are generated in code.

type shotT struct {
	ID        string  `json:"id"`
	GameID    string  `json:"game_id"`
	RunID     *string `json:"run_id"`
	UserID    string  `json:"user_id"`
	UserName  string  `json:"user_name"`
	CreatedAt string  `json:"created_at"`
	Width     int     `json:"width"`
	Height    int     `json:"height"`
	SizeBytes int64   `json:"size_bytes"`
}

type shotListT struct {
	Screenshots []shotT `json:"screenshots"`
	TotalBytes  int64   `json:"total_bytes"`
}

func testPNG(t *testing.T, w, h int) []byte {
	t.Helper()
	var b bytes.Buffer
	if err := png.Encode(&b, image.NewRGBA(image.Rect(0, 0, w, h))); err != nil {
		t.Fatal(err)
	}
	return b.Bytes()
}

func (s *sessEnv) shotUpload(d dev, game, query string, data []byte) *httptest.ResponseRecorder {
	s.t.Helper()
	path := "/api/v1/games/" + game + "/screenshots"
	if query != "" {
		path += "?" + query
	}
	return s.do("POST", path, nil, opt{token: d.tok, data: data, ctype: "image/png"})
}

func TestScreenshotsAPI(t *testing.T) {
	r := newRunEnv(t)
	anna, bob := r.device(r.anna.ID, "Annas PC"), r.device(r.bob.ID, "Bobs PC")
	carol := r.device(r.carol.ID, "Carols PC")
	gid := r.game.ID
	const unknown = "00000000-0000-4000-8000-000000000000"
	data := testPNG(t, 256, 192)

	wantStatus(t, r.do("POST", "/api/v1/games/"+gid+"/screenshots", nil, opt{data: data, ctype: "image/png"}), 401, "")
	wantStatus(t, r.do("GET", "/api/v1/games/"+gid+"/screenshots", nil, opt{}), 401, "")
	wantStatus(t, r.do("DELETE", "/api/v1/screenshots/"+unknown, nil, opt{}), 401, "")

	// Upload, with width and height from the PNG.
	rec := r.shotUpload(anna, gid, "", data)
	wantStatus(t, rec, 201, "")
	a := decode[shotT](t, rec)
	if a.Width != 256 || a.Height != 192 || a.SizeBytes != int64(len(data)) || a.RunID != nil || a.UserID != r.anna.ID ||
		a.UserName == "" || a.GameID != gid || a.CreatedAt == "" {
		t.Fatalf("%+v", a)
	}

	// Invalid images, unknown game, too large.
	wantStatus(t, r.shotUpload(anna, gid, "", []byte("not a png")), 400, "invalid_image")
	wantStatus(t, r.shotUpload(anna, unknown, "", data), 404, "not_found")
	big := append(append([]byte{}, data...), bytes.Repeat([]byte{0}, hub.MaxScreenshotBytes)...)
	wantStatus(t, r.do("POST", "/api/v1/games/"+gid+"/screenshots", nil, opt{token: anna.tok, data: big, ctype: "image/png"}), 413, "payload_too_large")

	// List: own screenshots only, with the total.
	r.clk.Advance(2e9)
	b := decode[shotT](t, r.shotUpload(anna, gid, "", testPNG(t, 8, 8)))
	list := decode[shotListT](t, r.api(anna, "GET", "/api/v1/games/"+gid+"/screenshots", nil))
	if len(list.Screenshots) != 2 || list.Screenshots[0].ID != b.ID || list.Screenshots[1].ID != a.ID || list.TotalBytes != a.SizeBytes+b.SizeBytes {
		t.Fatalf("%+v", list)
	}
	empty := decode[shotListT](t, r.api(bob, "GET", "/api/v1/games/"+gid+"/screenshots", nil))
	if empty.Screenshots == nil || len(empty.Screenshots) != 0 || empty.TotalBytes != 0 {
		t.Fatalf("%+v", empty)
	}
	wantStatus(t, r.api(anna, "GET", "/api/v1/games/"+unknown+"/screenshots", nil), 404, "not_found")

	// Content: owner only while untagged.
	rec = r.api(anna, "GET", "/api/v1/screenshots/"+a.ID+"/content", nil)
	wantStatus(t, rec, 200, "")
	if !bytes.Equal(rec.Body.Bytes(), data) || rec.Header().Get("Content-Type") != "image/png" ||
		rec.Header().Get("Content-Length") != strconv.Itoa(len(data)) {
		t.Fatalf("content %q len %s", rec.Header().Get("Content-Type"), rec.Header().Get("Content-Length"))
	}
	wantStatus(t, r.api(bob, "GET", "/api/v1/screenshots/"+a.ID+"/content", nil), 404, "not_found")
	wantStatus(t, r.api(anna, "GET", "/api/v1/screenshots/"+unknown+"/content", nil), 404, "not_found")

	// Run tagging: anna creates the run, bob joins, carol is not a member.
	run := r.create(anna, "Run", r.bob.ID)
	wantStatus(t, r.shotUpload(bob, gid, "run_id="+run.RunID, data), 404, "not_found") // invited, not joined
	r.mustJoin(bob, run.RunID)
	wantStatus(t, r.shotUpload(carol, gid, "run_id="+run.RunID, data), 404, "not_found")
	wantStatus(t, r.shotUpload(anna, gid, "run_id="+unknown, data), 404, "not_found")
	ta := decode[shotT](t, r.shotUpload(anna, gid, "run_id="+run.RunID, data))
	r.clk.Advance(2e9)
	tb := decode[shotT](t, r.shotUpload(bob, gid, "run_id="+run.RunID, testPNG(t, 3, 3)))
	if ta.RunID == nil || *ta.RunID != run.RunID || tb.RunID == nil {
		t.Fatalf("%+v %+v", ta, tb)
	}
	rl := decode[shotListT](t, r.api(bob, "GET", "/api/v1/runs/"+run.RunID+"/screenshots", nil))
	if len(rl.Screenshots) != 2 || rl.Screenshots[0].ID != tb.ID || rl.Screenshots[1].ID != ta.ID || rl.TotalBytes != ta.SizeBytes+tb.SizeBytes {
		t.Fatalf("%+v", rl)
	}
	rec = r.api(bob, "GET", "/api/v1/screenshots/"+ta.ID+"/content", nil) // member of the run
	wantStatus(t, rec, 200, "")
	if !bytes.Equal(rec.Body.Bytes(), data) {
		t.Fatal("run member content differs")
	}
	wantStatus(t, r.api(carol, "GET", "/api/v1/runs/"+run.RunID+"/screenshots", nil), 404, "not_found")
	wantStatus(t, r.api(carol, "GET", "/api/v1/screenshots/"+ta.ID+"/content", nil), 404, "not_found")
	wantStatus(t, r.api(anna, "GET", "/api/v1/runs/"+unknown+"/screenshots", nil), 404, "not_found")
	// The own list holds the caller's screenshots of the game only.
	if own := decode[shotListT](t, r.api(bob, "GET", "/api/v1/games/"+gid+"/screenshots", nil)); len(own.Screenshots) != 1 || own.Screenshots[0].ID != tb.ID {
		t.Fatalf("%+v", own)
	}

	// Delete: owner only.
	wantStatus(t, r.api(bob, "DELETE", "/api/v1/screenshots/"+ta.ID, nil), 404, "not_found")
	wantStatus(t, r.api(anna, "DELETE", "/api/v1/screenshots/"+ta.ID, nil), 204, "")
	wantStatus(t, r.api(anna, "DELETE", "/api/v1/screenshots/"+ta.ID, nil), 404, "not_found")
	wantStatus(t, r.api(bob, "GET", "/api/v1/screenshots/"+ta.ID+"/content", nil), 404, "not_found")
	if rl = decode[shotListT](t, r.api(bob, "GET", "/api/v1/runs/"+run.RunID+"/screenshots", nil)); len(rl.Screenshots) != 1 {
		t.Fatalf("%+v", rl)
	}

	// Deleting the run keeps the screenshots, untagged.
	wantStatus(t, r.do("DELETE", "/api/v1/admin/runs/"+run.RunID, nil, opt{token: r.device(r.admin.ID, "Admin PC").tok}), 204, "")
	own := decode[shotListT](t, r.api(bob, "GET", "/api/v1/games/"+gid+"/screenshots", nil))
	if len(own.Screenshots) != 1 || own.Screenshots[0].RunID != nil {
		t.Fatalf("%+v", own)
	}
}
