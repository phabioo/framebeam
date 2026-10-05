// Package hubtest stellt Test-Hilfen bereit: temporäres Datenverzeichnis, schnelle Argon2-Parameter,
// injizierbare Uhr. Nur für Tests.
package hubtest

import (
	"context"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/auth"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/store"
)

// Clock ist eine steuerbare Uhr.
type Clock struct {
	mu sync.Mutex
	t  time.Time
}

// Now liefert die aktuelle Testzeit.
func (c *Clock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }

// Advance verschiebt die Uhr.
func (c *Clock) Advance(d time.Duration) { c.mu.Lock(); c.t = c.t.Add(d); c.mu.Unlock() }

// New erzeugt einen Service auf einem temporären Datenverzeichnis. mod darf die Optionen anpassen.
func New(t *testing.T, mod func(*hub.Options)) (*hub.Service, *Clock) {
	t.Helper()
	dir := t.TempDir()
	db, err := store.Open(filepath.Join(dir, "framebeam.db"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })
	clk := &Clock{t: time.Date(2026, 10, 5, 12, 0, 0, 0, time.UTC)}
	fast := auth.Params{Time: 1, MemoryKiB: 64, Threads: 1, KeyLen: 32, SaltLen: 16}
	o := hub.Options{DataDir: dir, Name: "Test-Hub", HubVersion: "0.0.0-test", Now: clk.Now, PasswordParams: &fast}
	if mod != nil {
		mod(&o)
	}
	svc, err := hub.Open(context.Background(), db, o)
	if err != nil {
		t.Fatal(err)
	}
	return svc, clk
}
