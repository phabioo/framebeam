package diag

import (
	"archive/zip"
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/version"
)

// Bundle layout (the app bundle mirrors it):
//
//	README.txt        what the bundle contains and what was removed
//	hub-info.json     versions, platform, network, cores, counts, devices (redacted)
//	logs/<name>       the server's log files, newest first (hub.log, hub.log.1, updater.log ...), redacted
//	logs/recent.log   running mode: the server's last log lines from memory (about 5000 lines / 4 MiB), redacted
//	logs/journal.txt  Linux: recent journal of the uzume-server unit, redacted (when journalctl is available)
const (
	EntryReadme  = "README.txt"
	EntryInfo    = "hub-info.json"
	EntryJournal = "logs/journal.txt"
	EntryRecent  = "logs/recent.log"
	LogsPrefix   = "logs/"
	// InfoVersion is the format version of hub-info.json.
	InfoVersion = 1
	// DefaultMaxLogBytes caps the log content of one bundle (before compression).
	DefaultMaxLogBytes int64 = 50 << 20
	journalUnit              = "uzume-server"
)

var processStart = time.Now()

// FileName is the download name uzume-diagnostics-<UTC timestamp>.zip.
func FileName(t time.Time) string {
	return "uzume-diagnostics-" + t.UTC().Format("20060102T150405Z") + ".zip"
}

// Options controls what the bundle contains.
type Options struct {
	// Config is the effective (running) configuration; DataDir and the network values are reported from it.
	Config config.Config
	// LogDir holds the server's log files (default <data-dir>/logs).
	LogDir string
	// MaxLogBytes caps the log content (default DefaultMaxLogBytes). The newest file comes first; the oldest file
	// that does not fit completely is cut to its newest lines, further files are left out.
	MaxLogBytes int64
	// Recent is the in-memory log ring (default Recent). The entry is written in running mode when it has lines.
	Recent *Ring
	// NoJournal skips the journalctl lookup (tests).
	NoJournal bool
	// Now overrides the clock (tests).
	Now func() time.Time
}

func (o *Options) now() time.Time {
	if o.Now != nil {
		return o.Now().UTC()
	}
	return time.Now().UTC()
}

func (o *Options) logDir() string {
	if o.LogDir != "" {
		return o.LogDir
	}
	return filepath.Join(o.Config.DataDir, "logs")
}

// ---- hub-info.json ----

// Info is hub-info.json. Fields marked service are left out when no database is open (offline CLI bundle).
type Info struct {
	InfoVersion int       `json:"info_version"`
	GeneratedAt time.Time `json:"generated_at"`
	Mode        string    `json:"mode"` // "running" (web interface) or "offline" (CLI, database not read)
	Hub         HubInfo   `json:"hub"`
	System      SysInfo   `json:"system"`
	Network     NetInfo   `json:"network"`
	Cores       *CoreInfo `json:"cores,omitempty"`   // service
	Counts      *Counts   `json:"counts,omitempty"`  // service
	Devices     []Device  `json:"devices,omitempty"` // service
	Features    []string  `json:"features,omitempty"`
}

type HubInfo struct {
	Name               string `json:"name,omitempty"`
	ID                 string `json:"id,omitempty"`
	Version            string `json:"version"`
	Channel            string `json:"channel"`
	Commit             string `json:"commit,omitempty"`
	ProtocolVersion    int    `json:"protocol_version,omitempty"`
	MinProtocolVersion int    `json:"min_protocol_version,omitempty"`
	GoVersion          string `json:"go_version"`
}

type SysInfo struct {
	OS               string    `json:"os"`
	Arch             string    `json:"arch"`
	NumCPU           int       `json:"num_cpu"`
	StartedAt        time.Time `json:"started_at"`
	UptimeSeconds    int64     `json:"uptime_seconds"`
	DataDir          string    `json:"data_dir"`
	DataDirFreeBytes uint64    `json:"data_dir_free_bytes"` // 0: unknown
}

type NetInfo struct {
	Listen         string            `json:"listen"`
	ListenPort     int               `json:"listen_port"`
	PublicHost     string            `json:"public_host,omitempty"`
	TLS            bool              `json:"tls"`
	DevMode        bool              `json:"dev_mode"`
	NetworkSharing bool              `json:"network_sharing"`
	TURNConfigured bool              `json:"turn_configured"`
	TURNRunning    *bool             `json:"turn_running,omitempty"` // service
	TURNPort       int               `json:"turn_port,omitempty"`
	TURNRelayPorts string            `json:"turn_relay_ports,omitempty"`
	TURNRelayIP    string            `json:"turn_relay_ip,omitempty"`
	Cloudflare     *CloudflareInfo   `json:"cloudflare_turn,omitempty"` // service
	ExternalTURN   *ExternalTURNInfo `json:"external_turn,omitempty"`   // service
	ICEServers     []string          `json:"ice_servers"`
	// Secrets (the TURN secret, TURN credentials, tokens) are never part of the bundle.
}

