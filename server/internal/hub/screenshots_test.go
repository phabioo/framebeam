package hub_test

import (
	"bytes"
	"errors"
	"image"
	"image/color"
	"image/png"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Screenshots (0.19 package 5). All pictures are generated in code.

func pngBytes(t *testing.T, w, h int) []byte {
	t.Helper()
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	img.Set(0, 0, color.RGBA{R: 200, A: 255})
	var b bytes.Buffer
	if err := png.Encode(&b, img); err != nil {
		t.Fatal(err)
	}
	return b.Bytes()
}

func (e *playEnv) shot(userID, gameID, runID string, data []byte) hub.Screenshot {
	e.t.Helper()
	sc, err := e.svc.AddScreenshot(ctx, userID, gameID, runID, bytes.NewReader(data))
	if err != nil {
		e.t.Fatal(err)
	}
	return sc
}

func (e *playEnv) shotFile(sc hub.Screenshot) string {
	return filepath.Join(e.dir, "screenshots", sc.UserID, sc.GameID, sc.ID+".png")
}

func mustExist(t *testing.T, p string, want bool) {
	t.Helper()
	_, err := os.Stat(p)
	if (err == nil) != want {
		t.Fatalf("%s exists=%v, want %v (%v)", p, err == nil, want, err)
	}
}

func TestScreenshotAddListOpenDelete(t *testing.T) {
	e := newPlayEnv(t, nil)
	data := pngBytes(t, 256, 192)
	a := e.shot(e.admin.ID, e.g1.ID, "", data)
	if a.Width != 256 || a.Height != 192 || a.SizeBytes != int64(len(data)) || a.RunID != nil || a.UserName == "" {
		t.Fatalf("%+v", a)
	}
	mustExist(t, e.shotFile(a), true)
	e.clk.Advance(5e9)
	b := e.shot(e.admin.ID, e.g1.ID, "", pngBytes(t, 10, 10))
	e.shot(e.admin.ID, e.g2.ID, "", pngBytes(t, 4, 4))

	l, err := e.svc.ListScreenshots(ctx, e.admin.ID, e.g1.ID)
	if err != nil || len(l.Screenshots) != 2 || l.Screenshots[0].ID != b.ID || l.Screenshots[1].ID != a.ID ||
		l.TotalBytes != a.SizeBytes+b.SizeBytes {
		t.Fatalf("%+v %v", l, err)
	}
	f, sc, err := e.svc.OpenScreenshot(ctx, e.admin.ID, a.ID)
	if err != nil {
		t.Fatal(err)
	}
	got, _ := io.ReadAll(f)
	f.Close()
	if !bytes.Equal(got, data) || sc.ID != a.ID {
		t.Fatal("content differs")
	}

	// Another user sees and deletes nothing.
	bob, _ := e.svc.CreateUser(ctx, "bob", "Bob")
	if _, _, err := e.svc.OpenScreenshot(ctx, bob.ID, a.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("foreign open: %v", err)
	}
	if err := e.svc.DeleteScreenshot(ctx, bob.ID, a.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("foreign delete: %v", err)
	}
	if bl, _ := e.svc.ListScreenshots(ctx, bob.ID, e.g1.ID); len(bl.Screenshots) != 0 || bl.TotalBytes != 0 {
		t.Fatalf("%+v", bl)
	}
	mustExist(t, e.shotFile(a), true)

	if err := e.svc.DeleteScreenshot(ctx, e.admin.ID, a.ID); err != nil {
		t.Fatal(err)
	}
	mustExist(t, e.shotFile(a), false)
	if err := e.svc.DeleteScreenshot(ctx, e.admin.ID, a.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("second delete: %v", err)
	}
	if l, _ = e.svc.ListScreenshots(ctx, e.admin.ID, e.g1.ID); len(l.Screenshots) != 1 {
		t.Fatalf("%+v", l)
	}
}

func TestScreenshotRejectsBadInput(t *testing.T) {
	e := newPlayEnv(t, nil)
	for name, body := range map[string][]byte{"text": []byte("not an image"), "empty": {}, "truncated": pngBytes(t, 8, 8)[:12]} {
		if _, err := e.svc.AddScreenshot(ctx, e.admin.ID, e.g1.ID, "", bytes.NewReader(body)); !errors.Is(err, hub.ErrInvalidImage) {
			t.Fatalf("%s: %v", name, err)
		}
	}
	// Too large: more than 16 MiB, even when the start is a valid PNG.
	big := append(pngBytes(t, 4, 4), bytes.Repeat([]byte{0}, hub.MaxScreenshotBytes)...)
	if _, err := e.svc.AddScreenshot(ctx, e.admin.ID, e.g1.ID, "", bytes.NewReader(big)); !errors.Is(err, hub.ErrPayloadTooLarge) {
		t.Fatalf("big: %v", err)
	}
	if _, err := e.svc.AddScreenshot(ctx, e.admin.ID, "00000000-0000-4000-8000-000000000000", "", bytes.NewReader(pngBytes(t, 4, 4))); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown game: %v", err)
	}
	if u, _ := e.svc.ScreenshotUsage(ctx); u.TotalCount != 0 {
		t.Fatalf("%+v", u)
	}
	// Nothing is left behind.
	if ents, _ := os.ReadDir(filepath.Join(e.dir, "tmp")); len(ents) != 0 {
		t.Fatalf("tmp holds %d entries", len(ents))
	}
	if ents, _ := os.ReadDir(filepath.Join(e.dir, "screenshots")); len(ents) != 0 {
		t.Fatalf("screenshots holds %d entries", len(ents))
	}
}

