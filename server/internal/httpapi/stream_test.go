package httpapi

import (
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestRollingWriteStreamsOnRealConnection(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w, done := rollingWrite(w)
		defer done()
		io.Copy(w, strings.NewReader(strings.Repeat("x", 1<<20)))
	}))
	defer srv.Close()
	resp, err := http.Get(srv.URL)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if n, _ := io.Copy(io.Discard, resp.Body); n != 1<<20 {
		t.Fatalf("got %d bytes", n)
	}
	// Writers without deadline support are used unchanged.
	rec := httptest.NewRecorder()
	if w, _ := rollingWrite(rec); w != http.ResponseWriter(rec) {
		t.Fatal("recorder was wrapped")
	}
}
