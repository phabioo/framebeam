package hub

import (
	"context"
	"testing"
	"time"
)

func recvTopic(t *testing.T, ch <-chan Event) string {
	t.Helper()
	select {
	case ev, ok := <-ch:
		if !ok {
			t.Fatal("channel closed")
		}
		return ev.Topic
	case <-time.After(2 * time.Second):
		t.Fatal("timeout waiting for event")
	}
	return ""
}

func TestEventBusFanOut(t *testing.T) {
	s := &Service{}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	a, b := s.Subscribe(ctx), s.Subscribe(ctx)
	s.Publish(TopicSaves)
	if got := recvTopic(t, a); got != TopicSaves {
		t.Fatalf("a got %q", got)
	}
	if got := recvTopic(t, b); got != TopicSaves {
		t.Fatalf("b got %q", got)
	}
}

func TestEventBusUnsubscribe(t *testing.T) {
	s := &Service{}
	ctx, cancel := context.WithCancel(context.Background())
	ch := s.Subscribe(ctx)
	cancel()
	select {
	case _, ok := <-ch:
		if ok {
			t.Fatal("unexpected event")
		}
	case <-time.After(2 * time.Second):
		t.Fatal("channel not closed after cancel")
	}
	time.Sleep(20 * time.Millisecond)
	s.bus.mu.Lock()
	n := len(s.bus.subs)
	s.bus.mu.Unlock()
	if n != 0 {
		t.Fatalf("subscriber not removed: %d", n)
	}
}

func TestEventBusSlowSubscriberDoesNotBlock(t *testing.T) {
	s := &Service{}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	slow := s.Subscribe(ctx) // never read
	fast := s.Subscribe(ctx)
	done := make(chan struct{})
	go func() {
		for i := 0; i < 10000; i++ {
			s.Publish(TopicClients, TopicSaves, TopicLibrary)
		}
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		t.Fatal("publisher blocked by slow subscriber")
	}
	if got := recvTopic(t, fast); got == "" {
		t.Fatal("fast subscriber got nothing")
	}
	// The slow subscriber still sees each pending topic once it reads (coalesced).
	seen := map[string]bool{}
	for i := 0; i < 3; i++ {
		seen[recvTopic(t, slow)] = true
	}
	if len(seen) != 3 {
		t.Fatalf("expected 3 distinct topics, got %v", seen)
	}
}