type CoreInfo struct {
	BuildbotURL     string      `json:"buildbot_url"`
	InfoURL         string      `json:"info_url"`
	LastCheck       *time.Time  `json:"last_check,omitempty"`
	LastSuccess     *time.Time  `json:"last_success,omitempty"`
	LastError       string      `json:"last_error,omitempty"`
	AutoUpdate      bool        `json:"auto_update"`
	AutoUpdateError string      `json:"auto_update_error,omitempty"`
	CatalogCores    int         `json:"catalog_cores"`
	Installed       []CoreEntry `json:"installed"`
}

type CoreEntry struct {
	System    string   `json:"system"`
	Core      string   `json:"core"`
	Name      string   `json:"name"`
	Version   string   `json:"version"`
	BuildDate string   `json:"build_date,omitempty"`
	Default   bool     `json:"default"`
	Platforms []string `json:"platforms,omitempty"`
}

type Counts struct {
	Users          int `json:"users"`
	Devices        int `json:"devices"`
	Games          int `json:"games"`
	ActiveSessions int `json:"active_sessions"`
}

type Device struct {
	ID            string     `json:"id"`
	Name          string     `json:"name"`
	Platform      string     `json:"platform"`
	Arch          string     `json:"arch"`
	PlayerVersion string     `json:"player_version"`
	Status        string     `json:"status"`
	Connected     bool       `json:"connected"`
	LastSeenAt    *time.Time `json:"last_seen_at,omitempty"`
}

// Collect gathers hub-info. svc may be nil (offline CLI bundle: no database access, so no running Session is
// touched); the service dependent sections are then left out.
func Collect(ctx context.Context, svc *hub.Service, o Options) (Info, error) {
	now := o.now()
	cfg := o.Config
	started := processStart.UTC()
	info := Info{InfoVersion: InfoVersion, GeneratedAt: now, Mode: "offline"}
	info.Hub = HubInfo{Version: version.String(), Channel: version.Channel, Commit: version.Commit, GoVersion: runtime.Version()}
	info.System = SysInfo{OS: runtime.GOOS, Arch: runtime.GOARCH, NumCPU: runtime.NumCPU(), StartedAt: started,
		UptimeSeconds: int64(now.Sub(started).Seconds()), DataDir: cfg.DataDir}
	ice := append([]string(nil), cfg.ICEServers...)
	if ice == nil {
		ice = []string{}
	}
	info.Network = NetInfo{Listen: cfg.Listen, ListenPort: cfg.ListenPort(), PublicHost: cfg.PublicHost, TLS: cfg.UseTLS(),
		DevMode: cfg.Dev, NetworkSharing: cfg.NetworkSharing, TURNConfigured: cfg.TURN, ICEServers: ice}
	if cfg.TURN {
		info.Network.TURNPort, info.Network.TURNRelayPorts, info.Network.TURNRelayIP = cfg.TURNPort, cfg.TURNRelayPorts, cfg.TURNRelayIP
	}
	if svc == nil {
		return info, nil
	}
	info.Mode = "running"
	hi := svc.Info()
	info.Hub.Name, info.Hub.ID = hi.Name, hi.HubID
	info.Hub.ProtocolVersion, info.Hub.MinProtocolVersion = hi.ProtocolVersion, hi.MinProtocolVersion
	info.System.DataDirFreeBytes = svc.DataDirFreeBytes()
	turn := svc.TURNEnabled()
	info.Network.TURNRunning = &turn
	info.Features = hub.BaseFeatures()
	info.Network.Cloudflare = cloudflareInfo(svc.CloudflareStatus())
	info.Network.ExternalTURN = externalTURNInfo(svc.ExternalTURNStatus(), svc.ExternalTURNSource(), svc.ExternalTURNActive())
	if svc.RelayEnabled() {
		info.Features = append(info.Features, hub.FeatureTURNV1)
	}

	src, err := svc.CoreSource(ctx)
	if err != nil {
		return info, fmt.Errorf("core source: %w", err)
	}
	ci := &CoreInfo{BuildbotURL: src.BuildbotURL, InfoURL: src.InfoURL, LastCheck: src.LastCheck, LastSuccess: src.LastSuccess,
		LastError: src.LastError, AutoUpdate: src.AutoUpdate, AutoUpdateError: src.AutoUpdateError, CatalogCores: src.Cores,
		Installed: []CoreEntry{}}
	reg, err := svc.ListRegistry(ctx)
	if err != nil {
		return info, fmt.Errorf("registry: %w", err)
	}
	for _, e := range reg {
		for _, c := range e.Cores {
			ci.Installed = append(ci.Installed, CoreEntry{System: e.ID, Core: c.CoreID, Name: c.DisplayName, Version: c.Version,
				BuildDate: c.BuildDate, Default: c.Default, Platforms: c.Platforms})
		}
	}
	info.Cores = ci

	n, err := svc.DiagCounts(ctx)
	if err != nil {
		return info, fmt.Errorf("counts: %w", err)
	}
	info.Counts = &Counts{Users: n.Users, Devices: n.Devices, Games: n.Games, ActiveSessions: n.ActiveSessions}

	devs, err := svc.ListDevices(ctx)
	if err != nil {
		return info, fmt.Errorf("devices: %w", err)
	}
	conn := map[string]bool{}
	for _, id := range svc.ConnectedDeviceIDs() {
		conn[id] = true
	}
	for _, d := range devs {
		info.Devices = append(info.Devices, Device{ID: d.ID, Name: d.Name, Platform: d.Platform, Arch: d.Arch,
			PlayerVersion: d.PlayerVersion, Status: string(d.Status), Connected: conn[d.ID], LastSeenAt: d.LastSeenAt})
	}
	return info, nil
}

