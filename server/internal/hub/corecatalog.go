package hub

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
	"path"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
)

// corePlatform maps a FrameBeam platform to its libretro buildbot path and library suffix (ADR 0020 D1).
type corePlatform struct {
	ID     string // FrameBeam platform
	Dir    string // <os>/<arch> below the buildbot base URL
	Suffix string // library suffix, e.g. ".so"
}

// corePlatforms are the platforms the Hub offers cores for; others follow when the Player exists for them.
var corePlatforms = []corePlatform{
	{"windows-x64", "windows/x86_64", ".dll"},
	{"linux-x64", "linux/x86_64", ".so"},
}

// Limits for the catalog sources.
const (
	maxInfoZipBytes   = 32 << 20
	maxInfoEntryBytes = 1 << 20
	maxInfoEntries    = 20000
	maxInfoTotalBytes = 128 << 20
	maxIndexExtBytes  = 16 << 20
)

var (
	dateRe  = regexp.MustCompile(`^\d{4}-\d{2}-\d{2}$`)
	crcRe   = regexp.MustCompile(`^[0-9a-fA-F]{8}$`)
	zipName = regexp.MustCompile(`^([a-z0-9][a-z0-9_-]{0,63})_libretro(\.[a-z0-9]+)\.zip$`)
)

// coreInfo is the metadata of a core from its libretro .info file.
type coreInfo struct {
	ID             string   `json:"id"`
	CoreName       string   `json:"core_name,omitempty"`
	DisplayName    string   `json:"display_name"`
	SystemID       string   `json:"system_id"`
	License        string   `json:"license"`
	DisplayVersion string   `json:"display_version,omitempty"`
	RequiredHWAPI  string   `json:"required_hw_api,omitempty"`
	Extensions     []string `json:"extensions,omitempty"`
	Notes          []string `json:"notes,omitempty"` // firmware descriptions and "(!)" notes
}

// catalogBuild is one upstream build of a core for one platform.
type catalogBuild struct {
	Date  string `json:"date"`  // YYYY-MM-DD
	CRC32 string `json:"crc32"` // 8 hex characters
	File  string `json:"file"`  // <core>_libretro<suffix>.zip
}

// catalogCore is a core of the catalog: its info and the builds per platform.
type catalogCore struct {
	coreInfo
	Builds map[string]catalogBuild `json:"builds"`
}

// newestDate is the newest build date over all platforms.
func (c catalogCore) newestDate() string {
	best := ""
	for _, b := range c.Builds {
		if b.Date > best {
			best = b.Date
		}
	}
	return best
}

type catalog struct {
	FetchedAt int64                  `json:"fetched_at"`
	Cores     map[string]catalogCore `json:"cores"`
}

func loadCatalogFile(p string) catalog {
	var c catalog
	if b, err := os.ReadFile(p); err == nil {
		_ = json.Unmarshal(b, &c)
	}
	if c.Cores == nil {
		c.Cores = map[string]catalogCore{}
	}
	return c
}

// parseInfoLines parses the `key = "value"` lines of a libretro .info file.
func parseInfoLines(data []byte) map[string]string {
	out := map[string]string{}
	sc := bufio.NewScanner(bytes.NewReader(data))
	sc.Buffer(make([]byte, 64<<10), 1<<20)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		k, v = strings.TrimSpace(k), strings.TrimSpace(v)
		if len(v) >= 2 && v[0] == '"' && v[len(v)-1] == '"' {
			v = v[1 : len(v)-1]
		}
		if k != "" {
			out[k] = v
		}
	}
	return out
}

func infoFromLines(id string, m map[string]string) coreInfo {
	ci := coreInfo{ID: id, CoreName: cleanText(m["corename"], 128), DisplayName: cleanText(m["display_name"], 128),
		SystemID: strings.ToLower(cleanText(m["systemid"], 64)), License: cleanText(m["license"], 128),
		DisplayVersion: cleanText(m["display_version"], 64), RequiredHWAPI: cleanText(m["required_hw_api"], 128)}
	if ci.DisplayName == "" {
		ci.DisplayName = ci.CoreName
	}
	if ci.DisplayName == "" {
		ci.DisplayName = id
	}
	for _, e := range strings.Split(m["supported_extensions"], "|") {
		if e = strings.TrimSpace(e); e != "" && len(ci.Extensions) < 64 {
			ci.Extensions = append(ci.Extensions, cleanText(e, 16))
		}
	}
	for i := 0; i < 32; i++ {
		if d := cleanText(m["firmware"+strconv.Itoa(i)+"_desc"], 160); d != "" {
			ci.Notes = append(ci.Notes, d)
		}
	}
	for _, n := range strings.Split(m["notes"], "|") {
		if n = strings.TrimSpace(n); strings.HasPrefix(n, "(!)") && len(ci.Notes) < 48 {
			ci.Notes = append(ci.Notes, cleanText(n, 200))
		}
	}
	return ci
}

