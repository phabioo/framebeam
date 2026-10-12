package web

import (
	"fmt"
	"strings"
	"time"
	"unicode"
)

// humanBytes formats bytes with a decimal point (steps of 1024).
func humanBytes[T ~int64 | ~uint64](n T) string {
	const unit = 1024.0
	v := float64(n)
	suffix := []string{"B", "KB", "MB", "GB", "TB"}
	i := 0
	for v >= unit && i < len(suffix)-1 {
		v /= unit
		i++
	}
	if i == 0 {
		return fmt.Sprintf("%d B", int64(v))
	}
	return fmt.Sprintf("%.1f", v) + " " + suffix[i]
}

func shortHash(h string) string {
	if len(h) < 9 {
		return h
	}
	return h[:4] + "…" + h[len(h)-4:]
}

func initial(title string) string {
	for _, r := range title {
		if unicode.IsLetter(r) || unicode.IsDigit(r) {
			return strings.ToUpper(string(r))
		}
	}
	return "?"
}

// When is a formatted point in time: Text for lists, Title for the tooltip (full timestamp).
type When struct{ Text, Title string }

// fmtWhen is the one date/time helper of the Hub (design sheet 0-2): "just now", "5 min ago", "today 18:52",
// "yesterday 18:52", "16 Oct", "3 Mar 2025". The tooltip holds the full timestamp. Lists never show seconds.
func fmtWhen(t, now time.Time) When {
	if t.IsZero() {
		return When{Text: "never"}
	}
	lt, ln := t.Local(), now.Local()
	w := When{Title: lt.Format("2 Jan 2006, 15:04:05")}
	d := ln.Sub(lt)
	ty, tm, td := lt.Date()
	y, m, dd := ln.Date()
	yy, ym, yd := ln.AddDate(0, 0, -1).Date()
	switch {
	case d < time.Minute: // also future timestamps (clock skew)
		w.Text = "just now"
	case d < time.Hour:
		w.Text = fmt.Sprintf("%d min ago", int(d.Minutes()))
	case ty == y && tm == m && td == dd:
		w.Text = "today " + lt.Format("15:04")
	case ty == yy && tm == ym && td == yd:
		w.Text = "yesterday " + lt.Format("15:04")
	case ty == y:
		w.Text = lt.Format("2 Jan")
	default:
		w.Text = lt.Format("2 Jan 2006")
	}
	return w
}

// when formats t with fmtWhen; the zero time gives Text "never".
func when(t, now time.Time) When { return fmtWhen(t, now) }

// whenPtr is when for an optional time; nil gives Text "never".
func whenPtr(t *time.Time, now time.Time) When {
	if t == nil {
		return When{Text: "never"}
	}
	return fmtWhen(*t, now)
}

// ago returns the list text of fmtWhen (kept for call sites that need only a string).
func ago(t, now time.Time) string { return fmtWhen(t, now).Text }
