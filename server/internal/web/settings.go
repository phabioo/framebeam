package web

import (
	"context"
	"errors"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/tlsutil"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

// NetConfig is what the Network settings need to know about the running Hub (set by cmd/framebeam-hub).
type NetConfig struct {
	// Base is the configuration from hub.env and flags, without the values saved on the web interface.
	Base config.Config
	// Running is the effective configuration the Hub started with (Base plus saved values, after fallbacks).
	Running config.Config
	// Issues are saved values that could not be applied at startup (the Hub then runs with a fallback).
	Issues []NetIssue
	// RequestRestart asks the process to shut down gracefully and re-execute itself (nil: not supported).
	RequestRestart func()
	// TestBind checks that the Hub can listen on an address (default config.TestBind).
	TestBind func(addr string) error
}

// NetIssue is a saved network value that could not be used at startup.
type NetIssue struct {
	Key     string // config.Net* key
	Value   string // the saved raw value that failed
	Message string // shown in the Network section
}

// NetSignature identifies the value of a setting for NetIssue.Value. The TURN setting is the combination of all
// TURN related values, so that an issue disappears as soon as any of them is changed.
func NetSignature(c *config.Config, key string) string {
	if key == config.NetTURN {
		return strings.Join([]string{c.NetValue(config.NetTURN), c.NetValue(config.NetPublicHost), c.NetValue(config.NetTURNPort),
			c.NetValue(config.NetRelayPorts), c.NetValue(config.NetRelayIP)}, "|")
	}
	return c.NetValue(key)
}

// turnKeys are the settings that make up the TURN configuration.
var turnKeys = map[string]bool{config.NetTURN: true, config.NetPublicHost: true, config.NetTURNPort: true,
	config.NetRelayPorts: true, config.NetRelayIP: true}

// Settings sections (GET /settings/{section}).
var settingsSections = []struct{ ID, Title, Subtitle string }{
	{"updates", "Updates", "Versions, channel and automatic installs for this hub"},
	{"general", "General", "How the hub appears to Players and users"},
	{"network", "Network", "Ports, public address and relay for Players outside your network"},
	{"security", "Security", "Transport, certificate and the admin account"},
}

// settingsFlash holds the success texts of the settings pages that are not in the shared flash table.
var settingsFlash = map[string]string{
	"netsaved":   "Network setting saved.",
	"netreset":   "Reset to the hub.env value.",
	"namesaved":  "Hub name saved.",
	"restarting": "The hub is restarting.",
}

// settingsQuiet lists the success keys of autosaving fields: the saved indicator is enough, no banner.
var settingsQuiet = map[string]bool{"name": true, "uploads": true, "updates": true, "netsaved": true, "netreset": true}

// ---- view models ----

type settingsBody struct {
	Section, SectionTitle, SectionSub string
	Nav                               []navItem
	Notice, Err                       string // action result inside the section (HTMX responses)
	FieldKey, FieldErr                string // validation error next to one field
	PollUpdates                       bool

	// General
	HubName, Host, Listen, PlayerAddr string
	Appearance                        string
	AllowUploads                      bool
	// Security
	TLS                     bool
	Fingerprint, CertSource string
	CertNotAfter            string
	CertExpiresSoon         bool
	CertSelfGenerated       bool
	CanRenewCert            bool
	MinPassword             int
	Updates                 updatesBody
	Net                     netBody
}

type navItem struct {
	ID, Title, Status string
	Level             string // ok, warn, muted
	Selected          bool
}

type updatesBody struct {
	hub.UpdateStatus
	LastCheckText string
	LastResultAt  string
	CadenceText   string // "checks every hour" ... or ""
	// OffOption: the channel "Off" is offered (development builds).
	OffOption bool
	Channel   string // select value: stable, beta or "" (development default)
	Err       bool   // the status could not be read
}

type turnBody struct {
	On                    bool
	PublicHost            string
	RelayIP, ResolvedAt   string
	Fixed                 bool
	Port, RelayMin        int
	RelayMax, Allocations int
	Forwards              []string
}

// netField is one network setting as shown: its value, where it comes from and a validation error.
type netField struct {
	Key      string
	Value    string // shown in the input
	Min, Max string // relay range inputs
	Web      bool   // saved on the web interface (otherwise it comes from hub.env/flags)
	EnvText  string // the hub.env/flag value, shown on the reset button
	CSRF     string // for the reset form
	Err      string
}

type netBody struct {
	Listen, PublicHost, TURNPort, Relay, RelayIP netField
	KeepRecent, KeepDaily, KeepWeekly, ICE       netField
	TURNField                                    netField
	TURNOn                                       bool
	ICEList                                      []string
	ICEAdd                                       string
	Pending                                      []string // labels of settings that need a restart
	Issues                                       []string
	ReachOK                                      bool
	ReachTitle, ReachText                        string
	TURN                                         turnBody
	Restarting                                   *restartBody
	CanRestart                                   bool
}

type restartBody struct {
	URL     string // address of the hub after the restart
	Changed bool   // the port changes: the page cannot reconnect by itself
}

// settingsRes is the outcome of a settings action handed to the renderer.
type settingsRes struct {
	OK, Err        string
	FieldKey, Msg  string
	Form           url.Values // attempted values (so a rejected input is not lost)
	PollUpdates    bool
	Restarting     *restartBody
	NoticeOverride string
}

// ---- rendering ----

// settingsPane reports an htmx request that swaps only the settings content area (#settings-area); everything
// else (direct load, sidebar navigation into #main, history restore) gets the full page.
func settingsPane(r *http.Request) bool {
	return r.Header.Get("HX-Request") == "true" && r.Header.Get("HX-Target") == "settings-area" &&
		r.Header.Get("HX-History-Restore-Request") != "true"
}

func sectionIndex(id string) int {
	for i, s := range settingsSections {
		if s.ID == id {
			return i
		}
	}
	return -1
}

// netDesired returns the configuration as it is meant to be: hub.env/flags plus the saved values.
func (s *Server) netDesired(ctx context.Context) (config.Config, map[string]string, error) {
	stored, err := s.svc.NetOverrides(ctx)
	if err != nil {
		return config.Config{}, nil, err
	}
	c := s.cfg.Net.Base
	c.ICEServers = append([]string(nil), s.cfg.Net.Base.ICEServers...)
	c.ApplyNet(stored)
	return c, stored, nil
}

func upper1(s string) string {
	if s == "" {
		return s
	}
	return strings.ToUpper(s[:1]) + s[1:]
}

func (s *Server) renderSettings(w http.ResponseWriter, r *http.Request, sess *session, status int, section string, res settingsRes) {
	if section == "" {
		section = "updates"
	}
	hx := settingsPane(r)
	sec := settingsSections[sectionIndex(section)]
	title := "Settings"
	d := s.base(r, sess, "settings", title)
	ctx := r.Context()
	b := settingsBody{Section: sec.ID, SectionTitle: sec.Title, SectionSub: sec.Subtitle,
		FieldKey: res.FieldKey, FieldErr: res.Msg, PollUpdates: res.PollUpdates}

	// General / Security basics.
	b.HubName = s.svc.Info().Name
	b.Host, b.Listen = r.Host, s.cfg.Listen
	fp, notAfter := s.certInfo()
	b.TLS, b.Fingerprint, b.CertSource, b.MinPassword = s.cfg.UseTLS, fp, s.cfg.CertSource, hub.MinPasswordLen
	if !notAfter.IsZero() {
		b.CertNotAfter = notAfter.Local().Format("2006-01-02")
		b.CertExpiresSoon = tlsutil.ExpiresSoon(notAfter, time.Now())
	}
	b.CertSelfGenerated = s.cfg.CertSource == "Self-generated"
	b.CanRenewCert = b.CertSelfGenerated && s.cfg.RenewCert != nil
	b.Appearance, _ = s.svc.Appearance(ctx)
	b.AllowUploads, _ = s.svc.AllowUserUploads(ctx)

	// Updates.
	updStatus, updErr := s.svc.UpdateStatus(ctx)
	if updErr == nil {
		b.Updates = updatesBody{UpdateStatus: updStatus, LastCheckText: "never",
			OffOption: updStatus.Settings.CompiledChannel != "stable" && updStatus.Settings.CompiledChannel != "beta"}
		if updStatus.Settings.ChannelIsSet || !b.Updates.OffOption {
			b.Updates.Channel = updStatus.Settings.Channel
		}
		switch updStatus.Settings.Channel {
		case hub.UpdateChannelBeta:
			b.Updates.CadenceText = "checks every hour"
		case "stable":
			b.Updates.CadenceText = "checks every 24 hours"
		}
		if updStatus.LastCheck != nil {
			b.Updates.LastCheckText = updStatus.LastCheck.Local().Format("2006-01-02 15:04")
		}
		if updStatus.LastResult != nil {
			b.Updates.LastResultAt = updStatus.LastResult.Time.Local().Format("2006-01-02 15:04")
		}
	} else {
		b.Updates.Err = true
	}

	// Network.
	nb, nerr := s.netBody(r, sess, res)
	if nerr != nil {
		s.fail(w, r, nerr)
		return
	}
	b.Net = nb
	b.PlayerAddr = s.playerAddress(r, nb)

	b.Nav = s.settingsNav(section, updStatus, updErr, nb)

	notice := res.NoticeOverride
	if notice == "" && res.OK != "" && !(hx && settingsQuiet[res.OK]) {
		notice = settingsFlash[res.OK]
		if notice == "" {
			notice = flashTexts[res.OK]
		}
	}
	errText := res.Err // field errors are shown next to the field
	if hx {
		b.Notice, b.Err = notice, errText
		d.Body = b
		s.render(w, status, "settings", "settings-pane-hx", d)
		return
	}
	if notice != "" {
		d.Flash = notice
	}
	if errText != "" {
		d.Error = errText
	}
	if res.Msg != "" && res.FieldKey == "" {
		d.Error = res.Msg
	}
	d.Body = b
	s.render(w, status, "settings", "layout", d)
}

// playerAddress is the address Players use: the public host if set (with the listen port), otherwise the host
// the admin used to open this page.
func (s *Server) playerAddress(r *http.Request, nb netBody) string {
	port := s.cfg.Net.Running.ListenPort()
	if s.cfg.Net.Running.Listen == "" {
		return r.Host
	}
	if h := strings.TrimSpace(nb.PublicHost.Value); h != "" {
		return net.JoinHostPort(h, strconv.Itoa(port))
	}
	return r.Host
}

func (s *Server) settingsNav(current string, st hub.UpdateStatus, updErr error, nb netBody) []navItem {
	items := make([]navItem, 0, len(settingsSections))
	for _, sec := range settingsSections {
		it := navItem{ID: sec.ID, Title: sec.Title, Selected: sec.ID == current, Level: "muted"}
		switch sec.ID {
		case "updates":
			switch {
			case updErr != nil:
				it.Status = "Status unavailable"
			case st.Available != nil:
				it.Status, it.Level = "Update available · "+st.Available.Version, "warn"
			case st.Disabled != "":
				it.Status = "Updates off"
			default:
				it.Status, it.Level = "Up to date · "+st.Settings.Channel, "ok"
			}
		case "general":
			it.Status = "Name, address, theme, uploads"
		case "network":
			it.Status = nb.ReachTitle
			it.Level = "warn"
			if nb.ReachOK {
				it.Level = "ok"
			}
			if len(nb.Pending) > 0 {
				it.Status, it.Level = "Restart required", "warn"
			}
		case "security":
			if s.cfg.UseTLS {
				it.Status, it.Level = "HTTPS active · admin", "ok"
			} else {
				it.Status, it.Level = "HTTP (dev mode) · admin", "warn"
			}
		}
		items = append(items, it)
	}
	return items
}

// netBody assembles the Network section from the effective configuration and the saved values.
func (s *Server) netBody(r *http.Request, sess *session, res settingsRes) (netBody, error) {
	var nb netBody
	desired, stored, err := s.netDesired(r.Context())
	if err != nil {
		return nb, err
	}
	base := s.cfg.Net.Base
	field := func(key string) netField {
		_, web := stored[key]
		f := netField{Key: key, Value: desired.NetValue(key), Web: web, EnvText: base.NetValue(key), CSRF: sess.CSRFToken}
		if res.FieldKey == key && res.Form != nil && key != config.NetICEServers && key != config.NetRelayPorts {
			f.Value = res.Form.Get("value")
		}
		if res.FieldKey == key {
			f.Err = res.Msg
		}
		return f
	}
	nb.Listen, nb.PublicHost, nb.TURNPort = field(config.NetListenPort), field(config.NetPublicHost), field(config.NetTURNPort)
	nb.RelayIP, nb.KeepRecent, nb.KeepDaily, nb.KeepWeekly = field(config.NetRelayIP), field(config.NetKeepRecent), field(config.NetKeepDaily), field(config.NetKeepWeekly)
	nb.Relay = field(config.NetRelayPorts)
	nb.Relay.Min, nb.Relay.Max, _ = strings.Cut(nb.Relay.Value, "-")
	if res.FieldKey == config.NetRelayPorts && res.Form != nil {
		nb.Relay.Min, nb.Relay.Max = res.Form.Get("min"), res.Form.Get("max")
	}
	nb.ICE = field(config.NetICEServers)
	nb.ICE.EnvText = "none"
	if len(base.ICEServers) > 0 {
		nb.ICE.EnvText = strings.Join(base.ICEServers, ", ")
	}
	nb.ICEList = desired.ICEServers
	if res.FieldKey == config.NetICEServers {
		nb.ICEAdd = res.Form.Get("url")
	}
	nb.TURNField = field(config.NetTURN)
	nb.TURNField.EnvText = "off"
	if base.TURN {
		nb.TURNField.EnvText = "on"
	}
	nb.TURNOn = desired.TURN
	nb.CanRestart = s.cfg.Net.RequestRestart != nil

	// Settings saved but not applied yet, except those that failed at startup (shown as an error card).
	run := s.cfg.Net.Running
	issues := map[string]NetIssue{}
	for _, is := range s.cfg.Net.Issues {
		if NetSignature(&desired, is.Key) == is.Value {
			issues[is.Key] = is
			nb.Issues = append(nb.Issues, is.Message)
		}
	}
	_, turnBroken := issues[config.NetTURN]
	for _, k := range config.NetKeys {
		if !config.NetNeedsRestart(k) {
			continue
		}
		if _, bad := issues[k]; bad || (turnBroken && turnKeys[k]) {
			continue
		}
		if desired.NetValue(k) != run.NetValue(k) {
			nb.Pending = append(nb.Pending, config.NetLabel(k))
		}
	}

	// Reachability from configuration only: nothing probes the hub from outside.
	host := strings.TrimSpace(run.PublicHost)
	turnOn := s.cfg.TURN != nil
	switch {
	case turnOn && host != "":
		nb.ReachOK, nb.ReachTitle = true, "Prepared for the internet"
		nb.ReachText = fmt.Sprintf("%s is set as the public address and the built-in relay is on. Remote Players can connect once the ports listed below are forwarded to this hub. The hub cannot test this from outside.", host)
	default:
		nb.ReachTitle = "Only in local network"
		var missing []string
		if host == "" {
			missing = append(missing, "a public host")
		}
		if !turnOn {
			missing = append(missing, "the built-in TURN relay")
		}
		nb.ReachText = "Players on your network connect normally. For Players outside it the hub still needs " + strings.Join(missing, " and ") + "."
	}
	if s.cfg.TURN != nil {
		st := s.cfg.TURN.Status()
		nb.TURN = turnBody{On: true, PublicHost: st.PublicHost, RelayIP: st.RelayIP, Fixed: st.Fixed, Port: st.Port,
			RelayMin: st.RelayMin, RelayMax: st.RelayMax, Allocations: st.Allocations, Forwards: turnForwards(run.Listen, st)}
		if !st.ResolvedAt.IsZero() {
			nb.TURN.ResolvedAt = st.ResolvedAt.Local().Format("2006-01-02 15:04")
		}
	}
	nb.Restarting = res.Restarting
	return nb, nil
}

// ---- handlers: pages ----

func (s *Server) settingsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	section := r.PathValue("section")
	if section == "" {
		section = "updates"
	}
	if sectionIndex(section) < 0 {
		http.NotFound(w, r)
		return
	}
	s.renderSettings(w, r, sess, http.StatusOK, section, settingsRes{OK: r.URL.Query().Get("ok"), Err: errTexts[r.URL.Query().Get("err")]})
}

