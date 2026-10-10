package hub_test

import (
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"unicode/utf8"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
)

func TestCleanTextTruncatesOnRuneBoundary(t *testing.T) {
	in := strings.Repeat("ゲ", 100) // 300 bytes, 3 bytes per rune
	for _, max := range []int{200, 201, 199, 1, 2, 3} {
		got := hub.CleanTextForTest(in, max)
		if !utf8.ValidString(got) || len(got) > max {
			t.Fatalf("max %d: valid=%v len=%d", max, utf8.ValidString(got), len(got))
		}
	}
	if got := hub.CleanTextForTest(in, 200); len(got) != 198 {
		t.Fatalf("len %d", len(got))
	}
	if got := hub.CleanTextForTest("abcdef", 3); got != "abc" {
		t.Fatal(got)
	}
}

func TestOpenClearsStagingLeftovers(t *testing.T) {
	dir := t.TempDir()
	tmp := filepath.Join(dir, "tmp")
	fw := filepath.Join(dir, "firmware", "nds")
	for _, d := range []string{tmp, fw, filepath.Join(tmp, "sub")} {
		if err := os.MkdirAll(d, 0o750); err != nil {
			t.Fatal(err)
		}
	}
	keep := filepath.Join(fw, "bios7.bin")
	for _, f := range []string{filepath.Join(tmp, "rom-123"), filepath.Join(tmp, "sub", "x"), filepath.Join(fw, "bios7.bin.bak"), keep} {
		if err := os.WriteFile(f, []byte("x"), 0o640); err != nil {
			t.Fatal(err)
		}
	}
	hubtest.New(t, func(o *hub.Options) { o.DataDir = dir })
	if entries, _ := os.ReadDir(tmp); len(entries) != 0 {
		t.Fatalf("tmp not cleared: %v", entries)
	}
	if _, err := os.Stat(filepath.Join(fw, "bios7.bin.bak")); !os.IsNotExist(err) {
		t.Fatal("firmware backup kept")
	}
	if _, err := os.Stat(keep); err != nil {
		t.Fatal("firmware file removed")
	}
}

func TestNewSlotNameRejectsWindowsReserved(t *testing.T) {
	for _, n := range []string{"con", "prn", "aux", "nul", "com1", "com9", "lpt1", "lpt9"} {
		if !hub.ValidSlotName(n) || hub.ValidNewSlotName(n) {
			t.Fatalf("%s: valid=%v new=%v", n, hub.ValidSlotName(n), hub.ValidNewSlotName(n))
		}
	}
	for _, n := range []string{"default", "com10", "com0", "console", "lpt", "nul2"} {
		if !hub.ValidNewSlotName(n) {
			t.Fatalf("%s rejected", n)
		}
	}
	e, _ := newComfortEnv(t, nil)
	e.put(e.devA, 0, []byte("one"), hub.SyncCheckpoint)
	if _, err := e.svc.CreateSaveSlot(ctx, e.user.ID, e.game.ID, "default", "nul", hub.WebDeviceID(e.user.ID)); err == nil {
		t.Fatal("reserved slot created")
	}
}

func TestIsLocalRequest(t *testing.T) {
	for _, c := range []struct {
		remote string
		hdr    string
		want   bool
	}{
		{"127.0.0.1:1", "", true}, {"[::1]:1", "", true}, {"192.0.2.1:1", "", false},
		{"127.0.0.1:1", "X-Forwarded-For", false}, {"127.0.0.1:1", "X-Real-IP", false},
		{"127.0.0.1:1", "Forwarded", false}, {"127.0.0.1:1", "X-Forwarded-Host", false},
	} {
		r := httptest.NewRequest("GET", "/", nil)
		r.RemoteAddr = c.remote
		if c.hdr != "" {
			r.Header.Set(c.hdr, "203.0.113.9")
		}
		if got := hub.IsLocalRequest(r); got != c.want {
			t.Fatalf("%s %s: %v", c.remote, c.hdr, got)
		}
	}
}
