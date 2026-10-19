// Package rollwrite gives long downloads a rolling write deadline.
package rollwrite

import (
	"net/http"
	"time"
)

// Timeout is how long a single write of a file download may block. The deadline is rolling: it is renewed with
// every chunk, so a slow but alive client still finishes a multi-GiB download while a client that stops reading
// frees its goroutine and connection slot.
const Timeout = 60 * time.Second

// Wrap returns a writer that renews the write deadline of the connection before each chunk, and a function that
// clears the deadline again (it would otherwise leak into the next request on a kept-alive connection). Writers
// without deadline support (tests) are used unchanged.
func Wrap(w http.ResponseWriter) (http.ResponseWriter, func()) {
	rc := http.NewResponseController(w)
	if rc.SetWriteDeadline(time.Now().Add(Timeout)) != nil {
		return w, func() {}
	}
	rw := &rollingWriter{ResponseWriter: w, rc: rc, set: time.Now()}
	return rw, func() { _ = rc.SetWriteDeadline(time.Time{}) }
}

type rollingWriter struct {
	http.ResponseWriter
	rc  *http.ResponseController
	set time.Time // when the deadline was last renewed
}

func (w *rollingWriter) Write(p []byte) (int, error) {
	if now := time.Now(); now.Sub(w.set) >= time.Second { // a deadline syscall per 32 KiB chunk is not needed
		_ = w.rc.SetWriteDeadline(now.Add(Timeout))
		w.set = now
	}
	return w.ResponseWriter.Write(p)
}

func (w *rollingWriter) Unwrap() http.ResponseWriter { return w.ResponseWriter }
