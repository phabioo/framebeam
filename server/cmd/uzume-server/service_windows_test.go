//go:build windows

package main

import (
	"context"
	"testing"
	"time"

	"golang.org/x/sys/windows/svc"
)

// drive runs Execute with a fake service control manager and returns the reported states and exit values.
func drive(t *testing.T, run func(context.Context) error, send func(reqs chan<- svc.ChangeRequest)) (states []svc.State, ssec bool, code uint32) {
	t.Helper()
	h := &serviceHandler{run: run}
	reqs := make(chan svc.ChangeRequest, 4)
	status := make(chan svc.Status, 16)
	type ret struct {
		ssec bool
		code uint32
	}
	out := make(chan ret, 1)
	go func() { s, c := h.Execute(nil, reqs, status); out <- ret{s, c} }()
	if send != nil {
		send(reqs)
	}
	select {
	case r := <-out:
		ssec, code = r.ssec, r.code
	case <-time.After(5 * time.Second):
		t.Fatal("Execute did not return")
	}
	close(status)
	for s := range status {
		states = append(states, s.State)
	}
	return
}

func TestServiceStopCancelsContext(t *testing.T) {
	running := make(chan struct{})
	states, _, code := drive(t, func(ctx context.Context) error { close(running); <-ctx.Done(); return nil },
		func(reqs chan<- svc.ChangeRequest) { <-running; reqs <- svc.ChangeRequest{Cmd: svc.Stop} })
	want := []svc.State{svc.StartPending, svc.Running, svc.StopPending}
	if code != 0 || len(states) != 3 || states[0] != want[0] || states[1] != want[1] || states[2] != want[2] {
		t.Fatalf("states=%v code=%d", states, code)
	}
}

func TestServiceRunEndsByItself(t *testing.T) {
	_, _, code := drive(t, func(context.Context) error { return context.DeadlineExceeded }, nil)
	if code != 1 {
		t.Fatalf("exit code %d", code)
	}
}
