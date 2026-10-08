// Package web is the web interface of the FrameBeam Hub (html/template + htmx via embed, no Node build).
// It uses only the service layer internal/hub.
package web

import (
	"bytes"
	"context"
	"embed"
	"html/template"
	"io/fs"
	"log/slog"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/turnsrv"
)

//go:embed templates/*.html
var templatesFS embed.FS

//go:embed static/*
var staticFS embed.FS

// DefaultMaxUploadBytes is the upload limit for ROMs (4 GiB).
const DefaultMaxUploadBytes = hub.MaxROMBytes

const (
	sessionCookie = "fb_session"
	csrfCookie    = "fb_csrf" // double-submit cookie for forms before sign-in (setup, login)
	maxFormBytes  = 1 << 20
	// multipartSlack is the headroom for multipart framing and fields above the file limit.
	multipartSlack = 16 << 10
)

// Config describes operational data the service does not know.
type Config struct {
	// Listen is the configured listen address (shown in Settings).
	Listen string
	// UseTLS: HTTPS is active (cookie "Secure", transport display).
	UseTLS bool
	// CertFingerprint (SHA-256), CertSource ("Self-generated"/"Own cert/key"), CertNotAfter for Settings.
	CertFingerprint string
	CertSource      string
	CertNotAfter    time.Time
	// MaxUploadBytes: limit for ROM uploads (0 = DefaultMaxUploadBytes).
	MaxUploadBytes int64
	// TURN is the embedded TURN server (nil: off), shown on the Settings page.
	TURN *turnsrv.Server
	// Net describes the network settings and the restart hook (Settings > Network).
	Net NetConfig
}

// Server is the web interface.
type Server struct {
	svc   *hub.Service
	cfg   Config
	log   *slog.Logger
	tmpl  map[string]*template.Template
	login *limiter
	done  chan struct{} // closed by Shutdown: long-lived streams (SSE) end
	once  sync.Once
}

// New creates the web interface.
func New(svc *hub.Service, cfg Config, log *slog.Logger) (*Server, error) {
	if log == nil {
		log = slog.Default()
	}
	if cfg.MaxUploadBytes <= 0 {
		cfg.MaxUploadBytes = DefaultMaxUploadBytes
	}
	s := &Server{svc: svc, cfg: cfg, log: log, tmpl: map[string]*template.Template{}, done: make(chan struct{}),
		login: &limiter{max: 5, window: time.Minute, now: svc.Now, hits: map[string][]time.Time{}}}
	for _, p := range []string{"login", "setup", "library", "saves", "clients", "settings", "users", "systems", "confirm"} {
		t, err := template.New(p).ParseFS(templatesFS, "templates/layout.html", "templates/"+p+".html")
		if err != nil {
			return nil, err
		}
		s.tmpl[p] = t
	}
	return s, nil
}

// Shutdown ends long-lived responses (the /events streams) so a graceful http.Server.Shutdown does not
// wait for them. Idempotent; wire it with http.Server.RegisterOnShutdown.
func (s *Server) Shutdown() { s.once.Do(func() { close(s.done) }) }

