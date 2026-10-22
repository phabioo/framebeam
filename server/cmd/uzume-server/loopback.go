package main

import (
	"errors"
	"net"
	"sync"
)

// listenLoopback binds the port of addr on 127.0.0.1 and [::1] only (Network sharing off). A missing IPv6 loopback
// is tolerated as long as 127.0.0.1 works. The returned listener accepts on both.
func listenLoopback(addr string) (net.Listener, error) {
	_, port, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, err
	}
	l4, err := net.Listen("tcp", net.JoinHostPort("127.0.0.1", port))
	if err != nil {
		return nil, err
	}
	if port == "0" {
		_, port, _ = net.SplitHostPort(l4.Addr().String())
	}
	l6, err := net.Listen("tcp", net.JoinHostPort("::1", port))
	if err != nil {
		return l4, nil // no IPv6 loopback (or its port is taken): IPv4 loopback alone is still loopback only
	}
	return newMultiListener(l4, l6), nil
}

// multiListener accepts connections from several listeners.
type multiListener struct {
	lns   []net.Listener
	conns chan net.Conn
	errs  chan error
	done  chan struct{}
	once  sync.Once
}

func newMultiListener(lns ...net.Listener) *multiListener {
	m := &multiListener{lns: lns, conns: make(chan net.Conn), errs: make(chan error, len(lns)), done: make(chan struct{})}
	for _, ln := range lns {
		go func(ln net.Listener) {
			for {
				c, err := ln.Accept()
				if err != nil {
					select {
					case m.errs <- err:
					case <-m.done:
					}
					return
				}
				select {
				case m.conns <- c:
				case <-m.done:
					c.Close()
					return
				}
			}
		}(ln)
	}
	return m
}

func (m *multiListener) Accept() (net.Conn, error) {
	select {
	case c := <-m.conns:
		return c, nil
	case err := <-m.errs:
		return nil, err
	case <-m.done:
		return nil, net.ErrClosed
	}
}

func (m *multiListener) Close() error {
	var err error
	m.once.Do(func() {
		close(m.done)
		for _, ln := range m.lns {
			err = errors.Join(err, ln.Close())
		}
	})
	return err
}

func (m *multiListener) Addr() net.Addr { return m.lns[0].Addr() }
