package main

import (
	"net"
	"testing"
)

func TestListenLoopbackOnly(t *testing.T) {
	ln, err := listenLoopback(":0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	host, port, _ := net.SplitHostPort(ln.Addr().String())
	if host != "127.0.0.1" {
		t.Fatalf("bound to %s", ln.Addr())
	}
	accepted := make(chan net.Addr, 2)
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			accepted <- c.RemoteAddr()
			c.Close()
		}
	}()
	c, err := net.Dial("tcp", net.JoinHostPort("127.0.0.1", port))
	if err != nil {
		t.Fatal(err)
	}
	c.Close()
	<-accepted
	if c6, err := net.Dial("tcp", net.JoinHostPort("::1", port)); err == nil { // only where IPv6 loopback exists
		c6.Close()
		<-accepted
	}
	if err := ln.Close(); err != nil {
		t.Fatal(err)
	}
}
