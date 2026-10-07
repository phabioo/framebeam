//go:build unix

package main

import (
	"fmt"
	"os"
	"strings"
	"syscall"
)

// reexec replaces the process with a fresh copy of the same binary, arguments and environment (same PID, so a
// systemd Type=simple unit keeps tracking it). It only returns an error: if exec fails main exits non-zero and
// the service manager restarts the Hub.
func reexec() error {
	exe, err := os.Executable()
	if err != nil {
		return fmt.Errorf("restart: %w", err)
	}
	// After an update the running file was replaced: Linux then reports the old path with this suffix.
	exe = strings.TrimSuffix(exe, " (deleted)")
	fmt.Fprintln(os.Stderr, "Restarting FrameBeam Hub")
	if err := syscall.Exec(exe, os.Args, os.Environ()); err != nil {
		return fmt.Errorf("restart: exec %s: %w", exe, err)
	}
	return nil
}