// Register attaches the web routes to mux. "/" is the catch-all (redirects to /setup without an admin, otherwise 404).
func (s *Server) Register(mux *http.ServeMux) {
	sub, _ := fs.Sub(staticFS, "static")
	static := http.StripPrefix("/static/", http.FileServerFS(sub))
	mux.Handle("GET /static/", secure(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "public, max-age=3600")
		static.ServeHTTP(w, r)
	})))
	h := func(pattern string, f http.HandlerFunc) { mux.Handle(pattern, secure(f)) }
	h("/", s.catchAll)
	h("GET /{$}", func(w http.ResponseWriter, r *http.Request) {
		if has, err := s.svc.HasAdmin(r.Context()); err == nil && !has {
			http.Redirect(w, r, "/setup", http.StatusSeeOther)
			return
		}
		http.Redirect(w, r, "/library", http.StatusSeeOther)
	})
	h("GET /setup", s.setupGet)
	h("POST /setup", s.setupPost)
	h("GET /login", s.loginGet)
	h("POST /login", s.loginPost)
	h("POST /logout", s.guard(s.logout))
	h("GET /nav-fragment", s.guard(s.navFragment))
	mux.Handle("GET /events", secure(s.guard(s.events)))
	h("GET /library", s.guard(s.libraryGet))
	h("POST /library/upload", s.guard(s.libraryUpload))
	h("POST /library/{id}/delete", s.guard(s.libraryDelete))
	h("GET /saves", s.guard(s.savesGet))
	h("GET /saves/{user}/{game}/{slot}", s.guard(s.savesGet))
	h("GET /saves/{user}/{game}/{slot}/download", s.guard(s.saveDownload))
	h("GET /saves/{user}/{game}/{slot}/history/{version}/download", s.guard(s.saveHistoryDownload))
	h("POST /saves/{user}/{game}/{slot}/conflicts/{id}/resolve", s.guard(s.saveResolve))
	h("POST /saves/{user}/{game}/{slot}/history/{version}/restore", s.guard(s.saveRestore))
	h("POST /saves/{user}/{game}/{slot}/snapshots", s.guard(s.saveSnapshot))
	h("GET /clients", s.guard(s.clientsGet))
	h("POST /clients/requests/{id}/allow", s.guard(s.clientAllow))
	h("POST /clients/requests/{id}/deny", s.guard(s.clientDeny))
	h("POST /clients/devices/{id}/revoke", s.guard(s.clientRevoke))
	h("GET /clients/devices/{id}/delete", s.guard(s.clientDeleteConfirm))
	h("POST /clients/devices/{id}/delete", s.guard(s.clientDelete))
	h("GET /users", s.guard(s.usersGet))
	h("POST /users/invites", s.guard(s.inviteCreate))
	h("POST /users/invites/{id}/revoke", s.guard(s.inviteRevoke))
	h("POST /users/{id}/disable", s.guard(s.userDisable))
	h("POST /users/{id}/enable", s.guard(s.userEnable))
	h("GET /users/{id}/delete", s.guard(s.userDeleteConfirm))
	h("POST /users/{id}/delete", s.guard(s.userDelete))
	h("GET /systems", s.guard(s.systemsGet))
	h("POST /systems/{id}/expected-version", s.guard(s.systemVersion))
	h("POST /systems/{id}/firmware-mode", s.guard(s.systemFirmwareMode))
	h("POST /systems/{id}/firmware/{file}/upload", s.guard(s.firmwareUpload))
	h("POST /systems/{id}/firmware/{file}/pin", s.guard(s.firmwarePin))
	h("POST /systems/{id}/firmware/{file}/remove", s.guard(s.firmwareRemove))
	h("POST /cores/sync", s.guard(s.coresSync))
	h("GET /settings", s.guard(s.settingsGet))
	h("GET /settings/{section}", s.guard(s.settingsGet))
	h("POST /settings/network/restart", s.guard(s.settingsNetRestart))
	h("POST /settings/network/{key}", s.guard(s.settingsNetSave))
	h("POST /settings/network/{key}/reset", s.guard(s.settingsNetReset))
	h("POST /settings/appearance", s.guard(s.settingsAppearance))
	h("POST /settings/uploads", s.guard(s.settingsUploads))
	h("POST /settings/updates", s.guard(s.settingsUpdates))
	h("POST /settings/updates/check", s.guard(s.settingsUpdatesCheck))
	h("POST /settings/updates/install", s.guard(s.settingsUpdatesInstall))
	h("POST /settings/name", s.guard(s.settingsName))
	h("POST /settings/password", s.guard(s.settingsPassword))
}

