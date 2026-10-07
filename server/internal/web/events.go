package web

import (
	"fmt"
	"net/http"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
)

const (
	sseHeartbeat = 25 * time.Second
	// sseCoalesce is the minimum gap between two events of the same topic.
	sseCoalesce = 500 * time.Millisecond
)

// badgeTopics are the topics that change the sidebar badges.
var badgeTopics = map[string]bool{
	hub.TopicClients: true, hub.TopicSaves: true, hub.TopicSystems: true, hub.TopicUpdates: true,
}

// events is the Server-Sent Events stream (GET /events, admin session via guard). Each event is
// "event: <topic>\ndata: 1\n\n" without payload; the browser re-fetches what it shows. Bursts are
// coalesced: at most one event per topic per sseCoalesce. "badges" accompanies badge-relevant topics.
func (s *Server) events(w http.ResponseWriter, r *http.Request, _ *session) {
	s.serveEvents(w, r, sseHeartbeat, sseCoalesce)
}

func (s *Server) serveEvents(w http.ResponseWriter, r *http.Request, heartbeat, coalesce time.Duration) {
	rc := http.NewResponseController(w) // follows Unwrap() of wrapping writers (request logging)
	// A server-wide WriteTimeout would kill the stream; lift the deadline if the server supports it.
	_ = rc.SetWriteDeadline(time.Time{})
	h := w.Header()
	h.Set("Content-Type", "text/event-stream")
	h.Set("Cache-Control", "no-store")
	h.Set("X-Accel-Buffering", "no")
	w.WriteHeader(http.StatusOK)
	fmt.Fprint(w, "retry: 3000\n: connected\n\n")
	if err := rc.Flush(); err != nil {
		return // no streaming support; the client gets an empty 200 and retries
	}

	ctx := r.Context()
	ch := s.svc.Subscribe(ctx)
	hb := time.NewTicker(heartbeat)
	defer hb.Stop()
	last := map[string]time.Time{} // last send per topic
	held := map[string]bool{}      // topics waiting for the coalesce window to pass
	timer := time.NewTimer(time.Hour)
	timer.Stop()
	defer timer.Stop()

	send := func(topic string) {
		fmt.Fprintf(w, "event: %s\ndata: 1\n\n", topic)
		last[topic] = time.Now()
	}
	// emit sends topic now, or holds it until the window of its previous event has passed.
	emit := func(topic string) bool {
		if wait := coalesce - time.Since(last[topic]); wait > 0 {
			if !held[topic] {
				if len(held) == 0 { // otherwise the pending timer reschedules itself
					timer.Reset(wait)
				}
				held[topic] = true
			}
			return false
		}
		send(topic)
		return true
	}
	for {
		select {
		case <-ctx.Done():
			return
		case <-s.done:
			return
		case <-hb.C:
			fmt.Fprint(w, ": keepalive\n\n")
			rc.Flush()
		case ev, ok := <-ch:
			if !ok {
				return
			}
			sent := emit(ev.Topic)
			if badgeTopics[ev.Topic] {
				sent = emit("badges") || sent
			}
			if sent {
				rc.Flush()
			}
		case <-timer.C:
			var next time.Duration
			for t := range held {
				if wait := coalesce - time.Since(last[t]); wait > 0 {
					if next == 0 || wait < next {
						next = wait
					}
					continue
				}
				delete(held, t)
				send(t)
			}
			rc.Flush()
			if next > 0 {
				timer.Reset(next)
			}
		}
	}
}
