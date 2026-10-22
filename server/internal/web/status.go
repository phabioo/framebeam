package web

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/diag"
	"github.com/phabioo/framebeam/server/internal/health"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/sysmetrics"
)

// Admin Status page (design 6f): the Hub's health at a glance, refreshed every 10 s. The sidebar badge shows the
// title of the most urgent problem.

// statusRefresh is the polling interval of the page in seconds.
const statusRefresh = 10

// errorsWindow is how far back "Last errors" looks.
const errorsWindow = 24 * time.Hour

// maxErrorRows caps the rows of "Last errors" (the log download has everything).
const maxErrorRows = 20

// problems runs the health checks and returns the open problems, most urgent first. A later change adds a check
// here (for example "Backup failed"); the banner and the sidebar badge follow.
func (s *Server) problems(ctx context.Context) []health.Problem {
	var ps []health.Problem
	if total, free, ok := sysmetrics.Disk(s.svc.DataDir()); ok {
		ps = append(ps, diskProblems(total, free)...)
	}
	for _, is := range s.cfg.Net.Issues {
		if is.Key == config.NetTURN {
			ps = append(ps, health.Problem{ID: "turn", Title: "TURN relay down", Detail: is.Message, Severity: health.Error,
				ActionLabel: "Open Network", ActionHref: "/settings/network"})
		}
	}
	if f := s.svc.LastUpdateFailure(); f != nil {
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

// diskProblems is the data folder check: above 90 % used is a warning, above 97 % an error.
func diskProblems(total, free uint64) []health.Problem {
	pct := sysmetrics.UsedPercent(total, free)
	if pct < health.DiskWarnPercent {
		return nil
	}
	sev := health.Warn
	if pct >= health.DiskErrorPercent {
		sev = health.Error
	}
	return []health.Problem{{ID: "disk", Title: fmt.Sprintf("Disk %.0f %%", pct), Severity: sev,
		Detail:      fmt.Sprintf("Only %s of %s are free on the data folder. Saves, uploads and updates need space.", humanBytes(free), humanBytes(total)),
		ActionLabel: "Open Saves retention", ActionHref: "/settings/saves"}}
}

// ---- view model ----

type meterView struct {
	Label, Value, Sub string
	Percent           int
	Show              bool // a bar is drawn
	Warn              bool
	Link, LinkText    string
}

type statusSession struct {
	Title, Sub, Viewers string
}

type statusPlayer struct {
	Device, Sub, Version, State string
	Outdated                    bool
}

type statusError struct {
	Time  When
	Level string
	Text  string
}

type statusBody struct {
	HubName, Version, Uptime, Address string
	Updated                           string
	Refresh                           int
	Problem                           *health.Problem
	Meters                            []meterView
	Sessions                          []statusSession
	SessionsHead                      string
	TURNOn                            bool
	TURNLine                          string
	Players                           []statusPlayer
	Errors                            []statusError
	ErrorCount                        int
	MetricsOn                         bool
	MetricsURL                        string
}

// publicAddress is the address Players use: the public host with its port if set, else the host of this request.
func (s *Server) publicAddress(r *http.Request) string {
	if h := strings.TrimSpace(s.cfg.PublicHost); h != "" {
		port := s.cfg.PublicPort
		if port <= 0 {
			port = s.cfg.Net.Running.ListenPort()
		}
		if port > 0 {
			return net.JoinHostPort(h, strconv.Itoa(port))
		}
		return h
	}
	return r.Host
}

// formatDuration is a coarse uptime: "12 days 4 h", "3 h 12 min", "5 min".
func formatDuration(d time.Duration) string {
	if d < time.Minute {
		return "under 1 min"
	}
	days, hours, mins := int(d.Hours())/24, int(d.Hours())%24, int(d.Minutes())%60
	switch {
	case days > 0:
		return fmt.Sprintf("%s %d h", plural(days, "day", "days"), hours)
	case hours > 0:
		return fmt.Sprintf("%d h %d min", hours, mins)
	}
	return fmt.Sprintf("%d min", mins)
}

func (s *Server) statusData(r *http.Request) (statusBody, error) {
	ctx := r.Context()
	now := s.svc.Now()
	info := s.svc.Info()
	ver := info.HubVersion
	if ver != "" && ver[0] >= '0' && ver[0] <= '9' {
		ver = "v" + ver
	}
	b := statusBody{HubName: info.Name, Version: ver, Uptime: "up " + formatDuration(time.Since(sysmetrics.Started)),
		Address: s.publicAddress(r), Updated: now.Local().Format("15:04:05"), Refresh: statusRefresh,
		Problem: health.MostUrgent(s.problems(ctx))}

	// Meters.
	if pct, ok := sysmetrics.CPUPercent(); ok {
		b.Meters = append(b.Meters, meterView{Label: "CPU", Value: fmt.Sprintf("%.0f %%", pct), Percent: int(pct + .5), Show: true})
	} else {
		b.Meters = append(b.Meters, meterView{Label: "CPU", Value: "n/a", Sub: "Not available on this platform"})
	}
	mem := sysmetrics.Memory()
	mv := meterView{Label: "Memory", Value: humanBytes(mem.Process), Sub: "Hub process"}
	if mem.SystemOK && mem.SystemTotal > 0 {
		mv.Sub = fmt.Sprintf("Hub process · system %s of %s", humanBytes(mem.SystemUsed), humanBytes(mem.SystemTotal))
		mv.Percent, mv.Show = int(sysmetrics.UsedPercent(mem.SystemTotal, mem.SystemTotal-mem.SystemUsed)+.5), true
	}
	b.Meters = append(b.Meters, mv)
	dv := meterView{Label: "Disk · data", Value: "n/a", Sub: "Not available on this platform"}
	if total, free, ok := sysmetrics.Disk(s.svc.DataDir()); ok {
		pct := sysmetrics.UsedPercent(total, free)
		dv = meterView{Label: "Disk · data", Value: humanBytes(total - free), Sub: fmt.Sprintf("of %s · %s free", humanBytes(total), humanBytes(free)),
			Percent: int(pct + .5), Show: true, Warn: pct >= health.DiskWarnPercent, Link: "/settings/saves", LinkText: "Saves retention →"}
	}
	b.Meters = append(b.Meters, dv)

	// Sessions.
	sess, err := s.svc.ActiveSessionSummaries(ctx)
	if err != nil {
		return b, err
	}
	viewers := 0
	for _, x := range sess {
		viewers += x.Viewers
		b.Sessions = append(b.Sessions, statusSession{Title: x.Owner + " · " + x.GameTitle,
			Sub:     visibilityLabel(x.Visibility) + " · for " + formatDuration(now.Sub(x.Since)),
			Viewers: plural(x.Viewers, "viewer", "viewers")})
	}
	b.SessionsHead = fmt.Sprintf("ACTIVE SESSIONS · %d · %s", len(sess), strings.ToUpper(plural(viewers, "viewer", "viewers")))
	if s.cfg.TURN != nil {
		st := s.cfg.TURN.Status()
		b.TURNOn = true
		b.TURNLine = fmt.Sprintf("TURN relay %d of %d", st.Allocations, st.RelayMax-st.RelayMin+1)
	}

	// Players.
	pl, err := s.svc.ConnectedPlayers(ctx)
	if err != nil {
		return b, err
	}
	for _, p := range pl {
		sp := statusPlayer{Device: p.DeviceName, Sub: p.User, Version: p.Version, State: playerStateLabel(p.State), Outdated: p.Outdated}
		if p.Outdated {
			sp.State += " · update available"
		}
		b.Players = append(b.Players, sp)
	}

	// Errors of the last 24 hours.
	ents := s.cfg.Errors.Since(time.Now().Add(-errorsWindow))
	b.ErrorCount = len(ents)
	for i, e := range ents {
		if i >= maxErrorRows {
			break
		}
		lvl := "WARN"
		if e.Level >= slog.LevelError {
			lvl = "ERROR"
		}
		b.Errors = append(b.Errors, statusError{Time: fmtWhen(e.Time, time.Now()), Level: lvl, Text: diag.RedactString(e.Message)})
	}

	// Metrics endpoint.
	b.MetricsOn = s.svc.MetricsEnabled(ctx)
	b.MetricsURL = s.metricsURL(r)
	return b, nil
}

func visibilityLabel(v hub.Visibility) string {
	switch v {
	case hub.VisibilityPrivate:
		return "Private"
	case hub.VisibilityInviteOnly:
		return "Invite only"
	}
	return "Hub users"
}

func playerStateLabel(st string) string {
	switch st {
	case hub.PlayerInSession:
		return "in Session"
	case hub.PlayerWatching:
		return "watching"
	case hub.PlayerPlaying:
		return "playing"
	}
	return "idle"
}

// metricsURL is the address of the metrics endpoint as a monitoring tool reaches it.
func (s *Server) metricsURL(r *http.Request) string {
	scheme := "http"
	if s.secureReq(r) {
		scheme = "https"
	}
	return scheme + "://" + s.publicAddress(r) + "/metrics"
}

// ---- handlers ----

// statusGet renders the page; the polling request (target #status-body) gets only the body.
func (s *Server) statusGet(w http.ResponseWriter, r *http.Request, sess *session) {
	b, err := s.statusData(r)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "status", "Status")
	d.Body = b
	if isHX(r, "status-body") {
		s.render(w, http.StatusOK, "status", "status-body", d)
		return
	}
	s.render(w, http.StatusOK, "status", "layout", d)
}

// statusLog downloads the recent log lines (redacted like the diagnostics bundle).
func (s *Server) statusLog(w http.ResponseWriter, r *http.Request, sess *session) {
	h := w.Header()
	h.Set("Content-Type", "text/plain; charset=utf-8")
	h.Set("Content-Disposition", `attachment; filename="framebeam-hub-log-`+time.Now().UTC().Format("20060102-150405")+`.txt"`)
	h.Set("Cache-Control", "no-store")
	w.Write(diag.Redact(s.cfg.Log.Snapshot()))
}