// settingsDone ends a settings action. Success: HTMX requests get the re-rendered section, others a redirect to
// the section (the flash text comes from ok). Failure: the section with the message next to the field and the
// attempted values (status 400 for plain requests, 200 for HTMX so that the fragment is swapped in).
func (s *Server) settingsDone(w http.ResponseWriter, r *http.Request, sess *session, section string, res settingsRes, failed bool) {
	hx := settingsPane(r)
	if !failed && !hx {
		to := "/settings/" + section
		if res.OK != "" {
			to += "?ok=" + url.QueryEscape(res.OK)
		}
		http.Redirect(w, r, to, http.StatusSeeOther)
		return
	}
	status := http.StatusOK
	if failed && !hx {
		status = http.StatusBadRequest
	}
	s.renderSettings(w, r, sess, status, section, res)
}

// ---- handlers: General ----

func (s *Server) settingsName(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetHubName(r.Context(), r.PostFormValue("name")); err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			s.settingsDone(w, r, sess, "general", settingsRes{FieldKey: "name", Msg: he.Message + ".", Form: r.PostForm}, true)
			return
		}
		s.fail(w, r, err)
		return
	}
	// The hub name is part of the sidebar: reload the page.
	s.redirect(w, r, "/settings/general?ok=name")
}

func (s *Server) settingsAppearance(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetAppearance(r.Context(), r.PostFormValue("mode")); err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			s.settingsDone(w, r, sess, "general", settingsRes{Err: he.Message + "."}, true)
			return
		}
		s.fail(w, r, err)
		return
	}
	// The theme applies to the whole page: reload it.
	s.redirect(w, r, "/settings/general?ok=appearance")
}

