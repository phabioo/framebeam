package hub

import (
	"context"
	"sync"
)

// Event topics published on the in-process bus. Events carry only the topic;
// subscribers (the web interface) re-fetch whatever they display.
const (
	TopicClients = "clients"
	TopicSaves   = "saves"
	TopicLibrary = "library"
	TopicSystems = "systems"
	TopicUsers   = "users"
	TopicUpdates = "updates"
)

// Event is a change notification without data.
type Event struct {
	Topic string
}

// eventBus is a tiny pub/sub. Each subscriber keeps a set of pending topics, so a slow
// subscriber coalesces (never blocks the publisher and never grows without bound).
type eventBus struct {
	mu   sync.Mutex
	subs map[*subscriber]struct{}
}

type subscriber struct {
	mu      sync.Mutex
	pending map[string]struct{}
	order   []string
	signal  chan struct{} // capacity 1: "pending changed"
}

// Subscribe returns a channel of events that is closed when ctx ends (this is also the unsubscribe).
// Events of a topic that is already pending for this subscriber are merged.
func (s *Service) Subscribe(ctx context.Context) <-chan Event {
	sub := &subscriber{pending: map[string]struct{}{}, signal: make(chan struct{}, 1)}
	out := make(chan Event, 8)
	b := &s.bus
	b.mu.Lock()
	if b.subs == nil {
		b.subs = map[*subscriber]struct{}{}
	}
	b.subs[sub] = struct{}{}
	b.mu.Unlock()
	go func() {
		defer func() {
			b.mu.Lock()
			delete(b.subs, sub)
			b.mu.Unlock()
			close(out)
		}()
		for {
			select {
			case <-ctx.Done():
				return
			case <-sub.signal:
			}
			for _, topic := range sub.take() {
				select {
				case out <- Event{Topic: topic}:
				case <-ctx.Done():
					return
				}
			}
		}
	}()
	return out
}

func (sub *subscriber) take() []string {
	sub.mu.Lock()
	defer sub.mu.Unlock()
	t := sub.order
	sub.order = nil
	sub.pending = map[string]struct{}{}
	return t
}

// Publish notifies all subscribers of a change in the given topics. It never blocks.
func (s *Service) Publish(topics ...string) {
	b := &s.bus
	b.mu.Lock()
	defer b.mu.Unlock()
	for sub := range b.subs {
		sub.mu.Lock()
		for _, t := range topics {
			if _, ok := sub.pending[t]; !ok {
				sub.pending[t] = struct{}{}
				sub.order = append(sub.order, t)
			}
		}
		sub.mu.Unlock()
		select {
		case sub.signal <- struct{}{}:
		default:
		}
	}
}

// publishOK publishes topics when the surrounding service method succeeded:
// use as `defer s.publishOK(&err, TopicX)` with a named result err.
func (s *Service) publishOK(errp *error, topics ...string) {
	if *errp == nil {
		s.Publish(topics...)
	}
}
