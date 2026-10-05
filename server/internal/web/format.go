package web

import (
	"fmt"
	"strings"
	"time"
	"unicode"
)

// humanBytes formatiert Bytes mit deutschem Dezimalkomma (1024er-Schritte).
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
	return strings.Replace(fmt.Sprintf("%.1f", v), ".", ",", 1) + " " + suffix[i]
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

// ago formatiert "vor 2 min" relativ zu now.
func ago(t, now time.Time) string {
	d := now.Sub(t)
	switch {
	case d < time.Minute:
		return "gerade eben"
	case d < time.Hour:
		return fmt.Sprintf("vor %d min", int(d.Minutes()))
	case d < 48*time.Hour:
		return fmt.Sprintf("vor %d h", int(d.Hours()))
	}
	return t.Format("02.01.2006")
}

// lastSeen formatiert "zuletzt aktiv": gerade eben / heute HH:MM / gestern / TT.MM. / TT.MM.JJJJ.
func lastSeen(t *time.Time, now time.Time) string {
	if t == nil {
		return "nie"
	}
	lt, ln := t.Local(), now.Local()
	if ln.Sub(lt) < time.Minute {
		return "gerade eben"
	}
	y, m, d := ln.Date()
	ty, tm, td := lt.Date()
	switch {
	case ty == y && tm == m && td == d:
		return "heute " + lt.Format("15:04")
	case lt.AddDate(0, 0, 1).Format("2006-01-02") == ln.Format("2006-01-02"):
		return "gestern"
	case ty == y:
		return lt.Format("02.01.")
	}
	return lt.Format("02.01.2006")
}
