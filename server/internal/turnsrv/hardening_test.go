package turnsrv

import (
	"context"
	"errors"
	"net"
	"os"
	"sync/atomic"
	"testing"
	"time"
)

func TestAuthCacheAvoidsRepeatedLookups(t *testing.T) {
	now := time.Date(2026, 10, 19, 12, 0, 0, 0, time.UTC)
	c := newAuthCache(30*time.Second, func() time.Time { return now })
	var calls atomic.Int32
	lookup := func(_ context.Context, id string) bool { calls.Add(1); return id == "good" }
	for i := 0; i < 100; i++ {
		if !c.check(context.Background(), "good", lookup) {
			t.Fatal("good device refused")
		}
		if c.check(context.Background(), "bad", lookup) {
			t.Fatal("bad device accepted")
		}
	}
	if calls.Load() != 2 {
		t.Fatalf("lookups %d, want 2 (positive and negative result cached)", calls.Load())
	}
	now = now.Add(31 * time.Second) // expired: looked up again
	c.check(context.Background(), "good", lookup)
	if calls.Load() != 3 {
		t.Fatalf("lookups %d after expiry", calls.Load())
	}
	c.invalidate("good")
	c.check(context.Background(), "good", lookup)
	if calls.Load() != 4 {
		t.Fatalf("lookups %d after invalidate", calls.Load())
	}
}

func TestAuthCacheBoundedAndRateLimited(t *testing.T) {
	now := time.Date(2026, 10, 19, 12, 0, 0, 0, time.UTC)
	c := newAuthCache(time.Hour, func() time.Time { return now })
	var calls atomic.Int32
	lookup := func(context.Context, string) bool { calls.Add(1); return false }
	// A flood of random device IDs within one instant: only the burst reaches the database.
	for i := 0; i < 10000; i++ {
		c.check(context.Background(), "id-"+itoa(i), lookup)
	}
	if n := calls.Load(); n > int32(authLookupBurst) {
		t.Fatalf("%d lookups for a flood, burst is %v", n, authLookupBurst)
	}
	if len(c.m) > authCacheMax {
		t.Fatalf("cache grew to %d", len(c.m))
	}
	// Tokens come back over time.
	now = now.Add(time.Second)
	before := calls.Load()
	c.check(context.Background(), "fresh", lookup)
	if calls.Load() != before+1 {
		t.Fatal("no lookup after the limiter refilled")
	}
}

func TestLimitListenerIdleDeadlineAndPerIPCap(t *testing.T) {
	inner, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ln := &limitListener{Listener: inner, sem: make(chan struct{}, MaxTCPConns), perIP: map[string]int{},
		first: 150 * time.Millisecond, idle: 150 * time.Millisecond}
	defer ln.Close()
	accepted := make(chan net.Conn, 64)
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			accepted <- c
		}
	}()
	c, err := net.Dial("tcp", inner.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	srv := <-accepted
	// A silent connection is ended by the read deadline instead of holding a slot forever.
	_, err = srv.Read(make([]byte, 1))
	if !errors.Is(err, os.ErrDeadlineExceeded) {
		t.Fatalf("read on a silent connection: %v", err)
	}
	srv.Close()

	// Per-IP cap: all test connections come from 127.0.0.1.
	var open []net.Conn
	for i := 0; i < MaxTCPConnsPerIP+4; i++ {
		cc, err := net.Dial("tcp", inner.Addr().String())
		if err != nil {
			t.Fatal(err)
		}
		open = append(open, cc)
	}
	defer func() {
		for _, cc := range open {
			cc.Close()
		}
	}()
	time.Sleep(100 * time.Millisecond)
	if n := len(accepted); n != MaxTCPConnsPerIP {
		t.Fatalf("%d connections from one IP accepted, cap %d", n, MaxTCPConnsPerIP)
	}
}

func TestAuthCacheNegativeResultIsShortLived(t *testing.T) {
	now := time.Date(2026, 10, 19, 12, 0, 0, 0, time.UTC)
	c := newAuthCache(30*time.Second, func() time.Time { return now })
	healthy := false // simulates a transient database hiccup that ends
	var calls atomic.Int32
	lookup := func(context.Context, string) bool { calls.Add(1); return healthy }
	if c.check(context.Background(), "dev", lookup) {
		t.Fatal("accepted during the hiccup")
	}
	healthy = true
	now = now.Add(authNegativeTTL - time.Second)
	if c.check(context.Background(), "dev", lookup) || calls.Load() != 1 {
		t.Fatal("refusal not cached within the short window")
	}
	now = now.Add(2 * time.Second) // beyond the negative TTL, far below the positive TTL
	if !c.check(context.Background(), "dev", lookup) {
		t.Fatal("real device still locked out after the negative TTL")
	}
	if !c.check(context.Background(), "dev", lookup) || calls.Load() != 2 {
		t.Fatalf("positive result not cached, lookups %d", calls.Load())
	}
}

func TestAuthCacheDoesNotCacheTimedOutLookup(t *testing.T) {
	now := time.Date(2026, 10, 19, 12, 0, 0, 0, time.UTC)
	c := newAuthCache(30*time.Second, func() time.Time { return now })
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if c.check(ctx, "dev", func(context.Context, string) bool { return false }) {
		t.Fatal("accepted")
	}
	if len(c.m) != 0 {
		t.Fatal("timed-out lookup was cached")
	}
	if !c.check(context.Background(), "dev", func(context.Context, string) bool { return true }) {
		t.Fatal("device refused on retry")
	}
}