// runShots sets up a run of the admin and bob (joined) with an invited-only carol and an outsider dave.
func (e *playEnv) runShots(t *testing.T) (run hub.Run, bob, carol, dave hub.User) {
	t.Helper()
	e.svc.SetPluginCatalog(onePlugin{"net.example.board", "1.0.0"})
	bob, _ = e.svc.CreateUser(ctx, "bob", "Bob")
	carol, _ = e.svc.CreateUser(ctx, "carol", "Carol")
	dave, _ = e.svc.CreateUser(ctx, "dave", "Dave")
	run, err := e.svc.CreateRun(ctx, hub.CreateRunInput{UserID: e.admin.ID, Name: "Run", GameID: e.g1.ID,
		InviteUserIDs: []string{bob.ID, carol.ID}, Plugins: []hub.RunPluginRef{{PluginID: "net.example.board", Version: "1.0.0"}}})
	if err != nil {
		t.Fatal(err)
	}
	if _, err := e.svc.JoinRun(ctx, bob.ID, run.ID, e.g1.ID); err != nil {
		t.Fatal(err)
	}
	return run, bob, carol, dave
}

func TestScreenshotRunTaggingAndVisibility(t *testing.T) {
	e := newPlayEnv(t, nil)
	run, bob, carol, dave := e.runShots(t)
	png1 := pngBytes(t, 6, 6)

	// Tagging needs a joined membership of that run for that game.
	for name, u := range map[string]string{"invited": carol.ID, "outsider": dave.ID} {
		if _, err := e.svc.AddScreenshot(ctx, u, e.g1.ID, run.ID, bytes.NewReader(png1)); !errors.Is(err, hub.ErrNotFound) {
			t.Fatalf("%s: %v", name, err)
		}
	}
	if _, err := e.svc.AddScreenshot(ctx, e.admin.ID, e.g2.ID, run.ID, bytes.NewReader(png1)); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("other game: %v", err)
	}
	if _, err := e.svc.AddScreenshot(ctx, e.admin.ID, e.g1.ID, "00000000-0000-4000-8000-000000000000", bytes.NewReader(png1)); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("unknown run: %v", err)
	}

	a := e.shot(e.admin.ID, e.g1.ID, run.ID, png1)
	e.clk.Advance(2e9)
	b := e.shot(bob.ID, e.g1.ID, run.ID, png1)
	own := e.shot(bob.ID, e.g1.ID, "", png1) // untagged
	if a.RunID == nil || *a.RunID != run.ID {
		t.Fatalf("%+v", a)
	}

	for _, u := range []string{e.admin.ID, bob.ID} {
		l, err := e.svc.ListRunScreenshots(ctx, u, run.ID)
		if err != nil || len(l.Screenshots) != 2 || l.Screenshots[0].ID != b.ID || l.Screenshots[1].ID != a.ID || l.TotalBytes != a.SizeBytes+b.SizeBytes {
			t.Fatalf("%+v %v", l, err)
		}
	}
	for _, u := range []string{carol.ID, dave.ID} {
		if _, err := e.svc.ListRunScreenshots(ctx, u, run.ID); !errors.Is(err, hub.ErrNotFound) {
			t.Fatalf("list as %s: %v", u, err)
		}
		if _, _, err := e.svc.OpenScreenshot(ctx, u, a.ID); !errors.Is(err, hub.ErrNotFound) {
			t.Fatalf("open as %s: %v", u, err)
		}
	}
	// A run member reads the content of a screenshot of the run, but not an untagged one, and cannot delete it.
	f, _, err := e.svc.OpenScreenshot(ctx, e.admin.ID, b.ID)
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
	if _, _, err := e.svc.OpenScreenshot(ctx, e.admin.ID, own.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("untagged: %v", err)
	}
	if err := e.svc.DeleteScreenshot(ctx, e.admin.ID, b.ID); !errors.Is(err, hub.ErrNotFound) {
		t.Fatalf("member delete: %v", err)
	}

	// Deleting the run keeps the screenshots; run_id becomes NULL.
	if err := e.svc.DeleteRun(ctx, run.ID); err != nil {
		t.Fatal(err)
	}
	mustExist(t, e.shotFile(a), true)
	l, _ := e.svc.ListScreenshots(ctx, e.admin.ID, e.g1.ID)
	if len(l.Screenshots) != 1 || l.Screenshots[0].RunID != nil {
		t.Fatalf("%+v", l)
	}
}

