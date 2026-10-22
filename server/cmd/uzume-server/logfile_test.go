package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRotatingWriterRotatesAndRecovers(t *testing.T) {
	path := filepath.Join(t.TempDir(), "hub.log")
	w, err := newRotatingWriter(path, 20)
	if err != nil {
		t.Fatal(err)
	}
	defer w.Close()
	for i := 0; i < 3; i++ {
		if _, err := w.Write([]byte("0123456789\n")); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := os.Stat(path + ".1"); err != nil {
		t.Fatalf("not rotated: %v", err)
	}
	// A writer whose reopen failed earlier (f == nil) opens the file again on the next write.
	w.f.Close()
	w.f = nil
	if _, err := w.Write([]byte("again\n")); err != nil {
		t.Fatalf("writer stays dead: %v", err)
	}
	// After Close, writes fail.
	w.Close()
	if _, err := w.Write([]byte("x")); err == nil {
		t.Fatal("write after Close succeeded")
	}
}