// ---- zip ----

const readmeText = `Uzume server diagnostics bundle

hub-info.json   versions, platform, network configuration, installed cores, counts, paired apps
logs/           the server's log files, newest first, and on Linux the recent journal of the uzume-server service

This bundle contains IP addresses and host names (they are needed to debug connections), but no passwords or
tokens. Removed before packing: TURN secret and credentials (username/credential), tokens, passwords, invite
codes, Authorization headers and session cookies. Please look through the files before you share them.
`

// Write streams the zip to w. Log content is redacted line by line; the whole bundle is built without temporary
// files. Failing to read one log file is noted inside the bundle and does not abort it.
func Write(ctx context.Context, w io.Writer, info Info, o Options) error {
	now := o.now()
	zw := zip.NewWriter(w)
	add := func(name string) (io.Writer, error) {
		return zw.CreateHeader(&zip.FileHeader{Name: name, Method: zip.Deflate, Modified: now})
	}
	fw, err := add(EntryReadme)
	if err != nil {
		return err
	}
	if _, err := io.WriteString(fw, readmeText); err != nil {
		return err
	}
	raw, err := json.MarshalIndent(info, "", "  ")
	if err != nil {
		return err
	}
	if fw, err = add(EntryInfo); err != nil {
		return err
	}
	if _, err := fw.Write(append(Redact(raw), '\n')); err != nil {
		return err
	}

	budget := o.MaxLogBytes
	if budget <= 0 {
		budget = DefaultMaxLogBytes
	}
	files := logFiles(o.logDir())
	for _, f := range files {
		if budget <= 0 || ctx.Err() != nil {
			break
		}
		fw, err := add(LogsPrefix + f.name)
		if err != nil {
			return err
		}
		n, err := copyRedactedTail(fw, f.path, budget)
		budget -= n
		if err != nil {
			fmt.Fprintf(fw, "\n[could not read this file completely: %v]\n", err)
		}
	}
	ring := o.Recent
	if ring == nil {
		ring = Recent
	}
	if info.Mode == "running" {
		if snap := ring.Snapshot(); len(snap) > 0 {
			if fw, err = add(EntryRecent); err != nil {
				return err
			}
			if err := copyRedacted(fw, bytes.NewReader(snap)); err != nil {
				return err
			}
		}
	}
	if budget > 0 && !o.NoJournal && ctx.Err() == nil {
		if out := journal(ctx, budget); len(out) > 0 {
			if fw, err = add(EntryJournal); err != nil {
				return err
			}
			if err := copyRedacted(fw, bytes.NewReader(out)); err != nil {
				return err
			}
		}
	}
	return zw.Close()
}

type logFile struct {
	name, path string
	mod        time.Time
}

// logFiles lists the regular files of dir (hub.log, hub.log.1, updater.log ...), newest first.
func logFiles(dir string) []logFile {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var out []logFile
	for _, e := range ents {
		fi, err := e.Info()
		if err != nil || !fi.Mode().IsRegular() {
			continue
		}
		out = append(out, logFile{name: e.Name(), path: filepath.Join(dir, e.Name()), mod: fi.ModTime()})
	}
	sort.Slice(out, func(i, j int) bool {
		if !out[i].mod.Equal(out[j].mod) {
			return out[i].mod.After(out[j].mod)
		}
		return out[i].name < out[j].name
	})
	return out
}

