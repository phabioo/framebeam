package main

import (
	"net"
	"net/http/httptest"
	"testing"
	"time"
)

func TestReadTimeoutFor(t *testing.T) {
	for _, tc := range []struct {
		method, path string
		want         time.Duration
	}{
		{"GET", "/library", requestReadTimeout},
		{"POST", "/users/u1/delete", requestReadTimeout},
		{"POST", "/login", requestReadTimeout},
		{"POST", "/api/v1/handshake", requestReadTimeout},
		{"GET", "/events", 0},
		{"GET", "/api/v1/ws", 0},
		{"POST", "/library/upload", uploadReadTimeout},
		{"POST", "/saves/u/g/slot1/upload", uploadReadTimeout},
		{"POST", "/systems/nds/firmware/bios7/upload", uploadReadTimeout},
		{"POST", "/api/v1/games", uploadReadTimeout},
		{"PUT", "/api/v1/games/g1/saves/slot1", uploadReadTimeout},
		{"POST", "/api/v1/games/g1/saves/slot1/upload", uploadReadTimeout},
		{"GET", "/api/v1/games", requestReadTimeout},
	} {
		if got := readTimeoutFor(httptest.NewRequest(tc.method, tc.path, nil)); got != tc.want {
			t.Errorf("%s %s: %v, want %v", tc.method, tc.path, got, tc.want)
		}
	}
}

func TestLimitListener(t *testing.T) {
	base, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	l := limitListener(base, 2)
	defer l.Close()
	accepted := make(chan net.Conn, 4)
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			accepted <- c
		}
	}()
	dial := func() net.Conn {
		c, err := net.Dial("tcp", base.Addr().String())
		if err != nil {
			t.Fatal(err)
		}
		return c
	}
	var clients []net.Conn
	for i := 0; i < 3; i++ {
		clients = append(clients, dial())
	}
	defer func() {
		for _, c := range clients {
			c.Close()
		}
	}()
	c1, c2 := <-accepted, <-accepted
	select {
	case <-accepted:
		t.Fatal("third connection accepted above the limit")
	case <-time.After(150 * time.Millisecond):
	}
	c1.Close()
	c1.Close() // idempotent
	select {
	case c3 := <-accepted:
		c3.Close()
	case <-time.After(2 * time.Second):
		t.Fatal("slot not released")
	}
	c2.Close()
}