// parseInfoZip reads the <core>_libretro.info files of info.zip. Bounded: entries, entry size and total size.
func parseInfoZip(data []byte) (map[string]coreInfo, error) {
	zr, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		return nil, fmt.Errorf("info.zip: %w", err)
	}
	if len(zr.File) > maxInfoEntries {
		return nil, errors.New("info.zip has too many entries")
	}
	out := map[string]coreInfo{}
	var total int64
	for _, f := range zr.File {
		base := path.Base(strings.ReplaceAll(f.Name, "\\", "/"))
		id, ok := strings.CutSuffix(base, "_libretro.info")
		if !ok || f.FileInfo().IsDir() || !corepkg.ValidCoreID(id) {
			continue
		}
		if f.UncompressedSize64 > maxInfoEntryBytes {
			continue
		}
		rc, err := f.Open()
		if err != nil {
			continue
		}
		b, err := io.ReadAll(io.LimitReader(rc, maxInfoEntryBytes+1))
		rc.Close()
		if err != nil || len(b) > maxInfoEntryBytes {
			continue
		}
		if total += int64(len(b)); total > maxInfoTotalBytes {
			return nil, errors.New("info.zip is too large when unpacked")
		}
		out[id] = infoFromLines(id, parseInfoLines(b))
	}
	return out, nil
}

// parseIndexExtended parses an .index-extended file ("<YYYY-MM-DD> [time] <crc32 hex> <file>") into the builds of
// `<core>_libretro<suffix>.zip` files, keyed by core id. Other lines are ignored.
func parseIndexExtended(data []byte, suffix string) map[string]catalogBuild {
	out := map[string]catalogBuild{}
	sc := bufio.NewScanner(bytes.NewReader(data))
	sc.Buffer(make([]byte, 64<<10), 1<<20)
	for sc.Scan() {
		f := strings.Fields(sc.Text())
		if len(f) < 3 {
			continue
		}
		date, crc, file := f[0], f[len(f)-2], f[len(f)-1]
		m := zipName.FindStringSubmatch(file)
		if m == nil || m[2] != suffix || !dateRe.MatchString(date) || !crcRe.MatchString(crc) {
			continue
		}
		if _, err := time.Parse("2006-01-02", date); err != nil {
			continue
		}
		out[m[1]] = catalogBuild{Date: date, CRC32: strings.ToLower(crc), File: file}
	}
	return out
}

func (s *Service) platformIndexURL(p corePlatform) string {
	return s.cores.buildbotURL + "/" + p.Dir + "/latest/.index-extended"
}

func (s *Service) buildURL(p corePlatform, file string) string {
	return s.cores.buildbotURL + "/" + p.Dir + "/latest/" + file
}

// CoreSyncReport summarizes one catalog refresh.
type CoreSyncReport struct {
	Cores         int           // cores with at least one build
	Updated       []CoreUpdated // cores auto-updated after the refresh
	UpdateErrors  []string      // per-core auto-update failures ("system/core: reason")
	AutoUpdateRan bool          // auto-update was enabled and the refresh succeeded
}

// CoreUpdated is one core installed to a newer build by the automatic update.
type CoreUpdated struct {
	SystemID, CoreID, Version string
}

// CoreAutoUpdate reports whether newer buildbot builds are installed automatically after each catalog refresh
// (default on).
func (s *Service) CoreAutoUpdate(ctx context.Context) (bool, error) {
	v, ok, err := s.getSetting(ctx, settingCoreAutoUpdate)
	if err != nil {
		return false, err
	}
	return !ok || v != "0", nil
}

