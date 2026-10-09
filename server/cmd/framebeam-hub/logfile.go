package main

import (
	"fmt"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"sync"
)

// logFileMax is the size at which the log file is rotated: hub.log and hub.log.1 (about 20 MB in total).
const logFileMax = 10 << 20

// rotatingWriter appends to a file and, when the next write would exceed max bytes, moves it to <path>.1
// (replacing the previous one) and starts a new file. It is safe for concurrent use.
type rotatingWriter struct {
	mu   sync.Mutex
	path string
	max  int64
	f    *os.File
	size int64
}

func newRotatingWriter(path string, max int64) (*rotatingWriter, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0o750); err != nil {
		return nil, err
	}
	w := &rotatingWriter{path: path, max: max}
	if err := w.open(); err != nil {
		return nil, err
	}
	return w, nil
}

func (w *rotatingWriter) open() error {
	f, err := os.OpenFile(w.path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o640)
	if err != nil {
		return err
	}
	fi, err := f.Stat()
	if err != nil {
		f.Close()
		return err
	}
	w.f, w.size = f, fi.Size()
	return nil
}

func (w *rotatingWriter) rotate() error {
	if err := w.f.Close(); err != nil {
		return err
	}
	w.f = nil
	_ = os.Remove(w.path + ".1") // Windows cannot rename over an existing file
	if err := os.Rename(w.path, w.path+".1"); err != nil {
		_ = os.Remove(w.path) // cannot keep the old content; do not grow without bound
	}
	return w.open()
}

func (w *rotatingWriter) Write(p []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.f == nil {
		return 0, os.ErrClosed
	}
	if w.size > 0 && w.size+int64(len(p)) > w.max {
		if err := w.rotate(); err != nil {
			return 0, err
		}
	}
	n, err := w.f.Write(p)
	w.size += int64(n)
	return n, err
}

func (w *rotatingWriter) Close() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.f == nil {
		return nil
	}
	err := w.f.Close()
	w.f = nil
	return err
}

// newLogger logs to stderr; when running as a Windows service (nobody reads stderr) it logs to the rotating file
// at path instead, falling back to stderr if the file cannot be opened. The returned func closes the file.
func newLogger(path string) (*slog.Logger, func()) {
	var out io.Writer = os.Stderr
	closeFn := func() {}
	if isWindowsService() {
		if w, err := newRotatingWriter(path, logFileMax); err == nil {
			out, closeFn = w, func() { w.Close() }
		} else {
			fmt.Fprintln(os.Stderr, "cannot open log file, logging to stderr:", err)
		}
	}
	return slog.New(slog.NewTextHandler(out, nil)), closeFn
}
