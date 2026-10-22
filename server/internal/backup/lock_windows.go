//go:build windows

package backup

import (
	"errors"
	"os"
	"path/filepath"

	"golang.org/x/sys/windows"
)

// ErrLocked: another process (the running Hub) holds the data directory.
var ErrLocked = errors.New("the data directory is in use by a running Hub")

// LockDataDir takes the exclusive lock of the data directory (hub.lock), see the unix version.
func LockDataDir(dataDir string) (release func(), err error) {
	f, err := os.OpenFile(filepath.Join(dataDir, "hub.lock"), os.O_CREATE|os.O_RDWR, 0o600)
	if err != nil {
		return nil, err
	}
	ol := new(windows.Overlapped)
	h := windows.Handle(f.Fd())
	if err := windows.LockFileEx(h, windows.LOCKFILE_EXCLUSIVE_LOCK|windows.LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, ol); err != nil {
		f.Close()
		if errors.Is(err, windows.ERROR_LOCK_VIOLATION) {
			return nil, ErrLocked
		}
		return nil, err
	}
	return func() { windows.UnlockFileEx(h, 0, 1, 0, ol); f.Close() }, nil
}