// SetCoreAutoUpdate switches the automatic core update on or off. Turning it on kicks a sync so pending updates
// apply promptly.
func (s *Service) SetCoreAutoUpdate(ctx context.Context, on bool) (err error) {
	defer s.publishOK(&err, TopicSystems)
	v := "0"
	if on {
		v = "1"
	}
	if err := s.setSetting(ctx, settingCoreAutoUpdate, v); err != nil {
		return err
	}
	if on {
		s.TriggerCoreSync()
	}
	return nil
}

// Per-system override of the automatic core update (systems.core_auto_update) and per-core pin (system_cores.auto_update).
const (
	SystemAutoDefault = ""    // use the Hub default
	SystemAutoOn      = "on"  // update this system's cores, whatever the Hub default says
	SystemAutoOff     = "off" // never update this system's cores automatically
	CoreAutoFollow    = ""    // follow the system
	CoreAutoOff       = "off" // pinned: this core is never updated automatically
)

// effectiveAutoUpdate resolves the Hub default, the system override and the per-core pin.
func effectiveAutoUpdate(global bool, system, core string) bool {
	if core == CoreAutoOff {
		return false
	}
	switch system {
	case SystemAutoOn:
		return true
	case SystemAutoOff:
		return false
	}
	return global
}

// SetSystemCoreAutoUpdate sets the per-system override: SystemAutoDefault, SystemAutoOn or SystemAutoOff.
// Turning updates on kicks a sync so pending updates apply promptly.
func (s *Service) SetSystemCoreAutoUpdate(ctx context.Context, systemID, mode string) (err error) {
	defer s.publishOK(&err, TopicSystems)
	switch mode {
	case SystemAutoDefault, SystemAutoOn, SystemAutoOff:
	default:
		return badRequest("unknown automatic update mode")
	}
	res, err := s.db.ExecContext(ctx, `UPDATE systems SET core_auto_update = ? WHERE id = ?`, mode, systemID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	if mode == SystemAutoOn {
		s.TriggerCoreSync()
	}
	return nil
}

// SetCoreAutoUpdatePin pins an installed core (never updated automatically) or lets it follow its system again.
func (s *Service) SetCoreAutoUpdatePin(ctx context.Context, systemID, coreID string, pinned bool) (err error) {
	defer s.publishOK(&err, TopicSystems)
	v := CoreAutoFollow
	if pinned {
		v = CoreAutoOff
	}
	res, err := s.db.ExecContext(ctx, `UPDATE system_cores SET auto_update = ? WHERE system_id = ? AND core_id = ?`, v, systemID, coreID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	if !pinned {
		s.TriggerCoreSync()
	}
	return nil
}

// autoUpdateWanted reports whether any core can be updated automatically: the Hub default is on or a system
// overrides it to on.
func (s *Service) autoUpdateWanted(ctx context.Context) bool {
	if on, err := s.CoreAutoUpdate(ctx); err == nil && on {
		return true
	}
	var n int
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM systems WHERE core_auto_update = 'on'`).Scan(&n); err == nil && n > 0 {
		return true
	}
	return false
}

// autoUpdateCores updates every installed core that has a newer catalog build and whose effective automatic
// update (Hub default, system override, per-core pin) is on, with the UpdateCore safeguards.
// One failing core does not stop the others; failures are recorded in the settings and returned in rep.
func (s *Service) autoUpdateCores(ctx context.Context, rep *CoreSyncReport) {
	entries, err := s.ListRegistry(ctx)
	if err != nil {
		rep.UpdateErrors = append(rep.UpdateErrors, "list systems: "+err.Error())
	}
	for _, e := range entries {
		sc, err := s.SystemCores(ctx, e.ID)
		if err != nil {
			rep.UpdateErrors = append(rep.UpdateErrors, e.ID+": "+err.Error())
			continue
		}
		for _, c := range sc.Installed {
			if !c.UpdateAvailable || !c.AutoUpdateEffective || ctx.Err() != nil {
				continue
			}
			ic, err := s.UpdateCore(ctx, e.ID, c.CoreID)
			switch {
			case err == nil:
				rep.Updated = append(rep.Updated, CoreUpdated{SystemID: e.ID, CoreID: c.CoreID, Version: ic.Version})
			case errors.Is(err, errUnchangedBuild):
			default:
				rep.UpdateErrors = append(rep.UpdateErrors, e.ID+"/"+c.CoreID+": "+err.Error())
			}
		}
	}
	msg := ""
	if len(rep.UpdateErrors) > 0 {
		msg = cleanText(strings.Join(rep.UpdateErrors, "; "), 500)
	}
	s.setCoreState(ctx, settingCoreAutoUpdateLastError, msg)
}

// RefreshCatalog fetches info.zip and the .index-extended of every served platform and replaces the catalog.
// Any failure keeps the previous catalog and is recorded for the Systems & Cores page. Safe to call concurrently.
func (s *Service) RefreshCatalog(ctx context.Context) (_ CoreSyncReport, err error) {
	defer s.publishOK(&err, TopicSystems)
	s.cores.refreshMu.Lock()
	defer s.cores.refreshMu.Unlock()
	var rep CoreSyncReport
	s.setCoreState(ctx, settingCoreLastCheck, strconv.FormatInt(s.now().Unix(), 10))
	fail := func(err error) (CoreSyncReport, error) {
		s.setCoreState(ctx, settingCoreLastError, cleanText(err.Error(), 500))
		return rep, err
	}
	zdata, err := s.fetchCore(ctx, s.cores.infoURL, maxInfoZipBytes)
	if err != nil {
		return fail(fmt.Errorf("fetch core info: %w", err))
	}
	infos, err := parseInfoZip(zdata)
	if err != nil {
		return fail(err)
	}
	cat := catalog{FetchedAt: s.now().Unix(), Cores: map[string]catalogCore{}}
	for _, p := range corePlatforms {
		idx, err := s.fetchCore(ctx, s.platformIndexURL(p), maxIndexExtBytes)
		if err != nil {
			return fail(fmt.Errorf("fetch build index for %s: %w", p.ID, err))
		}
		for id, b := range parseIndexExtended(idx, p.Suffix) {
			info, ok := infos[id]
			if !ok {
				continue
			}
			c, ok := cat.Cores[id]
			if !ok {
				c = catalogCore{coreInfo: info, Builds: map[string]catalogBuild{}}
			}
			c.Builds[p.ID] = b
			cat.Cores[id] = c
		}
	}
	if b, err := json.Marshal(cat); err != nil {
		return fail(internal(err))
	} else if err := writeFileAtomic(s.cores.catPath, b); err != nil {
		return fail(internal(err))
	}
	s.cores.catMu.Lock()
	s.cores.cat = cat
	s.cores.catMu.Unlock()
	rep.Cores = len(cat.Cores)
	s.setCoreState(ctx, settingCoreLastSuccess, strconv.FormatInt(s.now().Unix(), 10))
	s.setCoreState(ctx, settingCoreLastError, "")
	return rep, nil
}

func (s *Service) catalogCore(id string) (catalogCore, bool) {
	s.cores.catMu.RLock()
	defer s.cores.catMu.RUnlock()
	c, ok := s.cores.cat.Cores[id]
	return c, ok
}

func (s *Service) catalogCores() []catalogCore {
	s.cores.catMu.RLock()
	defer s.cores.catMu.RUnlock()
	out := make([]catalogCore, 0, len(s.cores.cat.Cores))
	for _, c := range s.cores.cat.Cores {
		out = append(out, c)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// TriggerCoreSync asks the background loop (RunCoreSync) for a refresh now; it never blocks.
func (s *Service) TriggerCoreSync() {
	select {
	case s.cores.kickSync <- struct{}{}:
	default:
	}
}

// RunCoreSync refreshes the catalog at once (in the caller's goroutine, so start it with go), then every `every`
// and on TriggerCoreSync, until ctx ends. report (may be nil) receives each result. It never fails the caller.
func (s *Service) RunCoreSync(ctx context.Context, every time.Duration, report func(CoreSyncReport, error)) {
	t := time.NewTicker(every)
	defer t.Stop()
	do := func() {
		rep, err := s.RefreshCatalog(ctx)
		if err == nil {
			if s.autoUpdateWanted(ctx) {
				rep.AutoUpdateRan = true
				s.autoUpdateCores(ctx, &rep)
			}
		}
		if report != nil && ctx.Err() == nil {
			report(rep, err)
		}
	}
	do()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			do()
		case <-s.cores.kickSync:
			do()
		}
	}
}
