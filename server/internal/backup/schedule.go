package backup

import (
	"os"
	"path/filepath"
	"sort"
	"time"
)

// Schedule modes.
const (
	ModeOff    = "off"
	ModeDaily  = "daily"
	ModeWeekly = "weekly"
)

// NextRun returns the first run strictly after now: today or the next day (daily) or the next given weekday
// (weekly) at hour:min in loc. Days are counted as calendar days, not as 24 hours, so a daylight saving change
// does not move the run; a wall time that does not exist on a given day (spring forward) runs at the same
// clock time as Go resolves it. The zero time means "not scheduled".
func NextRun(now time.Time, loc *time.Location, mode string, weekday time.Weekday, hour, min int) time.Time {
	if mode != ModeDaily && mode != ModeWeekly {
		return time.Time{}
	}
	if loc == nil {
		loc = time.Local
	}
	n := now.In(loc)
	y, m, d := n.Date()
	for i := 0; i < 15; i++ {
		c := time.Date(y, m, d+i, hour, min, 0, 0, loc)
		if !c.After(now) {
			continue
		}
		if mode == ModeWeekly && c.Weekday() != weekday {
			continue
		}
		return c
	}
	return time.Time{}
}

// Prune deletes the oldest regular backups of dir so that keep remain. Safety backups are never touched. Call it
// only after a new backup succeeded (decision 26); it never looks at free space. It returns the deleted names.
func Prune(dir string, keep int) ([]string, error) {
	if keep < 1 {
		keep = 1
	}
	ents, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	type b struct {
		name string
		ts   time.Time
	}
	var all []b
	for _, e := range ents {
		ts, safety, ok := ParseName(e.Name())
		if ok && !safety && e.Type().IsRegular() {
			all = append(all, b{e.Name(), ts})
		}
	}
	sort.Slice(all, func(i, j int) bool {
		if !all[i].ts.Equal(all[j].ts) {
			return all[i].ts.After(all[j].ts)
		}
		return all[i].name > all[j].name
	})
	var deleted []string
	for i := keep; i < len(all); i++ {
		if err := os.Remove(filepath.Join(dir, all[i].name)); err != nil {
			return deleted, err
		}
		deleted = append(deleted, all[i].name)
	}
	return deleted, nil
}
