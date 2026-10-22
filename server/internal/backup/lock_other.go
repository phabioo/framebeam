//go:build !unix && !windows

package backup

import "errors"

// ErrLocked: another process (the running server) holds the data directory.
var ErrLocked = errors.New("the data directory is in use by a running server")

// LockDataDir is a no-op on platforms without file locking.
func LockDataDir(string) (func(), error) { return func() {}, nil }
