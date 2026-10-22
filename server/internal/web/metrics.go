package web

import (
	"bytes"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/sysmetrics"
)

// Optional Prometheus endpoint (Settings > Metrics). Off by default. When on, GET /metrics needs the header
// "Authorization: Bearer <token>"; the token is separate from every sign-in, shown once and stored as a hash only.
// The exposition is written by hand (text format 0.0.4), read-only, and carries no names, paths or secrets.

const metricsContentType = "text/plain; version=0.0.4; charset=utf-8"

// bearerToken returns the token of an "Authorization: Bearer" header, or "".
func bearerToken(r *http.Request) string {
	h := r.Header.Get("Authorization")
	if len(h) > 7 && strings.EqualFold(h[:7], "bearer ") {
		return strings.TrimSpace(h[7:])
	}
	return ""
}

// metricsGet answers the scrape. Off: 404 as if the route did not exist. On without a valid token: 401.
func (s *Server) metricsGet(w http.ResponseWriter, r *http.Request) {
	if !s.svc.MetricsEnabled(r.Context()) {
		http.NotFound(w, r)
		return
	}
	if !s.svc.CheckMetricsToken(r.Context(), bearerToken(r)) {
		w.Header().Set("WWW-Authenticate", `Bearer realm="FrameBeam Hub metrics"`)
		http.Error(w, "Unauthorized", http.StatusUnauthorized)
		return
	}
	body, err := s.metricsText(r)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	w.Header().Set("Content-Type", metricsContentType)
	w.Header().Set("Cache-Control", "no-store")
	w.Write(body)
}

// promWriter writes the Prometheus text exposition.
type promWriter struct{ buf bytes.Buffer }

// metric writes one family with a single sample. labels is "" or `key="value",...` (already escaped).
func (p *promWriter) metric(name, typ, help, labels string, value float64) {
	fmt.Fprintf(&p.buf, "# HELP %s %s\n# TYPE %s %s\n", name, help, name, typ)
	p.sample(name, labels, value)
}

func (p *promWriter) sample(name, labels string, value float64) {
	if labels != "" {
		labels = "{" + labels + "}"
	}
	fmt.Fprintf(&p.buf, "%s%s %s\n", name, labels, promFloat(value))
}

func (p *promWriter) header(name, typ, help string) {
	fmt.Fprintf(&p.buf, "# HELP %s %s\n# TYPE %s %s\n", name, help, name, typ)
}

// promFloat formats a sample value: integers without a decimal point.
func promFloat(v float64) string {
	if v == float64(int64(v)) {
		return fmt.Sprintf("%d", int64(v))
	}
	return fmt.Sprintf("%g", v)
}

// promLabel escapes a label value (backslash, double quote, newline).
func promLabel(v string) string {
	return strings.NewReplacer(`\`, `\\`, `"`, `\"`, "\n", `\n`).Replace(v)
}

func (s *Server) metricsText(r *http.Request) ([]byte, error) {
	ctx := r.Context()
	var p promWriter
	info := s.svc.Info()
	p.metric("framebeam_hub_info", "gauge", "FrameBeam Hub build information; the value is always 1.", `version="`+promLabel(info.HubVersion)+`"`, 1)
	p.metric("framebeam_hub_uptime_seconds", "gauge", "Seconds since the Hub process started.", "", time.Since(sysmetrics.Started).Seconds())

	sess, err := s.svc.ActiveSessionSummaries(ctx)
	if err != nil {
		return nil, err
	}
	viewers := 0
	for _, x := range sess {
		viewers += x.Viewers
	}
	p.metric("framebeam_sessions_active", "gauge", "Running Sessions.", "", float64(len(sess)))
	p.metric("framebeam_session_viewers", "gauge", "Viewers in running Sessions.", "", float64(viewers))
	p.metric("framebeam_players_connected", "gauge", "Players with an open connection to the Hub.", "", float64(len(s.svc.ConnectedDeviceIDs())))

	turnOn := 0.0
	if s.cfg.TURN != nil {
		turnOn = 1
	}
	p.metric("framebeam_turn_enabled", "gauge", "1 when the built-in TURN relay runs.", "", turnOn)
	if s.cfg.TURN != nil {
		st := s.cfg.TURN.Status()
		p.metric("framebeam_turn_allocations", "gauge", "Live relay allocations of the built-in TURN relay.", "", float64(st.Allocations))
		p.metric("framebeam_turn_relay_ports", "gauge", "Size of the relay port range.", "", float64(st.RelayMax-st.RelayMin+1))
	}

	if total, free, ok := sysmetrics.Disk(s.svc.DataDir()); ok {
		p.header("framebeam_disk_free_bytes", "gauge", "Free space of the volume holding the data folder.")
		p.sample("framebeam_disk_free_bytes", "", float64(free))
		p.header("framebeam_disk_total_bytes", "gauge", "Size of the volume holding the data folder.")
		p.sample("framebeam_disk_total_bytes", "", float64(total))
	}
	p.metric("framebeam_process_memory_bytes", "gauge", "Memory the Hub process holds from the operating system.", "", float64(sysmetrics.Memory().Process))
	if pct, ok := sysmetrics.CPUPercent(); ok {
		p.metric("framebeam_cpu_usage_ratio", "gauge", "Machine-wide CPU load, 0 to 1.", "", pct/100)
	}

	errs, warns := s.cfg.Errors.Totals()
	p.header("framebeam_log_records_total", "counter", "WARN and ERROR log records since the Hub started.")
	p.sample("framebeam_log_records_total", `level="error"`, float64(errs))
	p.sample("framebeam_log_records_total", `level="warn"`, float64(warns))
	return p.buf.Bytes(), nil
}

// ---- Settings > Metrics ----

type metricsSettings struct {
	On       bool
	TokenSet bool
	Token    string // shown once, right after it was created
	URL      string
}

func (s *Server) metricsSettings(r *http.Request, token string) metricsSettings {
	ctx := r.Context()
	return metricsSettings{On: s.svc.MetricsEnabled(ctx), TokenSet: s.svc.MetricsTokenSet(ctx), Token: token, URL: s.metricsURL(r)}
}

// settingsMetrics switches the endpoint on or off. Switching it on for the first time creates the token, which is
// shown in this response and never again. The response is rendered directly (no redirect), so the token never
// appears in a URL or a flash.
func (s *Server) settingsMetrics(w http.ResponseWriter, r *http.Request, sess *session) {
	on := r.PostFormValue("enabled") == "1"
	tok, err := s.svc.SetMetricsEnabled(r.Context(), on)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	s.log.Info("metrics endpoint switched", "enabled", on, "admin", sess.User.Username)
	s.renderSettings(w, r, sess, http.StatusOK, "metrics", settingsRes{OK: "metrics", Token: tok})
}

// settingsMetricsToken replaces the token; the old one stops working at once.
func (s *Server) settingsMetricsToken(w http.ResponseWriter, r *http.Request, sess *session) {
	tok, err := s.svc.RegenerateMetricsToken(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	s.log.Info("metrics token regenerated", "admin", sess.User.Username)
	s.renderSettings(w, r, sess, http.StatusOK, "metrics", settingsRes{OK: "metricstoken", Token: tok})
}
