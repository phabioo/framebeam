package web

import (
	"strings"
	"testing"
	"time"
)

func TestFmtWhen(t *testing.T) {
	now := time.Date(2026, 10, 16, 18, 52, 30, 0, time.Local)
	at := func(y int, m time.Month, d, h, min, sec int) time.Time {
		return time.Date(y, m, d, h, min, sec, 0, time.Local)
	}
	for _, tc := range []struct {
		name string
		t    time.Time
		want string
	}{
		{"seconds ago", now.Add(-20 * time.Second), "just now"},
		{"future (clock skew)", now.Add(5 * time.Minute), "just now"},
		{"minutes ago", now.Add(-5 * time.Minute), "5 min ago"},
		{"earlier today", at(2026, 10, 16, 7, 12, 59), "today 07:12"},
		{"yesterday", at(2026, 10, 15, 23, 40, 0), "yesterday 23:40"},
		{"this year", at(2026, 3, 3, 9, 0, 0), "3 Mar"},
		{"last year", at(2025, 3, 3, 9, 0, 0), "3 Mar 2025"},
	} {
		w := fmtWhen(tc.t, now)
		if w.Text != tc.want {
			t.Errorf("%s: %q, want %q", tc.name, w.Text, tc.want)
		}
		if strings.Count(w.Text, ":") > 1 || (w.Title != "" && !strings.Contains(w.Title, ":")) {
			t.Errorf("%s: list text %q has seconds or the tooltip %q has no full timestamp", tc.name, w.Text, w.Title)
		}
	}
	if w := fmtWhen(at(2026, 10, 16, 7, 12, 59), now); w.Title != "16 Oct 2026, 07:12:59" {
		t.Errorf("tooltip %q", w.Title)
	}
	if w := fmtWhen(time.Time{}, now); w.Text != "never" {
		t.Errorf("zero time: %q", w.Text)
	}
}

func TestCoreNotes(t *testing.T) {
	for _, tc := range []struct {
		name  string
		notes []string
		want  string
	}{
		{"firmware with md5 note", []string{"gba_bios.bin (Game Boy Advance BIOS)", "(!) gba_bios.bin (md5): a860e8c0b6d573d191e4ec7db1b1e4f6"}, "Firmware: gba_bios.bin (Game Boy Advance BIOS)"},
		{"other (!) note kept", []string{"a.bin (A)", "b.bin (B)", "(!) a.bin (md5): a860e8c0b6d573d191e4ec7db1b1e4f6", "(!) Needs a GPU"}, "Firmware: a.bin (A), b.bin (B) · (!) Needs a GPU"},
		{"none", nil, ""},
	} {
		if got := coreNotes(tc.notes); got != tc.want {
			t.Errorf("%s: %q, want %q", tc.name, got, tc.want)
		}
	}
}
