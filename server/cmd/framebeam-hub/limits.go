package main

import (
	"net"
	"net/http"
	"regexp"
	"strings"
	"sync"
	"time"
)

const (
	// requestReadTimeout bounds reading a whole request (headers and body) on ordinary routes (slow-body clients).
	requestReadTimeout = 30 * time.Second
	// uploadReadTimeout is the generous bound for the streaming upload routes (ROMs up to 4 GiB).
	uploadReadTimeout = 2 * time.Hour
	// maxConns caps concurrent connections on the listener.
	maxConns = 512
)

var (
	apiSaveUploadPath = regexp.MustCompile(`^/api/v1/games/[^/]+/saves/[^/]+/upload$`)
	webSaveUploadPath = regexp.MustCompile(`^/saves/[^/]+/[^/]+/[^/]+/upload$`)
	webFirmwareUpload = regexp.MustCompile(`^/systems/[^/]+/firmware/[^/]+/upload$`)
)

// readTimeoutFor returns the read deadline for a request: 0 means none (long-lived streams), otherwise the
// duration from now. Streaming uploads get a long deadline, everything else a short one.
func readTimeoutFor(r *http.Request) time.Duration {
	p := r.URL.Path
	switch {
	case p == "/events" || p == "/api/v1/ws":
		return 0 // SSE and WebSocket (the connection is hijacked): no read deadline
	case r.Method == http.MethodPost && (p == "/api/v1/games" || p == "/library/upload" ||
		apiSaveUploadPath.MatchString(p) || webSaveUploadPath.MatchString(p) || webFirmwareUpload.MatchString(p)):
		return uploadReadTimeout
	case r.Method == http.MethodPut && strings.HasPrefix(p, "/api/v1/games/") && strings.Contains(p, "/saves/"):
		return uploadReadTimeout
	}
	return requestReadTimeout
}

// withReadDeadline sets a per-request read deadline (http.Server has no per-route ReadTimeout).
func withReadDeadline(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		d := readTimeoutFor(r)
		var t time.Time
		if d > 0 {
			t = time.Now().Add(d)
		}
		// Errors (writer without deadline support, e.g. in tests) are ignored.
		_ = http.NewResponseController(w).SetReadDeadline(t)
		next.ServeHTTP(w, r)
	})
}

// limitListener caps the number of simultaneously open connections: Accept blocks while n are open.
func limitListener(l net.Listener, n int) net.Listener {
	return &limitedListener{Listener: l, sem: make(chan struct{}, n), done: make(chan struct{})}
}

type limitedListener struct {
	net.Listener
	sem  chan struct{}
	once sync.Once
	done chan struct{}
}

func (l *limitedListener) Accept() (net.Conn, error) {
	select {
	case l.sem <- struct{}{}:
	case <-l.done:
		return nil, net.ErrClosed
	}
	c, err := l.Listener.Accept()
	if err != nil {
		<-l.sem
		return nil, err
	}
	return &limitedConn{Conn: c, release: func() { <-l.sem }}, nil
}

func (l *limitedListener) Close() error {
	l.once.Do(func() { close(l.done) })
	return l.Listener.Close()
}

type limitedConn struct {
	net.Conn
	once    sync.Once
	release func()
}

func (c *limitedConn) Close() error {
	err := c.Conn.Close()
	c.once.Do(c.release)
	return err
}
