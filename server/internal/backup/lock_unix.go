//go:build unix

package backup

import (
	"errors"
	"os"
	"path/filepath"
	"syscall"
)

// ErrLocked: another process (the running Hub) holds the data directory.
var ErrLocked = errors.New("the data directory is in use by a running Hub")

// LockDataDir takes the exclusive lock of the data directory (hub.lock). The serving Hub holds it for its whole
// run; `framebeam-hub restore` needs it, so a restore never runs next to a live Hub. The lock goes away with the
// process, also after a crash.
func LockDataDir(dataDir string) (release func(), err error) {
	f, err := os.OpenFile(filepath.Join(dataDir, "hub.lock"), os.O_CREATE|os.O_RDWR, 0o600)
	if err != nil {
		return nil, err
	}
	if err := syscall.Flock(int(f.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		f.Close()
		if errors.Is(err, syscall.EWOULDBLOCK) {
			return nil, ErrLocked
		}
		return nil, err
	}
	return func() { syscall.Flock(int(f.Fd()), syscall.LOCK_UN); f.Close() }, nil
}