func TestScreenshotsRemovedWithUserAndGame(t *testing.T) {
	e := newPlayEnv(t, nil)
	bob, _ := e.svc.CreateUser(ctx, "bob", "Bob")
	d := pngBytes(t, 5, 5)
	a1 := e.shot(e.admin.ID, e.g1.ID, "", d)
	a2 := e.shot(e.admin.ID, e.g2.ID, "", d)
	b1 := e.shot(bob.ID, e.g1.ID, "", d)
	b2 := e.shot(bob.ID, e.g2.ID, "", d)

	// Removing a game removes the rows and files of every user for it.
	if err := e.svc.DeleteGame(ctx, e.g1.ID); err != nil {
		t.Fatal(err)
	}
	mustExist(t, e.shotFile(a1), false)
	mustExist(t, e.shotFile(b1), false)
	mustExist(t, e.shotFile(a2), true)
	u, _ := e.svc.ScreenshotUsage(ctx)
	if u.TotalCount != 2 {
		t.Fatalf("%+v", u)
	}

	// Deleting a user removes their files and rows, not those of others.
	if err := e.svc.DeleteUser(ctx, bob.ID, e.admin.ID); err != nil {
		t.Fatal(err)
	}
	mustExist(t, e.shotFile(b2), false)
	mustExist(t, filepath.Join(e.dir, "screenshots", bob.ID), false)
	mustExist(t, e.shotFile(a2), true)
	var n int
	if err := e.db.QueryRow(`SELECT COUNT(*) FROM screenshots`).Scan(&n); err != nil || n != 1 {
		t.Fatalf("rows %d %v", n, err)
	}
}

func TestScreenshotUsage(t *testing.T) {
	e := newPlayEnv(t, nil)
	if u, err := e.svc.ScreenshotUsage(ctx); err != nil || len(u.Rows) != 0 || u.TotalCount != 0 || u.TotalBytes != 0 {
		t.Fatalf("%+v %v", u, err)
	}
	bob, _ := e.svc.CreateUser(ctx, "bob", "Bob")
	small, large := pngBytes(t, 2, 2), pngBytes(t, 64, 64)
	e.shot(e.admin.ID, e.g1.ID, "", small)
	e.shot(e.admin.ID, e.g1.ID, "", large)
	e.shot(e.admin.ID, e.g2.ID, "", small)
	e.shot(bob.ID, e.g1.ID, "", large)
	u, err := e.svc.ScreenshotUsage(ctx)
	if err != nil {
		t.Fatal(err)
	}
	want := map[[2]string][2]int64{
		{e.admin.ID, e.g1.ID}: {2, int64(len(small) + len(large))},
		{e.admin.ID, e.g2.ID}: {1, int64(len(small))},
		{bob.ID, e.g1.ID}:     {1, int64(len(large))},
	}
	if len(u.Rows) != 3 {
		t.Fatalf("%+v", u)
	}
	for _, r := range u.Rows {
		w, ok := want[[2]string{r.UserID, r.GameID}]
		if !ok || int64(r.Count) != w[0] || r.Bytes != w[1] {
			t.Fatalf("row %+v, want %v", r, w)
		}
	}
	if u.TotalCount != 4 || u.TotalBytes != int64(2*len(small)+2*len(large)) {
		t.Fatalf("total %+v", u)
	}
}
