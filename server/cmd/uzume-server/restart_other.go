//go:build !unix

package main

import "errors"

// reexec is not supported here: the process exits with an error and the service manager (if any) restarts it.
func reexec() error {
	return errors.New("restart: in-place restart is not supported on this platform")
}
