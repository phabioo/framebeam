package hub

import (
	"net/netip"
	"testing"
)

func TestConnectedPlayerIP(t *testing.T) {
	s := &Service{}
	s.sess.rt.conns = map[string]*Client{}
	c := &Client{svc: s, deviceID: "dev"}
	c.SetRemoteAddr("[::ffff:192.168.1.20]:51234")
	s.sess.rt.conns["dev"] = c
	if !s.ConnectedPlayerIP(netip.MustParseAddr("192.168.1.20")) {
		t.Error("connected IP not found")
	}
	if !s.ConnectedPlayerIP(netip.MustParseAddr("::ffff:192.168.1.20")) {
		t.Error("mapped form not found")
	}
	if s.ConnectedPlayerIP(netip.MustParseAddr("192.168.1.21")) {
		t.Error("unknown IP accepted")
	}
	c2 := &Client{svc: s, deviceID: "dev2"}
	c2.SetRemoteAddr("garbage")
	s.sess.rt.conns["dev2"] = c2
	if s.ConnectedPlayerIP(netip.Addr{}) {
		t.Error("zero addr matched")
	}
}
