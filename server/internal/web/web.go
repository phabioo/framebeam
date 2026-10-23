// Package web is the web interface of the Uzume server (html/template + htmx via embed, no Node build).
// It uses only the service layer internal/hub.
package web

import (
	"bytes"
	"context"
	"embed"
	"errors"
	"html/template"
	"io/fs"
	"log/slog"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/diag"
	"github.com/phabioo/uzume/server/internal/health"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/statusinfo"
	"github.com/phabioo/uzume/server/internal/turnsrv"
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
	// hostPrefix is prepended to both cookie names when TLS is on (Secure, Path=/, no Domain).
	hostPrefix   = "__Host-"
	maxFormBytes = 1 << 20
	// multipartSlack is the headroom for multipart framing and fields above the file limit.
	multipartSlack = 16 << 10
)

// Config describes operational data the service does not know.
type Config struct {
	// Listen is the configured listen address (shown in Settings).
	Listen string
	// UseTLS: HTTPS is active (cookie "Secure", transport display).
	UseTLS bool
	// TrustedProxy lists the configured -trusted-proxy networks (read-only, shown in Settings > Security).
	TrustedProxy []string
	// CertFingerprint (SHA-256), CertSource ("Self-generated"/"Own cert/key"), CertNotAfter for Settings.
	CertFingerprint string
	CertSource      string
	CertNotAfter    time.Time
	// CertState, when set, returns the current fingerprint and expiry and overrides CertFingerprint/CertNotAfter
	// (the certificate can change at runtime after RenewCert).
	CertState func() (fingerprint string, notAfter time.Time)
	// RenewCert replaces the self-generated certificate without a restart and returns the new fingerprint and
	// expiry. nil when not supported (own certificate, plain HTTP): Settings then shows no "Renew now".
	RenewCert func() (fingerprint string, notAfter time.Time, err error)
	// PublicHost and PublicPort (0: unknown) are the externally used address (-public-host) for invite links.
	PublicHost string
	PublicPort int
	// ImportDir is the library import folder shown in the Library ("Rescan folder").
	ImportDir string
	// MaxUploadBytes: limit for ROM uploads (0 = DefaultMaxUploadBytes).
	MaxUploadBytes int64
	// TURN is the embedded TURN server (nil: off), shown on the Settings page.
	TURN *turnsrv.Server
	// Net describes the network settings and the restart hook (Settings > Network).
	Net NetConfig
	// Errors holds the recent WARN/ERROR log records for the Status page (default diag.RecentErrors).
	Errors *diag.ErrorRing
	// Log holds the recent log lines for "Download log" (default diag.Recent).
	Log *diag.Ring
	// Disk replaces the disk reader of the Status page (tests only).
	Disk func(dir string) (total, free uint64, ok bool)
}

// Server is the web interface.
type Server struct {
	svc   *hub.Service
	cfg   Config
	log   *slog.Logger
	tmpl  map[string]*template.Template
	login *limiter
	// verifySem bounds concurrent Argon2id verifications (64 MiB each) so parallel requests cannot exhaust memory.
	verifySem chan struct{}
	src       *statusinfo.Source
	done      chan struct{} // closed by Shutdown: long-lived streams (SSE) end
	once      sync.Once
}

// New creates the web interface.
func New(svc *hub.Service, cfg Config, log *slog.Logger) (*Server, error) {
	if log == nil {
		log = slog.Default()
	}
	if cfg.MaxUploadBytes <= 0 {
		cfg.MaxUploadBytes = DefaultMaxUploadBytes
	}
	if cfg.Errors == nil {
		cfg.Errors = diag.RecentErrors
	}
	if cfg.Log == nil {
		cfg.Log = diag.Recent
	}
	s := &Server{svc: svc, cfg: cfg, log: log, tmpl: map[string]*template.Template{}, done: make(chan struct{}),
		login: newLimiter(svc.Now), verifySem: make(chan struct{}, maxConcurrentVerify)}
	var turnIssues []string
	for _, is := range cfg.Net.Issues {
		if is.Key == config.NetTURN {
			turnIssues = append(turnIssues, is.Message)
		}
	}
	s.src = &statusinfo.Source{Svc: svc, TURN: cfg.TURN, PublicHost: cfg.PublicHost, PublicPort: cfg.PublicPort,
		ListenPort: func() int { return cfg.Net.Running.ListenPort() }, TURNIssues: turnIssues, Errors: cfg.Errors, Log: cfg.Log, Disk: cfg.Disk}
	for _, p := range []string{"login", "setup", "invite", "library", "saves", "clients", "settings", "users", "systems", "confirm", "plugins", "runs", "status", "reports", "notfound"} {
		t, err := template.New(p).Funcs(template.FuncMap{"asset": assetURL}).ParseFS(templatesFS, "templates/layout.html", "templates/"+p+".html")
		if err != nil {
			return nil, err
		}
		s.tmpl[p] = t
	}
	return s, nil
}