func (s *Server) settingsUploads(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetAllowUserUploads(r.Context(), r.PostFormValue("enabled") == "1"); err != nil {
		s.fail(w, r, err)
		return
	}
	s.settingsDone(w, r, sess, "general", settingsRes{OK: "uploads"}, false)
}

// ---- handlers: Security ----

func (s *Server) settingsPassword(w http.ResponseWriter, r *http.Request, sess *session) {
	fail := func(msg string) {
		s.settingsDone(w, r, sess, "security", settingsRes{FieldKey: "password", Msg: msg}, true)
	}
	if r.PostFormValue("new") != r.PostFormValue("new2") {
		fail("The new passwords do not match.")
		return
	}
	// A stolen session must not be able to guess the current password: same attempt budget as the login.
	ip, who := remoteIP(r), "pw:"+sess.User.ID
	if !s.login.take(ip, who) {
		w.Header().Set("Retry-After", "60")
		fail("Too many attempts. Please try again in a minute.")
		return
	}
	release, ok := s.acquireVerify(r.Context())
	if !ok {
		s.login.refund(ip, who)
		busy(w)
		return
	}
	defer release()
	if _, err := s.svc.VerifyPassword(r.Context(), sess.User.Username, r.PostFormValue("current")); err != nil {
		if errors.Is(err, hub.ErrInvalidCredentials) {
			fail("The current password is incorrect.")
			return
		}
		s.login.refund(ip, who)
		s.fail(w, r, err)
		return
	}
	s.login.refund(ip, who)
	if err := s.svc.ChangePassword(r.Context(), sess.User.ID, r.PostFormValue("new")); err != nil {
		var he *hub.Error
		if errors.As(err, &he) && he.Code == hub.CodeBadRequest {
			fail(he.Message + ".")
			return
		}
		s.fail(w, r, err)
		return
	}
	// ChangePassword ends all web sessions; start a new session for the current admin.
	if err := s.startSession(w, r, sess.User.ID); err != nil {
		s.fail(w, r, err)
		return
	}
	s.redirect(w, r, "/settings/security?ok=password")
}

