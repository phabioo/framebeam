// Package web ist das Webinterface des FrameBeam Hub (html/template + htmx, per embed, kein Node-Build).
// Es nutzt ausschließlich die Service-Schicht internal/hub.
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
	"strings"
	"sync"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
)

//go:embed templates/*.html
var templatesFS embed.FS

//go:embed static/*
var staticFS embed.FS

// DefaultMaxUploadBytes ist das Upload-Limit für ROMs (4 GiB).
const DefaultMaxUploadBytes int64 = 4 << 30

const (
	sessionCookie = "fb_session"
	csrfCookie    = "fb_csrf" // Double-Submit-Cookie für Formulare vor der Anmeldung (Setup, Login)
	maxFormBytes  = 1 << 20
	// multipartSlack ist der Spielraum für Multipart-Rahmen und Felder über dem Datei-Limit.
	multipartSlack = 16 << 10
)

// Config beschreibt Betriebsdaten, die der Service nicht kennt.
type Config struct {
	// Listen ist die konfigurierte Listen-Adresse (Anzeige in Settings).
	Listen string
	// UseTLS: HTTPS aktiv (Cookie "Secure", Transport-Anzeige).
	UseTLS bool
	// CertFingerprint (SHA-256), CertSource ("Selbst erzeugt"/"Eigenes cert/key"), CertNotAfter für Settings.
	CertFingerprint string
	CertSource      string
	CertNotAfter    time.Time
	// MaxUploadBytes: Limit für ROM-Uploads (0 = DefaultMaxUploadBytes).
	MaxUploadBytes int64
}

// Server ist das Webinterface.
type Server struct {
	svc   *hub.Service
	cfg   Config
	log   *slog.Logger
	tmpl  map[string]*template.Template
	login *limiter
}

// New erzeugt das Webinterface.
func New(svc *hub.Service, cfg Config, log *slog.Logger) (*Server, error) {
	if log == nil {
		log = slog.Default()
	}
	if cfg.MaxUploadBytes <= 0 {
		cfg.MaxUploadBytes = DefaultMaxUploadBytes
	}
	s := &Server{svc: svc, cfg: cfg, log: log, tmpl: map[string]*template.Template{},
		login: &limiter{max: 5, window: time.Minute, now: svc.Now, hits: map[string][]time.Time{}}}
	for _, p := range []string{"login", "setup", "library", "clients", "settings"} {
		t, err := template.New(p).ParseFS(templatesFS, "templates/layout.html", "templates/"+p+".html")
		if err != nil {
			return nil, err
		}
		s.tmpl[p] = t
	}
	return s, nil
}

// Register hängt die Web-Routen an mux. "/" ist der Catch-all (Redirect auf /setup ohne Admin, sonst 404).
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
	h("GET /library", s.guard(s.libraryGet))
	h("POST /library/upload", s.guard(s.libraryUpload))
	h("POST /library/{id}/delete", s.guard(s.libraryDelete))
	h("GET /clients", s.guard(s.clientsGet))
	h("POST /clients/requests/{id}/allow", s.guard(s.clientAllow))
	h("POST /clients/requests/{id}/deny", s.guard(s.clientDeny))
	h("POST /clients/devices/{id}/revoke", s.guard(s.clientRevoke))
	h("GET /settings", s.guard(s.settingsGet))
	h("POST /settings/name", s.guard(s.settingsName))
	h("POST /settings/password", s.guard(s.settingsPassword))
}

// secure setzt Sicherheits-Header (kein Inline-Skript/-Style) und verhindert Caching der Seiten.
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

// ---- Seitenmodell ----

type pageData struct {
	Title, Nav          string
	CSRF                string
	User, Role          string
	HubName, HubVersion string
	Pending             int
	Flash, Error        string
	Fragment            bool
	Body                any
}

var flashTexts = map[string]string{
	"uploaded": "ROM wurde zur Library hinzugefügt.",
	"deleted":  "ROM wurde gelöscht.",
	"name":     "Hub-Name gespeichert.",
	"password": "Passwort geändert.",
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
	d := pageData{Title: title, Nav: nav, HubName: info.Name, HubVersion: ver, Flash: flashTexts[r.URL.Query().Get("ok")]}
	if sess != nil {
		d.CSRF = sess.CSRFToken
		d.User = sess.User.DisplayName
		d.Role = roleLabel(sess.User.Role)
		if p, err := s.svc.ListPendingRequests(r.Context()); err == nil {
			d.Pending = len(p)
		}
	}
	return d
}

func roleLabel(r hub.Role) string {
	if r == hub.RoleAdmin {
		return "Admin"
	}
	return "Benutzer"
}

// render schreibt Template name ("layout", "bare" oder ein Fragment) der Seite page.
func (s *Server) render(w http.ResponseWriter, status int, page, name string, d pageData) {
	var buf bytes.Buffer
	if err := s.tmpl[page].ExecuteTemplate(&buf, name, d); err != nil {
		s.log.Error("Template", "page", page, "err", err)
		http.Error(w, "Interner Fehler", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(status)
	w.Write(buf.Bytes())
}

func isHX(r *http.Request, target string) bool {
	return r.Header.Get("HX-Request") == "true" && r.Header.Get("HX-Target") == target
}

// ---- Guard: Setup-Redirect, Session, CSRF ----

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
		if err != nil || ws.User.Role != hub.RoleAdmin { // Phase 1: nur Admins im Web
			s.clearSession(w)
			s.redirect(w, r, "/login")
			return
		}
		sess := &session{ws}
		if r.Method == http.MethodPost {
			if strings.HasPrefix(r.Header.Get("Content-Type"), "multipart/form-data") {
				// Streaming-Upload: der Handler prüft _csrf (vor der Datei) oder den Header.
				if tok := r.Header.Get("X-CSRF-Token"); tok != "" {
					if !hub.TokenEqual(tok, ws.CSRFToken) {
						http.Error(w, "CSRF-Prüfung fehlgeschlagen", http.StatusForbidden)
						return
					}
					r = r.WithContext(context.WithValue(r.Context(), keyCSRFChecked, true))
				}
			} else {
				r.Body = http.MaxBytesReader(w, r.Body, maxFormBytes)
				tok := r.Header.Get("X-CSRF-Token")
				if err := r.ParseForm(); err != nil {
					http.Error(w, "Ungültige Anfrage", http.StatusBadRequest)
					return
				}
				if tok == "" {
					tok = r.PostFormValue("_csrf")
				}
				if !hub.TokenEqual(tok, ws.CSRFToken) {
					http.Error(w, "CSRF-Prüfung fehlgeschlagen", http.StatusForbidden)
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
	s.log.Error("Fehler", "method", r.Method, "path", r.URL.Path, "err", err)
	http.Error(w, "Interner Fehler", http.StatusInternalServerError)
}

// ---- Cookies, CSRF vor der Anmeldung, Login-Begrenzung ----

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

// preCSRF liefert das Double-Submit-Token für Formulare ohne Session und setzt bei Bedarf das Cookie.
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

// blocked meldet, ob key das Limit erreicht hat.
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