// StatusSource is the health data source of the Status page; the admin REST API uses the same one.
func (s *Server) StatusSource() *statusinfo.Source { return s.src }

// Shutdown ends long-lived responses (the /events streams) so a graceful http.Server.Shutdown does not
// wait for them. Idempotent; wire it with http.Server.RegisterOnShutdown.
func (s *Server) Shutdown() { s.once.Do(func() { close(s.done) }) }

// Register attaches the web routes to mux. "/" is the catch-all (redirects to /setup without an admin, otherwise 404).
func (s *Server) Register(mux *http.ServeMux) {
	sub, _ := fs.Sub(staticFS, "static")
	static := http.StripPrefix("/static/", http.FileServerFS(sub))
	mux.Handle("GET /static/", secure(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", staticCacheControl(r))
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
	h("GET /invite", s.inviteLanding)
	h("GET /login", s.loginGet)
	h("POST /login", s.loginPost)
	h("POST /logout", s.guard(s.logout))
	h("GET /nav-fragment", s.guard(s.navFragment))
	mux.Handle("GET /events", secure(s.guard(s.events)))
	h("GET /library", s.guard(s.libraryGet))
	h("POST /library/upload", s.guardUpload(s.libraryUpload))
	h("POST /library/rescan", s.guard(s.libraryRescan))
	h("POST /library/uploads", s.guard(s.uploadCreate))
	h("GET /library/uploads/{id}", s.guard(s.uploadStatus))
	h("POST /library/uploads/{id}/chunk", s.guardChunk(s.uploadChunk))
	h("POST /library/uploads/{id}/abort", s.guard(s.uploadAbort))
	h("POST /library/{id}/rename", s.guard(s.libraryRename))
	h("POST /library/{id}/delete", s.guard(s.libraryDelete))
	h("GET /library/download/{id}", s.guard(s.libraryDownload))
	h("GET /saves", s.guard(s.savesGet))
	h("GET /saves/{user}/{game}/{slot}", s.guard(s.savesGet))
	h("GET /saves/{user}/{game}/{slot}/download", s.guard(s.saveDownload))
	h("GET /saves/{user}/{game}/{slot}/history/{version}/download", s.guard(s.saveHistoryDownload))
	h("POST /saves/{user}/{game}/{slot}/conflicts/{id}/resolve", s.guard(s.saveResolve))
	h("POST /saves/{user}/{game}/{slot}/history/{version}/restore", s.guard(s.saveRestore))
	h("POST /saves/{user}/{game}/{slot}/snapshots", s.guard(s.saveSnapshot))
	h("POST /saves/{user}/{game}/{slot}/history/{version}/delete", s.guard(s.saveDeleteSnapshot))
	h("POST /saves/{user}/{game}/{slot}/slots", s.guard(s.saveNewSlot))
	h("POST /saves/{user}/{game}/{slot}/rename", s.guard(s.saveRenameSlot))
	h("POST /saves/{user}/{game}/{slot}/delete", s.guard(s.saveDeleteSlot))
	h("POST /saves/{user}/{game}/{slot}/upload", s.guardUpload(s.saveUpload))
	h("GET /clients", s.guard(s.clientsGet))
	h("POST /clients/requests/{id}/allow", s.guard(s.clientAllow))
	h("POST /clients/requests/{id}/deny", s.guard(s.clientDeny))
	h("GET /clients/devices/{id}/revoke", s.guard(s.clientRevokeConfirm))
	h("POST /clients/devices/{id}/revoke", s.guard(s.clientRevoke))
	h("POST /clients/devices/{id}/rename", s.guard(s.clientRename))
	h("GET /clients/devices/{id}/delete", s.guard(s.clientDeleteConfirm))
	h("POST /clients/devices/{id}/delete", s.guard(s.clientDelete))
	h("GET /users", s.guard(s.usersGet))
	h("POST /users/create", s.guard(s.userCreate))
	h("POST /users/invites", s.guard(s.inviteCreate))
	h("GET /users/invites/{id}/revoke", s.guard(s.inviteRevokeConfirm))
	h("POST /users/invites/{id}/revoke", s.guard(s.inviteRevoke))
	h("POST /users/{id}/rename", s.guard(s.userRename))
	h("POST /users/{id}/code", s.guard(s.userCodeNew))
	h("POST /users/{id}/role", s.guard(s.userRoleUser))
	h("GET /users/{id}/admin", s.guard(s.userAdminForm))
	h("POST /users/{id}/admin", s.guard(s.userAdmin))
	h("POST /users/{id}/disable", s.guard(s.userDisable))
	h("POST /users/{id}/enable", s.guard(s.userEnable))
	h("GET /users/{id}/delete", s.guard(s.userDeleteConfirm))
	h("POST /users/{id}/delete", s.guard(s.userDelete))
	h("GET /plugins", s.guard(s.pluginsGet))
	h("GET /plugins/{id}/{version}", s.guard(s.pluginsGet))
	h("GET /plugins/{id}/{version}/download", s.guard(s.pluginDownload))
	h("GET /plugins/{id}/{version}/remove", s.guard(s.pluginRemoveConfirm))
	h("POST /plugins/{id}/{version}/remove", s.guard(s.pluginRemove))
	h("POST /plugins/upload", s.guardUpload(s.pluginUpload))
	h("POST /plugins/offer", s.guard(s.pluginOffer))
	h("POST /plugins/discard", s.guard(s.pluginDiscard))
	h("POST /plugins/index/refresh", s.guard(s.pluginIndexRefresh))
	h("POST /plugins/index/install", s.guard(s.pluginIndexInstall))
	h("GET /systems", s.guard(s.systemsGet))
	h("POST /systems/{id}/cores/{core}/install", s.guard(s.coreInstall))
	h("POST /systems/{id}/cores/{core}/update", s.guard(s.coreUpdate))
	h("GET /systems/{id}/cores/{core}/remove", s.guard(s.coreRemoveConfirm))
	h("POST /systems/{id}/cores/{core}/remove", s.guard(s.coreRemove))
	h("POST /systems/{id}/cores/{core}/auto-update", s.guard(s.coreAutoUpdate))
	h("POST /systems/{id}/auto-update", s.guard(s.systemAutoUpdate))
	h("POST /systems/{id}/cores/{core}/default", s.guard(s.coreDefault))
	h("POST /systems/{id}/firmware-mode", s.guard(s.systemFirmwareMode))
	h("POST /systems/{id}/firmware/{file}/upload", s.guardUpload(s.firmwareUpload))
	h("POST /systems/{id}/firmware/{file}/pin", s.guard(s.firmwarePin))
	h("GET /systems/{id}/firmware/{file}/remove", s.guard(s.firmwareRemoveConfirm))
	h("GET /systems/{id}/firmware/{file}/download", s.guard(s.firmwareDownload))
	h("POST /systems/{id}/firmware/{file}/remove", s.guard(s.firmwareRemove))
	h("POST /cores/sync", s.guard(s.coresSync))
	h("POST /cores/auto-update", s.guard(s.coresAutoUpdate))
	h("GET /runs", s.guard(s.runsList))
	h("GET /runs/{id}", s.guard(s.runsDetail))
	h("POST /runs/{id}/pause", s.guard(s.runStatusAction(hub.RunPaused, "runpaused")))
	h("POST /runs/{id}/resume", s.guard(s.runStatusAction(hub.RunActive, "runresumed")))
	h("POST /runs/{id}/archive", s.guard(s.runArchiveAction(true, "runarchived")))
	h("POST /runs/{id}/unarchive", s.guard(s.runArchiveAction(false, "rununarchived")))
	h("POST /runs/{id}/start", s.guard(s.runStart(false, "runstarted")))
	h("POST /runs/{id}/start-without", s.guard(s.runStart(true, "runstarted")))
	h("GET /runs/{id}/finish", s.guard(s.runFinishConfirm))
	h("POST /runs/{id}/finish", s.guard(s.runFinish))
	h("POST /runs/{id}/resend", s.guard(s.runResendAll))
	h("POST /runs/{id}/invites/{uid}/resend", s.guard(s.runResendInvite))
	h("GET /runs/{id}/invites/{uid}/remove", s.guard(s.runRemoveInviteConfirm))
	h("POST /runs/{id}/invites/{uid}/remove", s.guard(s.runRemoveInvite))
	h("GET /runs/{id}/export", s.guard(s.runExport))
	h("GET /runs/{id}/delete", s.guard(s.runDeleteConfirm))
	h("POST /runs/{id}/delete", s.guard(s.runDelete))
	h("GET /reports", s.guard(s.reportsGet))
	h("GET /reports/{id}", s.guard(s.reportsGet))
	h("POST /reports/{id}/seen", s.guard(s.reportSeen))
	h("GET /reports/{id}/delete", s.guard(s.reportDeleteConfirm))
	h("POST /reports/{id}/delete", s.guard(s.reportDelete))
	h("GET /reports/{id}/files/{name}", s.guard(s.reportFile))
	h("GET /reports/{id}/archive", s.guard(s.reportArchive))
	h("GET /status", s.guard(s.statusGet))
	h("GET /status/log", s.guard(s.statusLog))
	h("GET /metrics", s.metricsGet)
	h("GET /settings", s.guard(s.settingsGet))
	h("GET /settings/{section}", s.guard(s.settingsGet))
	h("POST /settings/network/restart", s.guard(s.settingsNetRestart))
	h("POST /settings/network/external-turn", s.guard(s.settingsExternalTURNSave))
	h("POST /settings/network/external-turn/delete", s.guard(s.settingsExternalTURNDelete))
	h("POST /settings/network/external-turn/reset", s.guard(s.settingsExternalTURNReset))
	h("POST /settings/network/{key}", s.guard(s.settingsNetSave))
	h("POST /settings/network/{key}/reset", s.guard(s.settingsNetReset))
	h("POST /settings/appearance", s.guard(s.settingsAppearance))
	h("POST /settings/uploads", s.guard(s.settingsUploads))
	h("POST /settings/updates", s.guard(s.settingsUpdates))
	h("POST /settings/updates/check", s.guard(s.settingsUpdatesCheck))
	h("POST /settings/updates/install", s.guard(s.settingsUpdatesInstall))
	h("POST /settings/updates/rollback", s.guard(s.settingsUpdatesRollback))
	h("POST /settings/updates/resume", s.guard(s.settingsUpdatesResume))
	h("POST /settings/name", s.guard(s.settingsName))
	h("POST /settings/password", s.guard(s.settingsPassword))
	h("POST /settings/security/renew-cert", s.guard(s.settingsRenewCert))
	h("POST /settings/diagnostics/bundle", s.guard(s.settingsDiagBundle))
	h("GET /settings/backups/download", s.guard(s.backupsDownload))
	h("POST /settings/backups/schedule", s.guard(s.backupsSchedule))
	h("POST /settings/backups/folder", s.guard(s.backupsFolder))
	h("POST /settings/backups/run", s.guard(s.backupsRun))
	h("POST /settings/backups/cancel", s.guard(s.backupsCancel))
	h("POST /settings/backups/recheck", s.guard(s.backupsRecheck))
	h("POST /settings/backups/dismiss", s.guard(s.backupsDismiss))
	h("POST /settings/backups/delete", s.guard(s.backupsDelete))
	h("POST /settings/backups/restore", s.guard(s.backupsRestore))
	h("POST /settings/metrics", s.guard(s.settingsMetrics))
	h("POST /settings/metrics/token", s.guard(s.settingsMetricsToken))
}

// secure sets security headers (no inline script/style) and prevents caching of pages.
func secure(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		h := w.Header()
		if hub.RequestHTTPS(r) {
			// Only when a trusted proxy terminates TLS (normally with a real certificate). Not sent for the server's own
			// self-signed certificate: HSTS would make the browser's certificate warning impossible to click through.
			h.Set("Strict-Transport-Security", "max-age=31536000")
		}
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
	s.notFound(w, r)
}

// notFound answers 404. Signed-in admins get a styled page inside the app shell with a way back; everyone else
// (and API callers) gets the plain Go 404 so nothing about the server is revealed.
func (s *Server) notFound(w http.ResponseWriter, r *http.Request) {
	p := r.URL.Path
	if strings.HasPrefix(p, "/api/") || strings.HasPrefix(p, "/.well-known/") {
		http.NotFound(w, r)
		return
	}
	c, err := r.Cookie(s.cookieName(r, sessionCookie))
	if err != nil || c.Value == "" {
		http.NotFound(w, r)
		return
	}
	ws, err := s.svc.LookupWebSession(r.Context(), c.Value)
	if err != nil || ws.User.Role != hub.RoleAdmin {
		http.NotFound(w, r)
		return
	}
	d := s.base(r, &session{ws}, "", "Not found")
	d.Flash, d.Error = "", ""
	s.render(w, http.StatusNotFound, "notfound", "layout", d)
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
	ActiveRuns          int    // runs in status active (nav badge)
	UpdateAvailable     bool   // a newer server version is known (nav dot on Settings)
	IsAdmin             bool   // the signed-in user is an admin (members see only Play and Settings)
	StatusProblem       string // title of the most urgent server problem (badge on Status), admins only
	NewReports          int    // reports not yet marked seen (badge on Reports), admins only
	NavGroups           []navGroup
	Theme               string // light, dark or system (server setting)
	Flash, Error        string
	Fragment            bool
	InlineFlash         bool // the page renders the flash itself inside its own column (Users)
	MainSwap            bool // HX main swap: render only the main content plus out-of-band nav and title
	Body                any
}

var flashTexts = map[string]string{
	"uploaded":        "ROM added to the library.",
	"rescan":          "Scanning the import folder.",
	"userrenamed":     "User renamed.",
	"reportseen":      "Report marked as seen.",
	"reportdeleted":   "Report deleted.",
	"userrole":        "Role changed.",
	"devrevoked":      "Access revoked.",
	"devrenamed":      "Device renamed.",
	"certrenewed":     "Certificate renewed. Every app must confirm the new fingerprint on its next connection.",
	"deleted":         "ROM deleted.",
	"renamed":         "Title changed.",
	"name":            "Server name saved.",
	"password":        "Password changed.",
	"resolved":        "Conflict resolved.",
	"restored":        "Version restored as the new current checkpoint. The previous checkpoint is in the history.",
	"snapshot":        "Snapshot created.",
	"snapdeleted":     "Version deleted.",
	"slotcreated":     "Slot created from the current version.",
	"slotrenamed":     "Slot renamed. History and conflicts moved with it.",
	"slotdeleted":     "Slot deleted.",
	"maindeleted":     "Slot \"Main\" deleted.",
	"disabled":        "User disabled. Their devices are signed out and their Sessions ended.",
	"enabled":         "User enabled.",
	"userdeleted":     "User deleted, together with their devices, invites, saves and save history.",
	"devdeleted":      "Device deleted.",
	"revoked":         "Invite revoked.",
	"appearance":      "Appearance saved.",
	"uploads":         "Upload setting saved.",
	"coreinstalled":   "Core installed.",
	"coreupdated":     "Core updated. The old build was removed.",
	"coreremoved":     "Core removed.",
	"coredefault":     "Default core changed.",
	"fwmode":          "Firmware mode saved.",
	"fwfile":          "Firmware file saved.",
	"fwremoved":       "Firmware file removed.",
	"fwpin":           "Expected SHA-256 saved.",
	"coreautoupdate":  "Automatic core updates saved.",
	"coresync":        "Checking the libretro buildbot in the background. Reload the page in a moment.",
	"updates":         "Update settings saved. Checking for updates in the background.",
	"updatecheck":     "Checking for updates in the background. Reload the page in a moment.",
	"pluginoffered":   "Plugin offered to apps.",
	"pluginremoved":   "Plugin removed from the server. Apps keep their copy until they uninstall it.",
	"pluginindex":     "Plugin catalog checked.",
	"plugininstalled": "Plugin installed. Apps on this server can now install it.",
	"runpaused":       "Run paused. No new events are recorded until it is resumed.",
	"runresumed":      "Run resumed.",
	"runstarted":      "Run started.",
	"runfinished":     "Run finished.",
	"invitesent":      "Invite sent again.",
	"inviteremoved":   "Invite removed.",
	"runarchived":     "Run archived. Apps no longer list it.",
	"rununarchived":   "Run unarchived.",
	"rundeleted":      "Run deleted.",
	"updateresume":    "Automatic updates are on again.",
	"updateback":      "Going back. A safety backup was written first; the server installs the older version and restarts in a moment. Reload this page afterwards.",
	"updateinstall":   "Update downloaded and verified. The server installs it and restarts in a moment; reload this page afterwards.",
}

var errTexts = map[string]string{
	"noreport":          "That report no longer exists.",
	"stale":             "The slot changed in the meantime or the conflict was already resolved. Please review and decide again.",
	"label":             "The snapshot label must be at most 64 characters.",
	"toolarge":          "The save file is too large (maximum 64 MiB).",
	"emptyfile":         "Please select a save file that is not empty.",
	"slotname":          "Use lowercase letters, digits, - and _ (up to 32 characters) for the slot name.",
	"slotexists":        "A slot with this name already exists.",
	"slotprotected":     "The default slot cannot be renamed.",
	"running":           "A Session of this game is running. End it first; slots cannot be uploaded to, renamed or deleted meanwhile.",
	"openconflict":      "Resolve the open conflict of this slot first.",
	"nogame":            "Game not found.",
	"admin":             "Admins cannot be disabled or deleted.",
	"nodevice":          "Device not found.",
	"nouser":            "User not found.",
	"rolekept":          "The role cannot be changed: the last admin and your own account stay admins, and a disabled user cannot become one.",
	"noinvite":          "The invite is no longer active.",
	"renewfailed":       "The certificate could not be renewed. The previous certificate stays active; see the server log.",
	"renewunsupported":  "This server cannot renew its certificate here (own certificate or plain HTTP).",
	"nosystem":          "System or file not found.",
	"nofile":            "Firmware file not found.",
	"nocore":            "System or core not found.",
	"updatepackage":     "This server was not installed from the .deb package, so it cannot install updates itself. Use the manual command shown below.",
	"updatenone":        "There is no update to install.",
	"rollbackversion":   "That version is not one of the earlier versions that can be installed.",
	"rollbackconfirm":   "Going back replaces the database with the backup made before the update. Tick the box to confirm.",
	"rollbackrefused":   "Going back to that version is not safe; see the reason in the list.",
	"rollbackcontainer": "This server runs in a container. Pull the older image tag and recreate the container.",
	"rollbackfailed":    "Going back failed; nothing was changed. The server log has the reason.",
	"resumefailed":      "Automatic updates are not paused.",
	"updatebreaking":    "This update breaks apps seen in the last 30 days. Use \"Install anyway\" to confirm.",
	"norun":             "Run not found.",
	"runstate":          "The run changed in the meantime. Please review its status and try again.",
	"noplugin":          "Plugin not found.",
	"noindex":           "No plugin index is configured on this server.",
	"indexfailed":       "The plugin index could not be checked. The reason is shown in the catalog section.",
	"updatefailed":      "The update could not be downloaded or verified. See the update status below.",
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
		if n, err := s.svc.ActiveRunCount(r.Context()); err == nil {
			d.ActiveRuns = n
		}
		d.UpdateAvailable = s.svc.UpdateAvailableBadge(r.Context())
		d.IsAdmin = sess.User.Role == hub.RoleAdmin
		if d.IsAdmin {
			if p := health.MostUrgent(s.problems(r.Context())); p != nil {
				d.StatusProblem = p.Title
			}
			if n, err := s.svc.NewReportCount(r.Context()); err == nil {
				d.NewReports = n
			}
		}
		d.NavGroups = navGroups(navInput{Admin: d.IsAdmin, Active: nav, Conflicts: d.Conflicts, Firmware: d.Firmware, Pending: d.Pending,
			ActiveRuns: d.ActiveRuns, UpdateAvailable: d.UpdateAvailable, StatusProblem: d.StatusProblem, NewReports: d.NewReports})
	}
	return d
}

// navFragment renders the sidebar nav (badges and active item); the active item comes from HX-Current-URL.
func (s *Server) navFragment(w http.ResponseWriter, r *http.Request, sess *session) {
	nav := navActiveFromURL(r.Header.Get("HX-Current-URL"))
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

// guard checks the setup state, the admin session and the CSRF token of every POST. Multipart bodies must carry
// the token in X-CSRF-Token or in the _csrf field (the form is parsed with the 1 MiB cap).
func (s *Server) guard(f handlerFunc) http.HandlerFunc { return s.guardWith(false, f) }

// guardUpload is the guard for the three streaming upload routes (ROM, save file, firmware). Only these may defer
// the CSRF check of a multipart body to the handler, which verifies _csrf before it reads the file part.
func (s *Server) guardUpload(f handlerFunc) http.HandlerFunc { return s.guardWith(true, f) }

// guardChunk is the guard for the raw-body chunk route of the resumable ROM upload: a POST must carry the token
// in X-CSRF-Token (the body is binary and is read by the handler, never parsed as a form).
func (s *Server) guardChunk(f handlerFunc) http.HandlerFunc { return s.guardCore(false, true, f) }

func (s *Server) guardWith(streamingUpload bool, f handlerFunc) http.HandlerFunc {
	return s.guardCore(streamingUpload, false, f)
}

func (s *Server) guardCore(streamingUpload, rawBody bool, f handlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if has, err := s.svc.HasAdmin(r.Context()); err != nil {
			s.fail(w, r, err)
			return
		} else if !has {
			s.redirect(w, r, "/setup")
			return
		}
		c, err := r.Cookie(s.cookieName(r, sessionCookie))
		if err != nil || c.Value == "" {
			s.redirect(w, r, "/login")
			return
		}
		ws, err := s.svc.LookupWebSession(r.Context(), c.Value)
		if err != nil && !errors.Is(err, hub.ErrUnauthorized) { // e.g. a transient DB error: keep the cookie
			s.fail(w, r, err)
			return
		}
		if err != nil || ws.User.Role != hub.RoleAdmin { // the web interface is admin-only
			s.clearSession(w, r)
			s.redirect(w, r, "/login")
			return
		}
		sess := &session{ws}
		if r.Method == http.MethodPost && rawBody {
			if !hub.TokenEqual(r.Header.Get("X-CSRF-Token"), ws.CSRFToken) {
				http.Error(w, "CSRF check failed", http.StatusForbidden)
				return
			}
		} else if r.Method == http.MethodPost {
			multipart := strings.HasPrefix(r.Header.Get("Content-Type"), "multipart/form-data")
			if multipart && streamingUpload {
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
				var err error
				if multipart {
					err = r.ParseMultipartForm(maxFormBytes)
					if r.MultipartForm != nil {
						defer r.MultipartForm.RemoveAll()
					}
				} else {
					err = r.ParseForm()
				}
				if err != nil {
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

// secureReq reports whether the client side of r is https: the server itself serves TLS, or a trusted proxy says so.
func (s *Server) secureReq(r *http.Request) bool { return s.cfg.UseTLS || hub.RequestHTTPS(r) }

// cookieName returns the cookie name for base: with the __Host- prefix when the client side is https.
func (s *Server) cookieName(r *http.Request, base string) string {
	if s.secureReq(r) {
		return hostPrefix + base
	}
	return base
}

func (s *Server) setCookie(w http.ResponseWriter, r *http.Request, base, value string, maxAge int) {
	http.SetCookie(w, &http.Cookie{Name: s.cookieName(r, base), Value: value, Path: "/", MaxAge: maxAge, HttpOnly: true,
		SameSite: http.SameSiteStrictMode, Secure: s.secureReq(r)})
}

func (s *Server) clearSession(w http.ResponseWriter, r *http.Request) {
	s.setCookie(w, r, sessionCookie, "", -1)
}

func (s *Server) startSession(w http.ResponseWriter, r *http.Request, userID string) error {
	tok, _, err := s.svc.CreateWebSession(r.Context(), userID)
	if err != nil {
		return err
	}
	s.setCookie(w, r, sessionCookie, tok, int(hub.WebSessionTTL.Seconds()))
	return nil
}

// preCSRF returns the double-submit token for forms without a session and sets the cookie if needed.
func (s *Server) preCSRF(w http.ResponseWriter, r *http.Request) string {
	if c, err := r.Cookie(s.cookieName(r, csrfCookie)); err == nil && len(c.Value) >= 20 {
		return c.Value
	}
	tok, err := hub.RandomToken()
	if err != nil {
		return ""
	}
	s.setCookie(w, r, csrfCookie, tok, int((2 * time.Hour).Seconds()))
	return tok
}

func (s *Server) checkPreCSRF(r *http.Request) bool {
	c, err := r.Cookie(s.cookieName(r, csrfCookie))
	if err != nil || c.Value == "" {
		return false
	}
	return hub.TokenEqual(r.PostFormValue("_csrf"), c.Value)
}

// remoteIP is the key for the login limiter and pairing records: the real client IP (behind a trusted proxy the
// address from its forwarding headers), or a non-IP key for the proxy itself when it gave none.
func remoteIP(r *http.Request) string { return hub.ClientKey(r) }

// isLoopback reports whether r comes from the machine the server runs on (see hub.IsLocalRequest).
func isLoopback(r *http.Request) bool { return hub.IsLocalRequest(r) }