// certInfo returns the current certificate fingerprint and expiry (live when the Hub can renew at runtime).
func (s *Server) certInfo() (string, time.Time) {
	if s.cfg.CertState != nil {
		return s.cfg.CertState()
	}
	return s.cfg.CertFingerprint, s.cfg.CertNotAfter
}

// settingsRenewCert replaces the self-generated certificate; new TLS handshakes use it immediately.
func (s *Server) settingsRenewCert(w http.ResponseWriter, r *http.Request, sess *session) {
	if s.cfg.RenewCert == nil || s.cfg.CertSource != "Self-generated" {
		s.redirect(w, r, "/settings/security?err=renewunsupported")
		return
	}
	fp, _, err := s.cfg.RenewCert()
	if err != nil {
		s.log.Error("renew tls certificate", "err", err)
		s.redirect(w, r, "/settings/security?err=renewfailed")
		return
	}
	s.log.Warn("TLS certificate renewed from Settings; Players must confirm the new fingerprint", "new_sha256_fingerprint", fp, "admin", sess.User.Username)
	s.redirect(w, r, "/settings/security?ok=certrenewed")
}

// ---- handlers: Updates ----

func (s *Server) settingsUpdates(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.SetUpdateSettings(r.Context(), r.PostFormValue("channel"), r.PostFormValue("auto") == "1"); err != nil {
		var he *hub.Error
		if errors.As(err, &he) {
			s.settingsDone(w, r, sess, "updates", settingsRes{Err: he.Message + "."}, true)
			return
		}
		s.fail(w, r, err)
		return
	}
	s.settingsDone(w, r, sess, "updates", settingsRes{OK: "updates"}, false)
}

