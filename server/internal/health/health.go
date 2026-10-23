// Package health models the problems the server shows on its Status page and in the sidebar badge. Each check
// returns zero or more Problem values; the most urgent one is the banner and the badge. Later features (for
// example "Backup failed") add a check, nothing else changes.
package health

import "sort"

// Severity orders problems: a higher value is more urgent.
type Severity int

const (
	// Warn is a condition that needs attention soon (amber).
	Warn Severity = 1
	// Error is a condition that already breaks a function (red).
	Error Severity = 2
)

// Problem is one open, actionable condition.
type Problem struct {
	// ID is stable and machine readable ("disk", "turn", "update").
	ID string
	// Title is the short text of the sidebar badge and the banner pill ("Disk 92 %").
	Title string
	// Detail is the sentence next to the pill.
	Detail string
	// Severity decides the order and the color.
	Severity Severity
	// ActionLabel is the banner button text ("Open Backups"); the app maps it to its own page. May be empty.
	ActionLabel string
}

// Sort orders problems from most to least urgent. Problems of the same severity keep their order (the order the
// checks ran in).
func Sort(ps []Problem) {
	sort.SliceStable(ps, func(i, j int) bool { return ps[i].Severity > ps[j].Severity })
}

// MostUrgent returns the most urgent problem, or nil when there is none.
func MostUrgent(ps []Problem) *Problem {
	if len(ps) == 0 {
		return nil
	}
	best := 0
	for i := range ps {
		if ps[i].Severity > ps[best].Severity {
			best = i
		}
	}
	return &ps[best]
}

// Disk thresholds in percent used.
const (
	DiskWarnPercent  = 90.0
	DiskErrorPercent = 97.0
)
