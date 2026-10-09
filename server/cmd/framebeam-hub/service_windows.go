//go:build windows

package main

import (
	"context"
	"errors"
	"time"

	"golang.org/x/sys/windows/svc"
)

const (
	hubServiceName     = "FrameBeamHub"
	updaterServiceName = "FrameBeamHubUpdater"
)

// stopWaitHint is what the SCM is told to wait for a graceful stop (the server allows 10 s plus helper goroutines).
const stopWaitHint = 20 * time.Second

func isWindowsService() bool {
	ok, err := svc.IsWindowsService()
	return err == nil && ok
}

// runAsService hands the process to the service control manager and runs fn until the service is stopped.
func runAsService(name string, fn func(context.Context) error) error {
	h := &serviceHandler{run: fn}
	if err := svc.Run(name, h); err != nil {
		return err
	}
	return h.err
}

// serviceHandler adapts a context-based run function to svc.Handler.
type serviceHandler struct {
	run func(context.Context) error
	err error // error of run, read after svc.Run returns
}

// Execute implements svc.Handler: StartPending, Running, then StopPending when the SCM sends Stop/Shutdown (which
// cancels the context and waits for run to return). If run ends by itself the service stops with exit code 1 on
// error, so SCM recovery actions can restart it.
func (h *serviceHandler) Execute(_ []string, requests <-chan svc.ChangeRequest, status chan<- svc.Status) (bool, uint32) {
	status <- svc.Status{State: svc.StartPending, WaitHint: uint32(stopWaitHint / time.Millisecond)}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- h.run(ctx) }()
	running := svc.Status{State: svc.Running, Accepts: svc.AcceptStop | svc.AcceptShutdown}
	status <- running
	for {
		select {
		case req := <-requests:
			switch req.Cmd {
			case svc.Interrogate:
				status <- req.CurrentStatus
			case svc.Stop, svc.Shutdown:
				status <- svc.Status{State: svc.StopPending, WaitHint: uint32(stopWaitHint / time.Millisecond)}
				cancel()
				h.err = <-done
				if h.err != nil && !errors.Is(h.err, context.Canceled) {
					return false, 1
				}
				h.err = nil
				return false, 0
			}
		case err := <-done:
			status <- svc.Status{State: svc.StopPending}
			h.err = err
			if err != nil {
				return false, 1
			}
			return false, 0
		}
	}
}
