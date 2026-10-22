//go:build !unix && !windows

package backup

import "errors"

// ErrLocked: another process (the running Hub) holds the data directory.
var ErrLocked = errors.New("the data directory is in use by a running Hub")

// LockDataDir is a no-op on platforms without file locking.
func LockDataDir(string) (func(), error) { return func() {}, nil }
