// Package statusinfo gathers the data of the Hub health view once, for the web Status page and the admin REST API,
// so both show the same numbers and the same problem list.
package statusinfo

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/diag"
	"github.com/phabioo/framebeam/server/internal/health"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/sysmetrics"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// ErrorsWindow is how far back the error list looks.
const ErrorsWindow = 24 * time.Hour

// Source is what the Hub health view reads. Zero values are fine for optional parts.
type Source struct {
	Svc *hub.Service
	// TURN is the embedded TURN server (nil: off).
	TURN *turnsrv.Server
	// PublicHost and PublicPort (0: unknown) are the externally used address; ListenPort is the fallback port.
	PublicHost string
	PublicPort int
	ListenPort func() int
	// TURNIssues are the messages of a TURN configuration that could not be started.
	TURNIssues []string
	// Errors and Log default to diag.RecentErrors and diag.Recent.
	Errors *diag.ErrorRing
	Log    *diag.Ring
	// Disk reads the volume of a directory (default sysmetrics.Disk; tests replace it).
	Disk func(dir string) (total, free uint64, ok bool)
}

func (s *Source) disk() (uint64, uint64, bool) {
	if s.Disk != nil {
		return s.Disk(s.Svc.DataDir())
	}
	return sysmetrics.Disk(s.Svc.DataDir())
}

func (s *Source) errors() *diag.ErrorRing {
	if s.Errors != nil {
		return s.Errors
	}
	return diag.RecentErrors
}

// LogSnapshot returns the recent log lines, redacted.
func (s *Source) LogSnapshot() []byte {
	l := s.Log
	if l == nil {
		l = diag.Recent
	}
	return diag.Redact(l.Snapshot())
}

// ErrorTotals returns the WARN and ERROR record counts since start.
func (s *Source) ErrorTotals() (errors, warns uint64) { return s.errors().Totals() }

// Problems runs the health checks and returns the open problems, most urgent first. A later change adds a check
// here (for example "Backup failed"); the banner, the sidebar badge and the API follow.
func (s *Source) Problems(ctx context.Context) []health.Problem {
	var ps []health.Problem
	if total, free, ok := s.disk(); ok {
		ps = append(ps, DiskProblems(total, free)...)
	}
	for _, m := range s.TURNIssues {
		ps = append(ps, health.Problem{ID: "turn", Title: "TURN relay down", Detail: m, Severity: health.Error,
			ActionLabel: "Open Network", ActionHref: "/settings/network"})
	}
	if f := s.Svc.LastUpdateFailure(); f != nil {
		detail := "Version " + f.Version + " could not be installed"
		if f.Message != "" {
			detail += ": " + f.Message
		}
		ps = append(ps, health.Problem{ID: "update", Title: "Update failed", Detail: detail, Severity: health.Warn,
			ActionLabel: "Open Updates", ActionHref: "/settings/updates"})
	}
	health.Sort(ps)
	return ps
}

// DiskProblems is the data folder check: above 90 % used is a warning, above 97 % an error.
func DiskProblems(total, free uint64) []health.Problem {
	pct := sysmetrics.UsedPercent(total, free)
	if pct < health.DiskWarnPercent {
		return nil
	}
	sev := health.Warn
	if pct >= health.DiskErrorPercent {
		sev = health.Error
	}
	return []health.Problem{{ID: "disk", Title: fmt.Sprintf("Disk %.0f %%", pct), Severity: sev,
		Detail:      fmt.Sprintf("Only %s of %s are free on the data folder. Saves, uploads and updates need space.", HumanBytes(free), HumanBytes(total)),
		ActionLabel: "Open Saves retention", ActionHref: "/settings/saves"}}
}

// HumanBytes formats bytes with one decimal (steps of 1024).
func HumanBytes(n uint64) string {
	v := float64(n)
	suffix := []string{"B", "KB", "MB", "GB", "TB"}
	i := 0
	for v >= 1024 && i < len(suffix)-1 {
		v /= 1024
		i++
	}
	if i == 0 {
		return fmt.Sprintf("%d B", n)
	}
	return fmt.Sprintf("%.1f %s", v, suffix[i])
}

// PublicAddress is the address Players use: the public host with its port if set, else fallbackHost.
func (s *Source) PublicAddress(fallbackHost string) string {
	if h := strings.TrimSpace(s.PublicHost); h != "" {
		port := s.PublicPort
		if port <= 0 && s.ListenPort != nil {
			port = s.ListenPort()
		}
		if port > 0 {
			return net.JoinHostPort(h, strconv.Itoa(port))
		}
		return h
	}
	return fallbackHost
}

// Status is the data of the health view.
type Status struct {
	HubName, Version string
	Uptime           time.Duration
	Address          string
	Now              time.Time
	Problems         []health.Problem
	CPUPercent       float64
	CPUOK            bool
	Memory           sysmetrics.Mem
	DiskTotal        uint64
	DiskFree         uint64
	DiskOK           bool
	Sessions         []hub.SessionSummary
	Viewers          int
	TURN             *TURNStatus
	Players          []hub.PlayerStatus
	Errors           []diag.Entry // newest first, redacted
	MetricsOn        bool
}

// TURNStatus is the load of the embedded TURN relay.
type TURNStatus struct {
	Allocations int
	RelayPorts  int
}

// Collect gathers the status. fallbackHost is the host the admin used (shown when no public address is set).
func (s *Source) Collect(ctx context.Context, fallbackHost string) (Status, error) {
	info := s.Svc.Info()
	st := Status{HubName: info.Name, Version: info.HubVersion, Uptime: time.Since(sysmetrics.Started),
		Address: s.PublicAddress(fallbackHost), Now: s.Svc.Now(), Problems: s.Problems(ctx)}
	st.CPUPercent, st.CPUOK = sysmetrics.CPUPercent()
	st.Memory = sysmetrics.Memory()
	st.DiskTotal, st.DiskFree, st.DiskOK = s.disk()
	var err error
	if st.Sessions, err = s.Svc.ActiveSessionSummaries(ctx); err != nil {
		return st, err
	}
	for _, x := range st.Sessions {
		st.Viewers += x.Viewers
	}
	if s.TURN != nil {
		t := s.TURN.Status()
		st.TURN = &TURNStatus{Allocations: t.Allocations, RelayPorts: t.RelayMax - t.RelayMin + 1}
	}
	if st.Players, err = s.Svc.ConnectedPlayers(ctx); err != nil {
		return st, err
	}
	for _, e := range s.errors().Since(time.Now().Add(-ErrorsWindow)) {
		e.Message = diag.RedactString(e.Message)
		st.Errors = append(st.Errors, e)
	}
	st.MetricsOn = s.Svc.MetricsEnabled(ctx)
	return st, nil
}

// IsError reports an ERROR (not WARN) record.
func IsError(e diag.Entry) bool { return e.Level >= slog.LevelError }