// secure sets security headers (no inline script/style) and prevents caching of pages.
func secure(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		h := w.Header()
		h.Set("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; "+
			"connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'")
		h.Set("X-Frame-Options", "DENY")
		h.Set("Referrer-Policy", "same-origin")
		h.Set("X-Content-Type-Options", "nosniff")
		if _, ok := h["Cache-Control"]; !ok && !strings.HasPrefix(r.URL.Path, "/static/") {
			h.Set("Cache-Control", "no-store")
		}
		next.ServeHTTP(w, r)
	})
}

func (s *Server) catchAll(w http.ResponseWriter, r *http.Request) {
	p := r.URL.Path
	if strings.HasPrefix(p, "/api/") || strings.HasPrefix(p, "/.well-known/") {
		http.NotFound(w, r)
		return
	}
	if has, err := s.svc.HasAdmin(r.Context()); err == nil && !has {
		http.Redirect(w, r, "/setup", http.StatusSeeOther)
		return
	}
	http.NotFound(w, r)
}

// ---- Page model ----

type pageData struct {
	Title, Nav          string
	CSRF                string
	User, Role          string
	HubName, HubVersion string
	Pending             int
	Conflicts           int    // open save conflicts (nav badge)
	Firmware            int    // required firmware files missing or mismatching (nav badge, mode native)
	UpdateAvailable     bool   // a newer Hub version is known (nav dot on Settings)
	Theme               string // light, dark or system (Hub setting)
	Flash, Error        string
	Fragment            bool
	MainSwap            bool // HX main swap: render only the main content plus out-of-band nav and title
	Body                any
}

var flashTexts = map[string]string{
	"uploaded":      "ROM added to the library.",
	"deleted":       "ROM deleted.",
	"name":          "Hub name saved.",
	"password":      "Password changed.",
	"resolved":      "Conflict resolved.",
	"restored":      "Version restored as the new current checkpoint. The previous checkpoint is in the history.",
	"snapshot":      "Snapshot created.",
	"disabled":      "User disabled. Their devices are signed out and their Sessions ended.",
	"enabled":       "User enabled.",
	"userdeleted":   "User deleted, together with their devices, invites, saves and save history.",
	"devdeleted":    "Device deleted.",
	"revoked":       "Invite revoked.",
	"appearance":    "Appearance saved.",
	"uploads":       "Upload setting saved.",
	"version":       "Expected core version saved.",
	"fwmode":        "Firmware mode saved.",
	"fwfile":        "Firmware file saved.",
	"fwremoved":     "Firmware file removed.",
	"fwpin":         "Expected SHA-256 saved.",
	"coresync":      "Checking the core source in the background. Reload the page in a moment.",
	"updates":       "Update settings saved. Checking for updates in the background.",
	"updatecheck":   "Checking for updates in the background. Reload the page in a moment.",
	"updateinstall": "Update downloaded and verified. The Hub installs it and restarts in a moment; reload this page afterwards.",
}

var errTexts = map[string]string{
	"stale":          "The slot changed in the meantime or the conflict was already resolved. Please review and decide again.",
	"label":          "The snapshot label must be at most 64 characters.",
	"admin":          "Admins cannot be disabled or deleted.",
	"nodevice":       "Device not found.",
	"nouser":         "User not found.",
	"noinvite":       "The invite is no longer active.",
	"nosystem":       "System or file not found.",
	"nofile":         "Firmware file not found.",
	"updatepackage":  "This Hub was not installed from the .deb package, so it cannot install updates itself. Use the manual command shown below.",
	"updatenone":     "There is no update to install.",
	"updatebreaking": "This update breaks Players seen in the last 30 days. Use \"Install anyway\" to confirm.",
	"updatefailed":   "The update could not be downloaded or verified. See the update status below.",
}

type session struct {
	hub.WebSession
}