// settingsUpdatesCheck starts a check in the background ("Check now").
func (s *Server) settingsUpdatesCheck(w http.ResponseWriter, r *http.Request, sess *session) {
	s.svc.TriggerUpdateCheck()
	s.settingsDone(w, r, sess, "updates", settingsRes{OK: "updatecheck", PollUpdates: true}, false)
}

// settingsUpdatesInstall stages the update and asks the root helper to install it. The browser asks for
// confirmation first (hx-confirm); an update that breaks recent Players is only installed with confirm_breaking=1.
func (s *Server) settingsUpdatesInstall(w http.ResponseWriter, r *http.Request, sess *session) {
	_, err := s.svc.InstallUpdate(r.Context(), r.PostFormValue("confirm_breaking") == "1")
	switch {
	case err == nil:
		s.redirect(w, r, "/settings/updates?ok=updateinstall")
	case errors.Is(err, hub.ErrNotPackaged):
		s.redirect(w, r, "/settings/updates?err=updatepackage")
	case errors.Is(err, hub.ErrNoUpdate), errors.Is(err, hub.ErrUpdatesOff):
		s.redirect(w, r, "/settings/updates?err=updatenone")
	case errors.Is(err, hub.ErrUpdateBreaking):
		s.redirect(w, r, "/settings/updates?err=updatebreaking")
	default:
		s.log.Error("install update", "err", err)
		s.redirect(w, r, "/settings/updates?err=updatefailed")
	}
}

