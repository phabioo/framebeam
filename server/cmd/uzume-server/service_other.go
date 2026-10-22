//go:build !windows

package main

import (
	"context"
	"errors"
)

const (
	hubServiceName     = "UzumeServer"
	updaterServiceName = "UzumeServerUpdater"
)

func isWindowsService() bool { return false }

func runAsService(string, func(context.Context) error) error {
	return errors.New("running as a Windows service is not supported on this platform")
}