// copyRedactedTail writes at most max bytes of the file's newest content (starting at a line boundary) and
// returns how many source bytes it used.
func copyRedactedTail(w io.Writer, path string, max int64) (int64, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	fi, err := f.Stat()
	if err != nil {
		return 0, err
	}
	size := fi.Size()
	r := bufio.NewReaderSize(f, 256<<10)
	used := size
	if size > max {
		if _, err := f.Seek(size-max, io.SeekStart); err != nil {
			return 0, err
		}
		r.Reset(f)
		if _, err := r.ReadBytes('\n'); err != nil { // drop the cut first line
			return 0, nil
		}
		used = max
		io.WriteString(w, "[older content cut: size cap of the bundle]\n")
	}
	return used, copyRedacted(w, r)
}

// copyRedacted copies r to w line by line, redacting each line.
func copyRedacted(w io.Writer, r io.Reader) error {
	br, ok := r.(*bufio.Reader)
	if !ok {
		br = bufio.NewReaderSize(r, 256<<10)
	}
	for {
		line, err := br.ReadSlice('\n')
		if len(line) > 0 {
			if _, werr := w.Write(Redact(line)); werr != nil {
				return werr
			}
		}
		switch {
		case err == nil, errors.Is(err, bufio.ErrBufferFull): // a very long line is redacted in pieces
		case errors.Is(err, io.EOF):
			return nil
		default:
			return err
		}
	}
}

// journal returns the recent journal of the server unit (Linux, best effort, empty when unavailable).
func journal(ctx context.Context, max int64) []byte {
	if runtime.GOOS != "linux" {
		return nil
	}
	path, err := exec.LookPath("journalctl")
	if err != nil {
		return nil
	}
	ctx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, path, "-u", journalUnit, "--no-pager", "-o", "short-iso", "-n", "5000").Output()
	if err != nil && len(out) == 0 {
		return nil
	}
	if int64(len(out)) > max {
		out = out[int64(len(out))-max:]
		if i := bytes.IndexByte(out, '\n'); i >= 0 {
			out = out[i+1:]
		}
	}
	return []byte(strings.TrimRight(string(out), "\n") + "\n")
}

// ExternalTURNInfo lists the external TURN servers in hub-info.json: URLs and credential mode, never a password or
// shared secret (only whether one is set).
type ExternalTURNInfo struct {
	Configured bool                 `json:"configured"`
	Source     string               `json:"source"` // "web" (saved in Settings, replaces the flags) or "flags" (-turn-external)
	Active     bool                 `json:"active"` // handed to apps (false while Network sharing is off)
	Servers    []ExternalTURNServer `json:"servers,omitempty"`
}

// ExternalTURNServer is one external TURN server in hub-info.json.
type ExternalTURNServer struct {
	Name      string   `json:"name"`
	Mode      string   `json:"mode"` // "secret" (shared secret, TURN REST API) or "static"
	URLs      []string `json:"urls"`
	SecretSet bool     `json:"auth_configured"` // the diagnostics redaction would blank a key named *secret*
}

func externalTURNInfo(st []hub.ExternalTURNStatus, source string, active bool) *ExternalTURNInfo {
	info := &ExternalTURNInfo{Configured: len(st) > 0, Source: source, Active: active}
	for _, e := range st {
		info.Servers = append(info.Servers, ExternalTURNServer{Name: e.Name, Mode: e.Mode, URLs: e.URLs, SecretSet: e.SecretSet})
	}
	return info
}

// CloudflareInfo is the Cloudflare TURN provider status in hub-info.json. The API token is never part of it and
// the key ID is shortened.
type CloudflareInfo struct {
	Configured  bool   `json:"configured"`
	KeyID       string `json:"key_id,omitempty"`       // first 4 characters only
	LastRefresh string `json:"last_refresh,omitempty"` // RFC 3339, empty: no attempt yet
	LastResult  string `json:"last_result,omitempty"`  // "ok" or the redacted error
}

func cloudflareInfo(st hub.CloudflareStatus) *CloudflareInfo {
	ci := &CloudflareInfo{Configured: st.Configured}
	if !st.Configured {
		return ci
	}
	if len(st.KeyID) > 4 {
		ci.KeyID = st.KeyID[:4] + "..."
	} else if st.KeyID != "" {
		ci.KeyID = "..."
	}
	if !st.LastAttempt.IsZero() {
		ci.LastRefresh = st.LastAttempt.UTC().Format(time.RFC3339)
		ci.LastResult = "ok"
		if st.LastError != "" {
			ci.LastResult = RedactString(st.LastError)
		}
	}
	return ci
}