// ---- handlers: Network ----

// netRaw turns the posted form of one setting into the raw value to store. cur is the candidate configuration.
func netRaw(key string, form url.Values, cur *config.Config) (string, error) {
	val := strings.TrimSpace(form.Get("value"))
	switch key {
	case config.NetTURN:
		if form.Get("value") == "1" {
			return "true", nil
		}
		return "false", nil
	case config.NetRelayPorts:
		min, max := strings.TrimSpace(form.Get("min")), strings.TrimSpace(form.Get("max"))
		if _, err := strconv.Atoi(min); err != nil {
			return "", &config.NetError{Msg: "Enter the first relay port as a number"}
		}
		if _, err := strconv.Atoi(max); err != nil {
			return "", &config.NetError{Msg: "Enter the last relay port as a number"}
		}
		return min + "-" + max, nil
	case config.NetICEServers:
		u := strings.TrimSpace(form.Get("url"))
		list := append([]string(nil), cur.ICEServers...)
		switch form.Get("op") {
		case "remove":
			out := list[:0]
			for _, x := range list {
				if x != u {
					out = append(out, x)
				}
			}
			list = out
		default:
			if err := config.ValidateSTUNURL(u); err != nil {
				return "", err
			}
			for _, x := range list {
				if x == u {
					return "", &config.NetError{Msg: "This server is already in the list"}
				}
			}
			list = append(list, u)
		}
		return config.EncodeICEServers(list), nil
	case config.NetListenPort, config.NetTURNPort, config.NetKeepRecent, config.NetKeepDaily, config.NetKeepWeekly:
		if _, err := strconv.Atoi(val); err != nil {
			return "", &config.NetError{Msg: "Enter a whole number"}
		}
	}
	return val, nil
}

