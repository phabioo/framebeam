package web

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/health"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/statusinfo"
	"github.com/phabioo/framebeam/server/internal/sysmetrics"
)

// Admin Status page (design 6f): the Hub's health at a glance, refreshed every 10 s. The sidebar badge shows the
// title of the most urgent problem.

// statusRefresh is the polling interval of the page in seconds.
const statusRefresh = 10

// maxErrorRows caps the rows of "Last errors" (the log download has everything).
const maxErrorRows = 20

// problems runs the health checks (statusinfo, shared with the admin API).
func (s *Server) problems(ctx context.Context) []health.Problem { return s.src.Problems(ctx) }

var diskProblems = statusinfo.DiskProblems

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
func (s *Server) publicAddress(r *http.Request) string { return s.src.PublicAddress(r.Host) }

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
	st, err := s.src.Collect(ctx, r.Host)
	if err != nil {
		return statusBody{}, err
	}
	ver := st.Version
	if ver != "" && ver[0] >= '0' && ver[0] <= '9' {
		ver = "v" + ver
	}
	b := statusBody{HubName: st.HubName, Version: ver, Uptime: "up " + formatDuration(st.Uptime), Address: st.Address,
		Updated: st.Now.Local().Format("15:04:05"), Refresh: statusRefresh, Problem: health.MostUrgent(st.Problems)}

	// Meters.
	if st.CPUOK {
		b.Meters = append(b.Meters, meterView{Label: "CPU", Value: fmt.Sprintf("%.0f %%", st.CPUPercent), Percent: int(st.CPUPercent + .5), Show: true})
	} else {
		b.Meters = append(b.Meters, meterView{Label: "CPU", Value: "n/a", Sub: "Not available on this platform"})
	}
	mem := st.Memory
	mv := meterView{Label: "Memory", Value: humanBytes(mem.Process), Sub: "Hub process"}
	if mem.SystemOK && mem.SystemTotal > 0 {
		mv.Sub = fmt.Sprintf("Hub process · system %s of %s", humanBytes(mem.SystemUsed), humanBytes(mem.SystemTotal))
		mv.Percent, mv.Show = int(sysmetrics.UsedPercent(mem.SystemTotal, mem.SystemTotal-mem.SystemUsed)+.5), true
	}
	b.Meters = append(b.Meters, mv)
	dv := meterView{Label: "Disk · data", Value: "n/a", Sub: "Not available on this platform"}
	if st.DiskOK {
		pct := sysmetrics.UsedPercent(st.DiskTotal, st.DiskFree)
		dv = meterView{Label: "Disk · data", Value: humanBytes(st.DiskTotal - st.DiskFree), Sub: fmt.Sprintf("of %s · %s free", humanBytes(st.DiskTotal), humanBytes(st.DiskFree)),
			Percent: int(pct + .5), Show: true, Warn: pct >= health.DiskWarnPercent, Link: "/settings/saves", LinkText: "Saves retention →"}
	}
	b.Meters = append(b.Meters, dv)
	bv := meterView{Label: "Disk · backup", Value: "Off", Sub: "No backup folder is set", Link: "/settings/backups", LinkText: "Backups →"}
	if bd := st.Backup; bd.FolderSet && bd.OK {
		pct := sysmetrics.UsedPercent(bd.Total, bd.Free)
		bv = meterView{Label: "Disk · backup", Value: humanBytes(bd.Free) + " free", Sub: fmt.Sprintf("of %s · %s", humanBytes(bd.Total), bd.Folder),
			Percent: int(pct + .5), Show: true, Warn: pct >= health.DiskWarnPercent, Link: "/settings/backups", LinkText: "Backups →"}
	} else if bd.FolderSet {
		bv.Value, bv.Sub = "n/a", bd.Folder
	}
	b.Meters = append(b.Meters, bv)

	// Sessions.
	for _, x := range st.Sessions {
		b.Sessions = append(b.Sessions, statusSession{Title: x.Owner + " · " + x.GameTitle,
			Sub:     visibilityLabel(x.Visibility) + " · for " + formatDuration(st.Now.Sub(x.Since)),
			Viewers: plural(x.Viewers, "viewer", "viewers")})
	}
	b.SessionsHead = fmt.Sprintf("ACTIVE SESSIONS · %d · %s", len(st.Sessions), strings.ToUpper(plural(st.Viewers, "viewer", "viewers")))
	if st.TURN != nil {
		b.TURNOn = true
		b.TURNLine = fmt.Sprintf("TURN relay %d of %d", st.TURN.Allocations, st.TURN.RelayPorts)
	}

	// Players.
	for _, p := range st.Players {
		sp := statusPlayer{Device: p.DeviceName, Sub: p.User, Version: p.Version, State: playerStateLabel(p.State), Outdated: p.Outdated}
		if p.Outdated {
			sp.State += " · update available"
		}
		b.Players = append(b.Players, sp)
	}

	// Errors of the last 24 hours (already redacted).
	b.ErrorCount = len(st.Errors)
	for i, e := range st.Errors {
		if i >= maxErrorRows {
			break
		}
		lvl := "WARN"
		if statusinfo.IsError(e) {
			lvl = "ERROR"
		}
		b.Errors = append(b.Errors, statusError{Time: fmtWhen(e.Time, time.Now()), Level: lvl, Text: e.Message})
	}

	b.MetricsOn = st.MetricsOn
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
	w.Write(s.src.LogSnapshot())
}
