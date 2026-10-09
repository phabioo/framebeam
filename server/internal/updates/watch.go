package updates

import (
	"context"
	"time"
)

// DefaultWatchInterval is how often Watch looks for the request file.
const DefaultWatchInterval = 5 * time.Second

// WatchOptions configure Watch.
type WatchOptions struct {
	// RequestFile is the path of the request file (see RequestPath).
	RequestFile string
	// Interval defaults to DefaultWatchInterval.
	Interval time.Duration
	// Apply runs when the request file exists; ApplyStaged removes it first, so a failed apply is not retried
	// until the Hub writes a new request. Required.
	Apply func(ctx context.Context) error
	// Done is called after every Apply with its error (optional; used for logging).
	Done func(err error)
}

// Watch polls for the request file until ctx ends. It is the Windows counterpart of the systemd path unit: the
// privileged helper service waits here and applies the staged update when the Hub asks for it. A request that is
// already present at start is handled immediately.
func Watch(ctx context.Context, o WatchOptions) {
	if o.Interval <= 0 {
		o.Interval = DefaultWatchInterval
	}
	tick := time.NewTicker(o.Interval)
	defer tick.Stop()
	for {
		if ctx.Err() != nil {
			return
		}
		if o.RequestFile != "" && o.Apply != nil && requestExists(o.RequestFile) {
			err := o.Apply(ctx)
			if o.Done != nil {
				o.Done(err)
			}
		}
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
		}
	}
}
