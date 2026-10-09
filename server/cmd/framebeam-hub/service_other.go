//go:build !windows

package main

import (
	"context"
	"errors"
)

const (
	hubServiceName     = "FrameBeamHub"
	updaterServiceName = "FrameBeamHubUpdater"
)

func isWindowsService() bool { return false }

func runAsService(string, func(context.Context) error) error {
	return errors.New("running as a Windows service is not supported on this platform")
}
