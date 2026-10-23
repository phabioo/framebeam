package main

import (
	"errors"
	"io"
	"net"
	"strings"
	"syscall"
)

// watchParent blocks until r reaches EOF or fails. In child mode r is stdin, the pipe from the app: it closes when
// the app exits or crashes, so the server never outlives it. Data sent to the pipe is ignored.
func watchParent(r io.Reader) {
	_, _ = io.Copy(io.Discard, r)
}

// listenPort is the port a listener is bound to (0 if it cannot be told).
func listenPort(a net.Addr) int {
	if t, ok := a.(*net.TCPAddr); ok {
		return t.Port
	}
	_, p, err := net.SplitHostPort(a.String())
	if err != nil {
		return 0
	}
	n := 0
	for _, c := range p {
		if c < '0' || c > '9' {
			return 0
		}
		n = n*10 + int(c-'0')
	}
	return n
}

// isAddrInUse reports whether err says the port is taken, on every platform (Windows words it differently).
func isAddrInUse(err error) bool {
	if errors.Is(err, syscall.EADDRINUSE) {
		return true
	}
	m := strings.ToLower(err.Error())
	return strings.Contains(m, "address already in use") || strings.Contains(m, "address in use") ||
		strings.Contains(m, "only one usage of each socket address")
}