func (s *Server) testBind(addr string) error {
	if s.cfg.Net.TestBind != nil {
		return s.cfg.Net.TestBind(addr)
	}
	return config.TestBind(addr)
}

// applyLiveNet applies the settings that need no restart.
func (s *Server) applyLiveNet(ctx context.Context) error {
	desired, _, err := s.netDesired(ctx)
	if err != nil {
		return err
	}
	s.svc.SetSaveRetention(desired.SaveKeepRecent, desired.SaveKeepDaily, desired.SaveKeepWeekly)
	s.svc.SetICEServers(desired.ICEServers)
	return nil
}

func validNetKey(key string) bool {
	for _, k := range config.NetKeys {
		if k == key {
			return true
		}
	}
	return false
}

// settingsNetSave validates and stores one network setting (autosave), then applies it live if it can be.
func (s *Server) settingsNetSave(w http.ResponseWriter, r *http.Request, sess *session) {
	key := r.PathValue("key")
	if !validNetKey(key) {
		http.NotFound(w, r)
		return
	}
	fail := func(msg string) {
		s.settingsDone(w, r, sess, "network", settingsRes{FieldKey: key, Msg: upper1(msg), Form: r.PostForm}, true)
	}
	cand, _, err := s.netDesired(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	raw, err := netRaw(key, r.PostForm, &cand)
	if err != nil {
		fail(err.Error())
		return
	}
	if err := cand.SetNet(key, raw); err != nil {
		fail(err.Error())
		return
	}
	if err := cand.ValidateNet(key); err != nil {
		fail(err.Error())
		return
	}
	if key == config.NetListenPort && cand.ListenPort() != s.cfg.Net.Running.ListenPort() {
		addr := config.ListenWithPort(s.cfg.Net.Running.Listen, cand.ListenPort())
		if err := s.testBind(addr); err != nil {
			s.log.Info("listen port test bind failed", "addr", addr, "err", err)
			fail(fmt.Sprintf("Port %d is in use", cand.ListenPort()))
			return
		}
	}
	if err := s.svc.SetNetOverride(r.Context(), key, cand.NetValue(key)); err != nil {
		s.fail(w, r, err)
		return
	}
	if err := s.applyLiveNet(r.Context()); err != nil {
		s.fail(w, r, err)
		return
	}
	s.settingsDone(w, r, sess, "network", settingsRes{OK: "netsaved"}, false)
}

// settingsNetReset deletes the saved value: hub.env/flags apply again.
func (s *Server) settingsNetReset(w http.ResponseWriter, r *http.Request, sess *session) {
	key := r.PathValue("key")
	if !validNetKey(key) {
		http.NotFound(w, r)
		return
	}
	_, stored, err := s.netDesired(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	// Validate the configuration that results from the reset, like a save would.
	rest := make(map[string]string, len(stored))
	for k, v := range stored {
		if k != key {
			rest[k] = v
		}
	}
	cand := s.cfg.Net.Base
	cand.ICEServers = append([]string(nil), s.cfg.Net.Base.ICEServers...)
	cand.ApplyNet(rest)
	if err := cand.ValidateNet(key); err != nil {
		s.settingsDone(w, r, sess, "network", settingsRes{FieldKey: key, Msg: upper1(err.Error()), Form: r.PostForm}, true)
		return
	}
	if key == config.NetListenPort && cand.ListenPort() != s.cfg.Net.Running.ListenPort() {
		addr := config.ListenWithPort(s.cfg.Net.Running.Listen, cand.ListenPort())
		if err := s.testBind(addr); err != nil {
			s.log.Info("listen port test bind failed", "addr", addr, "err", err)
			s.settingsDone(w, r, sess, "network", settingsRes{FieldKey: key, Msg: fmt.Sprintf("Port %d is in use", cand.ListenPort()), Form: r.PostForm}, true)
			return
		}
	}
	if err := s.svc.ResetNetOverride(r.Context(), key); err != nil {
		s.fail(w, r, err)
		return
	}
	if err := s.applyLiveNet(r.Context()); err != nil {
		s.fail(w, r, err)
		return
	}
	s.settingsDone(w, r, sess, "network", settingsRes{OK: "netreset"}, false)
}

// settingsNetRestart asks the process to restart (graceful shutdown, then re-exec). The response is rendered
// first; the shutdown waits for it to be written.
func (s *Server) settingsNetRestart(w http.ResponseWriter, r *http.Request, sess *session) {
	if s.cfg.Net.RequestRestart == nil {
		s.settingsDone(w, r, sess, "network", settingsRes{Err: "This hub cannot restart itself. Restart the service manually."}, true)
		return
	}
	desired, _, err := s.netDesired(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	scheme := "https"
	if !s.cfg.UseTLS {
		scheme = "http"
	}
	// The address comes from the configuration (public host, else a concrete listen host), never from the
	// request's Host header. Without one the URL stays empty and the page asks the admin to open the new port.
	host := s.cfg.PublicHost
	if host == "" {
		if h, _, err := net.SplitHostPort(s.cfg.Listen); err == nil && h != "" {
			if ip := net.ParseIP(h); ip == nil || !ip.IsUnspecified() {
				host = h
			}
		}
	}
	port := desired.ListenPort()
	rb := &restartBody{Changed: port != s.cfg.Net.Running.ListenPort()}
	if host != "" {
		rb.URL = scheme + "://" + net.JoinHostPort(host, strconv.Itoa(port)) + "/settings/network"
	}
	s.log.Info("hub restart requested from the web interface", "user", sess.User.Username)
	s.renderSettings(w, r, sess, http.StatusOK, "network", settingsRes{OK: "restarting", Restarting: rb})
	s.cfg.Net.RequestRestart()
}

// turnForwards lists the router port forwards a public Hub needs (ADR 0012 D1).
func turnForwards(listen string, st turnsrv.Status) []string {
	hubPort := "8443"
	if _, p, err := net.SplitHostPort(listen); err == nil && p != "" {
		hubPort = p
	}
	return []string{
		"TCP " + hubPort + " (Hub, HTTPS/WSS)",
		fmt.Sprintf("UDP and TCP %d (STUN/TURN)", st.Port),
		fmt.Sprintf("UDP %d-%d (relay)", st.RelayMin, st.RelayMax),
	}
}
