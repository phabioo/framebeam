package backup

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// Files in the data directory that carry a restore across the restart.
const (
	pendingFile  = "restore-pending.json"
	resultFile   = "restore-result.json"
	StagingDir   = "restore-staging"
	oldDir       = "restore-old"
	committedTag = "COMMITTED"
	absentSuffix = ".absent"
)

// swapItems are the top-level entries of the data directory a restore may replace.
var swapItems = map[string]bool{"uzume.db": true, "saves": true, "states": true, "plugins": true,
	"firmware": true, "tls": true, "screenshots": true, "roms": true, "restored-config": true}

// walFiles belong to uzume.db and are moved away with it (the restored database is a plain file).
var walFiles = []string{"uzume.db-wal", "uzume.db-shm"}

// Pending is a prepared restore: staging holds the verified, unpacked archive; the swap happens at the next start.
type Pending struct {
	Staging       string    `json:"staging"`
	Items         []string  `json:"items"`
	Backup        string    `json:"backup"`
	BackupCreated time.Time `json:"backup_created"`
	IncludesROMs  bool      `json:"includes_roms"`
	Safety        string    `json:"safety,omitempty"`
	Requested     time.Time `json:"requested"`
	By            string    `json:"by,omitempty"`
}

// RestoreResult is what the Backups page shows after a restore (until it is dismissed).
type RestoreResult struct {
	OK            bool      `json:"ok"`
	At            time.Time `json:"at"`
	Backup        string    `json:"backup"`
	BackupCreated time.Time `json:"backup_created"`
	IncludesROMs  bool      `json:"includes_roms"`
	Safety        string    `json:"safety,omitempty"`
	Error         string    `json:"error,omitempty"`
}

func writeJSON(path string, v any) error {
	b, err := json.MarshalIndent(v, "", " ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// WritePending records a prepared restore. The staging directory must be below dataDir.
func WritePending(dataDir string, p Pending) error {
	return writeJSON(filepath.Join(dataDir, pendingFile), p)
}

// HasPending reports whether a restore waits for the next start.
func HasPending(dataDir string) bool {
	_, err := os.Stat(filepath.Join(dataDir, pendingFile))
	return err == nil
}

// ReadResult returns the last restore result, or nil.
func ReadResult(dataDir string) *RestoreResult {
	b, err := os.ReadFile(filepath.Join(dataDir, resultFile))
	if err != nil {
		return nil
	}
	var r RestoreResult
	if json.Unmarshal(b, &r) != nil {
		return nil
	}
	return &r
}

// WriteResult stores a restore result (used for failures that happen before the restart).
func WriteResult(dataDir string, r RestoreResult) error {
	return writeJSON(filepath.Join(dataDir, resultFile), r)
}

// DismissResult removes the stored restore result.
func DismissResult(dataDir string) { os.Remove(filepath.Join(dataDir, resultFile)) }

// ApplyPending swaps a prepared restore in. The server must not have the database open. It returns nil, nil when
// nothing is pending. On failure every item is moved back (the data directory is as before), the failure is
// stored as the restore result and returned together with the error. A crash in the middle is repaired at the
// next call: a swap without the COMMITTED tag is rolled back and run again, a committed one is only cleaned up.
func ApplyPending(dataDir string, now time.Time) (*RestoreResult, error) {
	b, err := os.ReadFile(filepath.Join(dataDir, pendingFile))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	var p Pending
	if err := json.Unmarshal(b, &p); err != nil {
		os.Remove(filepath.Join(dataDir, pendingFile))
		return failRestore(dataDir, Pending{}, now, fmt.Errorf("restore request unreadable: %w", err))
	}
	staging := filepath.Join(dataDir, StagingDir)
	oldRoot := filepath.Join(dataDir, oldDir)
	items := append([]string{}, p.Items...)
	for _, it := range items {
		if !swapItems[it] {
			return failRestore(dataDir, p, now, fmt.Errorf("unexpected item %q", it))
		}
	}
	if _, err := os.Stat(filepath.Join(oldRoot, committedTag)); err == nil {
		return finishRestore(dataDir, p, now) // crashed after the swap: only clean up
	}
	rollback(dataDir, staging, oldRoot, items) // leftovers of a crashed attempt
	if err := swap(dataDir, staging, oldRoot, items); err != nil {
		rollback(dataDir, staging, oldRoot, items)
		return failRestore(dataDir, p, now, err)
	}
	return finishRestore(dataDir, p, now)
}

func finishRestore(dataDir string, p Pending, now time.Time) (*RestoreResult, error) {
	res := RestoreResult{OK: true, At: now, Backup: p.Backup, BackupCreated: p.BackupCreated, IncludesROMs: p.IncludesROMs, Safety: p.Safety}
	if err := WriteResult(dataDir, res); err != nil {
		return &res, err
	}
	os.Remove(filepath.Join(dataDir, pendingFile))
	os.RemoveAll(filepath.Join(dataDir, oldDir))
	os.RemoveAll(filepath.Join(dataDir, StagingDir))
	return &res, nil
}

func failRestore(dataDir string, p Pending, now time.Time, cause error) (*RestoreResult, error) {
	res := RestoreResult{OK: false, At: now, Backup: p.Backup, BackupCreated: p.BackupCreated, Safety: p.Safety, Error: cause.Error()}
	_ = WriteResult(dataDir, res)
	os.Remove(filepath.Join(dataDir, pendingFile))
	os.RemoveAll(filepath.Join(dataDir, oldDir))
	os.RemoveAll(filepath.Join(dataDir, StagingDir))
	return &res, cause
}

func exists(p string) bool { _, err := os.Lstat(p); return err == nil }

// swap moves the live items into oldRoot and the staged ones into place.
func swap(dataDir, staging, oldRoot string, items []string) error {
	if err := os.MkdirAll(oldRoot, 0o750); err != nil {
		return err
	}
	// The write-ahead files of the old database go away with it.
	for _, w := range walFiles {
		if exists(filepath.Join(dataDir, w)) {
			if err := os.Rename(filepath.Join(dataDir, w), filepath.Join(oldRoot, w)); err != nil {
				return err
			}
		}
	}
	for _, it := range items {
		target, old, staged := filepath.Join(dataDir, it), filepath.Join(oldRoot, it), filepath.Join(staging, it)
		if exists(target) {
			if err := os.Rename(target, old); err != nil {
				return fmt.Errorf("move %s aside: %w", it, err)
			}
		} else if err := os.WriteFile(old+absentSuffix, nil, 0o600); err != nil {
			return err
		}
		if err := os.Rename(staged, target); err != nil {
			return fmt.Errorf("put %s in place: %w", it, err)
		}
	}
	return os.WriteFile(filepath.Join(oldRoot, committedTag), nil, 0o600)
}

// rollback puts every moved item back. An item that is already replaced is moved back to staging.
func rollback(dataDir, staging, oldRoot string, items []string) {
	if !exists(oldRoot) {
		return
	}
	names := append(append([]string{}, items...), walFiles...)
	for i := len(names) - 1; i >= 0; i-- {
		it := names[i]
		target, old := filepath.Join(dataDir, it), filepath.Join(oldRoot, it)
		switch {
		case exists(old):
			if exists(target) {
				stash(target, filepath.Join(staging, it))
			}
			os.Rename(old, target)
		case exists(old + absentSuffix):
			if exists(target) {
				stash(target, filepath.Join(staging, it))
			}
			os.Remove(old + absentSuffix)
		}
	}
}

func stash(from, to string) {
	if exists(to) || os.Rename(from, to) != nil {
		os.RemoveAll(from)
	}
}