func (s *Server) base(r *http.Request, sess *session, nav, title string) pageData {
	info := s.svc.Info()
	ver := info.HubVersion
	if ver != "" && ver[0] >= '0' && ver[0] <= '9' {
		ver = "v" + ver
	}
	d := pageData{Title: title, Nav: nav, HubName: info.Name, HubVersion: ver, Flash: flashTexts[r.URL.Query().Get("ok")], Error: errTexts[r.URL.Query().Get("err")]}
	d.MainSwap = isMainSwap(r)
	if sess != nil {
		d.CSRF = sess.CSRFToken
		d.User = sess.User.DisplayName
		d.Role = roleLabel(sess.User.Role)
		if p, err := s.svc.ListPendingRequests(r.Context()); err == nil {
			d.Pending = len(p)
		}
		if n, err := s.svc.OpenConflictCount(r.Context()); err == nil {
			d.Conflicts = n
		}
		if n, err := s.svc.FirmwareProblems(r.Context()); err == nil {
			d.Firmware = n
		}
		d.UpdateAvailable = s.svc.UpdateAvailableBadge(r.Context())
	}
	return d
}

// navFragment renders the sidebar nav (badges and active item); the active item comes from HX-Current-URL.
func (s *Server) navFragment(w http.ResponseWriter, r *http.Request, sess *session) {
	nav := ""
	if u, err := url.Parse(r.Header.Get("HX-Current-URL")); err == nil {
		for _, n := range []string{"library", "saves", "systems", "clients", "users", "settings"} {
			if u.Path == "/"+n || strings.HasPrefix(u.Path, "/"+n+"/") {
				nav = n
			}
		}
	}
	d := s.base(r, sess, nav, "")
	d.MainSwap = false
	s.render(w, http.StatusOK, "library", "nav-fragment", d)
}

func roleLabel(r hub.Role) string {
	if r == hub.RoleAdmin {
		return "Admin"
	}
	return "User"
}

// render renders template name ("layout", "bare" or a fragment) of page.
func (s *Server) render(w http.ResponseWriter, status int, page, name string, d pageData) {
	if d.Theme == "" {
		d.Theme, _ = s.svc.Appearance(context.Background())
	}
	if name == "layout" && d.MainSwap {
		name = "main-swap"
	}
	w.Header().Add("Vary", "HX-Request, HX-Target, HX-History-Restore-Request")
	var buf bytes.Buffer
	if err := s.tmpl[page].ExecuteTemplate(&buf, name, d); err != nil {
		s.log.Error("template", "page", page, "err", err)
		http.Error(w, "Internal error", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(status)
	w.Write(buf.Bytes())
}

// isMainSwap reports a sidebar navigation request: htmx GET targeting #main, not a history restore.
func isMainSwap(r *http.Request) bool {
	return r.Method == http.MethodGet && isHX(r, "main") && r.Header.Get("HX-History-Restore-Request") != "true"
}

func isHX(r *http.Request, target string) bool {
	return r.Header.Get("HX-Request") == "true" && r.Header.Get("HX-Target") == target
}

// ---- Guard: setup redirect, session, CSRF ----

type handlerFunc func(w http.ResponseWriter, r *http.Request, sess *session)

type ctxKey int

const keyCSRFChecked ctxKey = 1

func (s *Server) guard(f handlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if has, err := s.svc.HasAdmin(r.Context()); err != nil {
			s.fail(w, r, err)
			return
		} else if !has {
			s.redirect(w, r, "/setup")
			return
		}
		c, err := r.Cookie(sessionCookie)
		if err != nil || c.Value == "" {
			s.redirect(w, r, "/login")
			return
		}
		ws, err := s.svc.LookupWebSession(r.Context(), c.Value)
		if err != nil || ws.User.Role != hub.RoleAdmin { // the web interface is admin-only
			s.clearSession(w)
			s.redirect(w, r, "/login")
			return
		}
		sess := &session{ws}
		if r.Method == http.MethodPost {
			if strings.HasPrefix(r.Header.Get("Content-Type"), "multipart/form-data") {
				// Streaming upload: the handler checks _csrf (before the file) or the header.
				if tok := r.Header.Get("X-CSRF-Token"); tok != "" {
					if !hub.TokenEqual(tok, ws.CSRFToken) {
						http.Error(w, "CSRF check failed", http.StatusForbidden)
						return
					}
					r = r.WithContext(context.WithValue(r.Context(), keyCSRFChecked, true))
				}
			} else {
				r.Body = http.MaxBytesReader(w, r.Body, maxFormBytes)
				tok := r.Header.Get("X-CSRF-Token")
				if err := r.ParseForm(); err != nil {
					http.Error(w, "Invalid request", http.StatusBadRequest)
					return
				}
				if tok == "" {
					tok = r.PostFormValue("_csrf")
				}
				if !hub.TokenEqual(tok, ws.CSRFToken) {
					http.Error(w, "CSRF check failed", http.StatusForbidden)
					return
				}
			}
		}
		f(w, r, sess)
	}
}

func (s *Server) redirect(w http.ResponseWriter, r *http.Request, to string) {
	if r.Header.Get("HX-Request") == "true" {
		w.Header().Set("HX-Redirect", to)
		w.WriteHeader(http.StatusOK)
		return
	}
	http.Redirect(w, r, to, http.StatusSeeOther)
}

func (s *Server) fail(w http.ResponseWriter, r *http.Request, err error) {
	s.log.Error("error", "method", r.Method, "path", r.URL.Path, "err", err)
	http.Error(w, "Internal error", http.StatusInternalServerError)
}

// ---- Cookies, CSRF before sign-in, login rate limiting ----

func (s *Server) setCookie(w http.ResponseWriter, name, value string, maxAge int) {
	http.SetCookie(w, &http.Cookie{Name: name, Value: value, Path: "/", MaxAge: maxAge, HttpOnly: true,
		SameSite: http.SameSiteStrictMode, Secure: s.cfg.UseTLS})
}

func (s *Server) clearSession(w http.ResponseWriter) { s.setCookie(w, sessionCookie, "", -1) }

func (s *Server) startSession(w http.ResponseWriter, r *http.Request, userID string) error {
	tok, _, err := s.svc.CreateWebSession(r.Context(), userID)
	if err != nil {
		return err
	}
	s.setCookie(w, sessionCookie, tok, int(hub.WebSessionTTL.Seconds()))
	return nil
}

// preCSRF returns the double-submit token for forms without a session and sets the cookie if needed.
func (s *Server) preCSRF(w http.ResponseWriter, r *http.Request) string {
	if c, err := r.Cookie(csrfCookie); err == nil && len(c.Value) >= 20 {
		return c.Value
	}
	tok, err := hub.RandomToken()
	if err != nil {
		return ""
	}
	s.setCookie(w, csrfCookie, tok, int((2 * time.Hour).Seconds()))
	return tok
}

func (s *Server) checkPreCSRF(r *http.Request) bool {
	c, err := r.Cookie(csrfCookie)
	if err != nil || c.Value == "" {
		return false
	}
	return hub.TokenEqual(r.PostFormValue("_csrf"), c.Value)
}

type limiter struct {
	mu     sync.Mutex
	max    int
	window time.Duration
	now    func() time.Time
	hits   map[string][]time.Time
}

func (l *limiter) prune(key string) []time.Time {
	cut := l.now().Add(-l.window)
	h := l.hits[key]
	i := 0
	for i < len(h) && !h[i].After(cut) {
		i++
	}
	h = h[i:]
	if len(h) == 0 {
		delete(l.hits, key)
	} else {
		l.hits[key] = h
	}
	return h
}

// blocked reports whether key has reached the limit.
func (l *limiter) blocked(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	return len(l.prune(key)) >= l.max
}

func (l *limiter) fail(key string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.prune(key)
	l.hits[key] = append(l.hits[key], l.now())
}

func remoteIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func isLoopback(r *http.Request) bool {
	ip := net.ParseIP(remoteIP(r))
	return ip != nil && ip.IsLoopback()
}
