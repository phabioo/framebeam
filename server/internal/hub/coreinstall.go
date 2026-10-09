package hub

import (
	"archive/zip"
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// maxZipEntries bounds the entries of a core zip (zip bomb guard; a buildbot core zip holds one file).
const maxZipEntries = 64

// errUnchangedBuild: the staged build equals the installed one (import of the same zips again).
var errUnchangedBuild = errors.New("this build is already installed")

// legacyAliases maps legacy core ids to the libretro core id they stand for (ADR 0020 D3).
var legacyAliases = map[string]string{"melonds_ds": "melondsds"}

// InstalledCore is a core installed for a system.
type InstalledCore struct {
	SystemID, CoreID, DisplayName, Version, License, Origin, RequiredHWAPI string
	BuildDate                                                              string // YYYY-MM-DD, empty for legacy packages
	CRC32                                                                  string
	InstalledAt                                                            time.Time
	Default, Experimental                                                  bool
	UpdateAvailable                                                        bool
	UpdateDate                                                             string // upstream date when UpdateAvailable
	Platforms                                                              []string
	Notes                                                                  []string
}

// AvailableCore is a core of the buildbot catalog that fits a system and is not installed.
type AvailableCore struct {
	CoreID, DisplayName, License, RequiredHWAPI, BuildDate string
	Extensions, Notes, Platforms                           []string
	Experimental, NonCommercial                            bool
}

// SystemCores is the installed and the available cores of a system.
type SystemCores struct {
	Installed []InstalledCore
	Available []AvailableCore
}

func isNonCommercial(license string) bool {
	l := strings.ToLower(license)
	return strings.Contains(l, "non-commercial") || strings.Contains(l, "noncommercial") || strings.Contains(l, "non commercial")
}

func (s *Service) profiledSet(ctx context.Context) (map[string]bool, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT core_id FROM profiled_cores`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	out := map[string]bool{}
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			return nil, internal(err)
		}
		out[id] = true
	}
	return out, rows.Err()
}

func isProfiled(set map[string]bool, coreID string) bool {
	if a, ok := legacyAliases[coreID]; ok {
		coreID = a
	}
	return set[coreID]
}

// loadInstalledCores returns the installed cores of all systems (or of one system when systemID is set).
func (s *Service) loadInstalledCores(ctx context.Context, systemID string) (map[string][]InstalledCore, error) {
	profiled, err := s.profiledSet(ctx)
	if err != nil {
		return nil, err
	}
	plat := map[[2]string][]string{}
	prows, err := s.db.QueryContext(ctx, `SELECT core_id, version, platform FROM core_packages ORDER BY platform`)
	if err != nil {
		return nil, internal(err)
	}
	for prows.Next() {
		var c, v, p string
		if err := prows.Scan(&c, &v, &p); err != nil {
			prows.Close()
			return nil, internal(err)
		}
		plat[[2]string{c, v}] = append(plat[[2]string{c, v}], p)
	}
	prows.Close()
	if err := prows.Err(); err != nil {
		return nil, internal(err)
	}
	rows, err := s.db.QueryContext(ctx, `SELECT c.system_id, c.core_id, c.version, c.installed_at, c.origin, c.display_name, c.license,
		c.required_hw_api, c.build_date, c.crc32, c.info, COALESCE(s.default_core_id,'') = c.core_id
		FROM system_cores c JOIN systems s ON s.id = c.system_id WHERE (? = '' OR c.system_id = ?) ORDER BY c.system_id, c.core_id`, systemID, systemID)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	out := map[string][]InstalledCore{}
	for rows.Next() {
		var c InstalledCore
		var at int64
		var info string
		if err := rows.Scan(&c.SystemID, &c.CoreID, &c.Version, &at, &c.Origin, &c.DisplayName, &c.License, &c.RequiredHWAPI,
			&c.BuildDate, &c.CRC32, &info, &c.Default); err != nil {
			return nil, internal(err)
		}
		c.InstalledAt = time.Unix(at, 0).UTC()
		c.Experimental = !isProfiled(profiled, c.CoreID)
		c.Platforms = plat[[2]string{c.CoreID, c.Version}]
		var ci coreInfo
		if json.Unmarshal([]byte(info), &ci) == nil {
			c.Notes = ci.Notes
		}
		if cc, ok := s.catalogCore(c.CoreID); ok && c.Origin == OriginBuildbot && c.BuildDate != "" && cc.newestDate() > c.BuildDate {
			c.UpdateAvailable, c.UpdateDate = true, cc.newestDate()
		}
		out[c.SystemID] = append(out[c.SystemID], c)
	}
	return out, rows.Err()
}

func (e SystemEntry) matchesSystemID(id string) bool {
	id = strings.ToLower(strings.TrimSpace(id))
	for _, l := range e.LibretroIDs {
		if strings.ToLower(l) == id {
			return id != ""
		}
	}
	return false
}

// SystemCores returns the installed cores of a system and the catalog cores that could be installed.
func (s *Service) SystemCores(ctx context.Context, systemID string) (SystemCores, error) {
	e, err := s.GetRegistryEntry(ctx, systemID)
	if err != nil {
		return SystemCores{}, err
	}
	profiled, err := s.profiledSet(ctx)
	if err != nil {
		return SystemCores{}, err
	}
	out := SystemCores{Installed: e.Cores}
	have := map[string]bool{}
	for _, c := range e.Cores {
		have[c.CoreID] = true
		if a, ok := legacyAliases[c.CoreID]; ok {
			have[a] = true // the legacy package is the same core
		}
	}
	for _, c := range s.catalogCores() {
		if have[c.ID] || !e.matchesSystemID(c.SystemID) || len(c.Builds) == 0 {
			continue
		}
		a := AvailableCore{CoreID: c.ID, DisplayName: c.DisplayName, License: c.License, RequiredHWAPI: c.RequiredHWAPI,
			BuildDate: c.newestDate(), Extensions: c.Extensions, Notes: c.Notes, Experimental: !isProfiled(profiled, c.ID),
			NonCommercial: isNonCommercial(c.License)}
		for _, p := range corePlatforms {
			if _, ok := c.Builds[p.ID]; ok {
				a.Platforms = append(a.Platforms, p.ID)
			}
		}
		out.Available = append(out.Available, a)
	}
	sort.SliceStable(out.Available, func(i, j int) bool {
		a, b := out.Available[i], out.Available[j]
		if a.Experimental != b.Experimental {
			return !a.Experimental
		}
		return strings.ToLower(a.DisplayName) < strings.ToLower(b.DisplayName)
	})
	return out, nil
}

// ---- Build sources ----

// buildSource is one platform zip to install: from the buildbot or from a local import directory.
type buildSource struct {
	platform corePlatform
	date     string // YYYY-MM-DD
	crc      string // expected CRC32 (8 hex) or "" when unknown
	srcURL   string // recorded as source_url
	open     func(ctx context.Context) (io.ReadCloser, int64, error)
}

func (s *Service) httpSource(p corePlatform, b catalogBuild) buildSource {
	u := s.buildURL(p, b.File)
	return buildSource{platform: p, date: b.Date, crc: b.CRC32, srcURL: u,
		open: func(ctx context.Context) (io.ReadCloser, int64, error) {
			if err := checkHTTPS(u); err != nil {
				return nil, 0, err
			}
			req, err := http.NewRequestWithContext(ctx, http.MethodGet, u, nil)
			if err != nil {
				return nil, 0, err
			}
			resp, err := s.cores.client.Do(req)
			if err != nil {
				return nil, 0, err
			}
			if resp.StatusCode != http.StatusOK {
				resp.Body.Close()
				return nil, 0, fmt.Errorf("HTTP %d", resp.StatusCode)
			}
			return resp.Body, resp.ContentLength, nil
		}}
}

// stagedBuild is a verified, extracted library waiting in the staging directory.
type stagedBuild struct {
	buildSource
	crc, libName, libPath, sha string
	size                       int64
}

// stage downloads (or reads) the zip of src into dir, checks size cap and CRC32 and extracts the one library.
func (s *Service) stage(ctx context.Context, dir string, coreID string, src buildSource) (_ stagedBuild, err error) {
	ctx, cancel := context.WithTimeout(ctx, coreFileTimeout)
	defer cancel()
	sb := stagedBuild{buildSource: src}
	fail := func(format string, a ...any) (stagedBuild, error) {
		return sb, fmt.Errorf("%s: %s", src.platform.ID, fmt.Sprintf(format, a...))
	}
	rc, declared, err := src.open(ctx)
	if err != nil {
		return fail("download failed: %v", err)
	}
	defer rc.Close()
	if declared > s.cores.maxZip {
		return fail("the zip is larger than the limit of %d bytes", s.cores.maxZip)
	}
	zf, err := os.CreateTemp(dir, "zip-*")
	if err != nil {
		return sb, internal(err)
	}
	defer os.Remove(zf.Name())
	n, err := io.Copy(zf, io.LimitReader(rc, s.cores.maxZip+1))
	if cerr := zf.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return fail("download failed: %v", err)
	}
	if n > s.cores.maxZip {
		return fail("the zip is larger than the limit of %d bytes", s.cores.maxZip)
	}
	sb.libName = coreID + "_libretro" + src.platform.Suffix
	sb.libPath = filepath.Join(dir, src.platform.ID+"-"+sb.libName)
	sb.size, sb.sha, sb.crc, err = extractLibrary(zf.Name(), sb.libName, src.platform.Suffix, sb.libPath, s.cores.maxLib)
	if err != nil {
		return fail("%v", err)
	}
	// The index CRC32 is the one of the uncompressed library (like RetroArch's core updater), not of the zip.
	if src.crc != "" && !strings.EqualFold(src.crc, sb.crc) {
		os.Remove(sb.libPath)
		return fail("CRC32 mismatch (expected %s, got %s)", strings.ToLower(src.crc), sb.crc)
	}
	return sb, nil
}

// extractLibrary writes the single library `want` of the zip to dst and returns its size and SHA-256. Entries with
// path components, a missing or second library, a symlink or an oversize library are rejected.
func extractLibrary(zipPath, want, suffix, dst string, maxBytes int64) (size int64, sha, crc string, err error) {
	zr, err := zip.OpenReader(zipPath)
	if err != nil {
		return 0, "", "", fmt.Errorf("not a valid zip: %w", err)
	}
	defer zr.Close()
	if len(zr.File) > maxZipEntries {
		return 0, "", "", errors.New("the zip has too many entries")
	}
	var lib *zip.File
	for _, f := range zr.File {
		n := f.Name
		if n == "" || n == "." || n == ".." || strings.ContainsAny(n, "/\\\x00") || strings.Contains(n, "..") {
			return 0, "", "", fmt.Errorf("the zip contains an unsafe path %q", n)
		}
		if f.Mode()&os.ModeSymlink != 0 {
			return 0, "", "", fmt.Errorf("the zip contains a link %q", n)
		}
		if strings.HasSuffix(n, "_libretro"+suffix) {
			if lib != nil {
				return 0, "", "", errors.New("the zip contains more than one library")
			}
			lib = f
		}
	}
	if lib == nil {
		return 0, "", "", errors.New("the zip contains no library")
	}
	if lib.Name != want {
		return 0, "", "", fmt.Errorf("the zip holds %q, expected %q", lib.Name, want)
	}
	if lib.UncompressedSize64 > uint64(maxBytes) {
		return 0, "", "", fmt.Errorf("the library is larger than the limit of %d bytes", maxBytes)
	}
	rc, err := lib.Open()
	if err != nil {
		return 0, "", "", err
	}
	defer rc.Close()
	out, err := os.OpenFile(dst, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o640)
	if err != nil {
		return 0, "", "", internal(err)
	}
	h, ch := sha256.New(), crc32.NewIEEE()
	n, err := io.Copy(io.MultiWriter(out, h, ch), io.LimitReader(rc, maxBytes+1))
	if cerr := out.Close(); err == nil {
		err = cerr
	}
	if err == nil && n > maxBytes {
		err = fmt.Errorf("the library is larger than the limit of %d bytes", maxBytes)
	}
	if err == nil && n == 0 {
		err = errors.New("the library is empty")
	}
	if err != nil {
		os.Remove(dst)
		return 0, "", "", err
	}
	return n, hex.EncodeToString(h.Sum(nil)), fmt.Sprintf("%08x", ch.Sum32()), nil
}

func (s *Service) packageVersionExists(ctx context.Context, coreID, version string) (bool, error) {
	var n int
	err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM core_packages WHERE core_id = ? AND version = ?`, coreID, version).Scan(&n)
	return n > 0, internal2(err)
}

// installCore stages every source, then stores the packages, records the installed core and (on replace) drops
// the old build only once the new one is fully cached. s.cores.mu must be held.
func (s *Service) installCore(ctx context.Context, e SystemEntry, info coreInfo, srcs []buildSource, replace bool) (InstalledCore, error) {
	var zero InstalledCore
	if len(srcs) == 0 {
		return zero, badRequest("No build of %s is available for the platforms of this Hub", info.DisplayName)
	}
	if err := os.MkdirAll(filepath.Join(s.dataDir, "tmp"), 0o750); err != nil {
		return zero, internal(err)
	}
	stageDir, err := os.MkdirTemp(filepath.Join(s.dataDir, "tmp"), "core-*")
	if err != nil {
		return zero, internal(err)
	}
	defer os.RemoveAll(stageDir)
	staged := make([]stagedBuild, 0, len(srcs))
	newest := ""
	for _, src := range srcs {
		sb, err := s.stage(ctx, stageDir, info.ID, src)
		if err != nil {
			var he *Error
			if errors.As(err, &he) || strings.HasPrefix(err.Error(), "hub:") {
				return zero, err
			}
			return zero, badRequest("%s could not be installed: %v", info.DisplayName, err)
		}
		staged = append(staged, sb)
		if src.date > newest {
			newest = src.date
		}
	}
	version := strings.ReplaceAll(newest, "-", ".")
	for n := 2; ; n++ {
		exists, err := s.packageVersionExists(ctx, info.ID, version)
		if err != nil {
			return zero, err
		}
		if !exists {
			break
		}
		version = fmt.Sprintf("%s.%d", strings.ReplaceAll(newest, "-", "."), n)
	}
	crcs := make([]string, 0, len(staged))
	for _, sb := range staged {
		crcs = append(crcs, sb.platform.ID+":"+sb.crc)
	}
	crcText := strings.Join(crcs, " ")
	var oldVersion, oldDate, oldCRC string
	err = s.db.QueryRowContext(ctx, `SELECT version, build_date, crc32 FROM system_cores WHERE system_id = ? AND core_id = ?`, e.ID, info.ID).
		Scan(&oldVersion, &oldDate, &oldCRC)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return zero, internal(err)
	}
	if replace && oldVersion != "" && oldDate == newest && oldCRC == crcText {
		return zero, errUnchangedBuild
	}
	// Move the files into the cache first; a failure leaves nothing behind.
	var placed []string
	cleanup := func() {
		for _, d := range placed {
			os.RemoveAll(d)
		}
		os.Remove(filepath.Join(s.dataDir, "cores", info.ID, version))
		os.Remove(filepath.Join(s.dataDir, "cores", info.ID))
	}
	for _, sb := range staged {
		dir := s.corePath(info.ID, version, sb.platform.ID)
		if err := os.MkdirAll(dir, 0o750); err != nil {
			cleanup()
			return zero, internal(err)
		}
		placed = append(placed, dir)
		if err := os.Rename(sb.libPath, filepath.Join(dir, sb.libName)); err != nil {
			cleanup()
			return zero, internal(err)
		}
	}
	now := s.now().Unix()
	infoJSON, _ := json.Marshal(info)
	err = func() error {
		tx, err := s.db.BeginTx(ctx, nil)
		if err != nil {
			return err
		}
		defer tx.Rollback()
		for _, sb := range staged {
			if _, err := tx.ExecContext(ctx, `INSERT INTO core_packages(core_id, version, platform, license, source_url, source_ref, origin, synced_at)
				VALUES (?,?,?,?,?,?,?,?)`, info.ID, version, sb.platform.ID, info.License, sb.srcURL, sb.date+" "+sb.crc, OriginBuildbot, now); err != nil {
				return err
			}
			if _, err := tx.ExecContext(ctx, `INSERT INTO core_package_files(core_id, version, platform, name, role, size, sha256, url, cached_at)
				VALUES (?,?,?,?, 'library', ?,?,?,?)`, info.ID, version, sb.platform.ID, sb.libName, sb.size, sb.sha, sb.srcURL, now); err != nil {
				return err
			}
		}
		if _, err := tx.ExecContext(ctx, `INSERT INTO system_cores(system_id, core_id, version, installed_at, origin, display_name, license,
			required_hw_api, build_date, crc32, info) VALUES (?,?,?,?,?,?,?,?,?,?,?)
			ON CONFLICT(system_id, core_id) DO UPDATE SET version = excluded.version, installed_at = excluded.installed_at,
			origin = excluded.origin, display_name = excluded.display_name, license = excluded.license,
			required_hw_api = excluded.required_hw_api, build_date = excluded.build_date, crc32 = excluded.crc32, info = excluded.info`,
			e.ID, info.ID, version, now, OriginBuildbot, info.DisplayName, info.License, info.RequiredHWAPI, newest, crcText, string(infoJSON)); err != nil {
			return err
		}
		if _, err := tx.ExecContext(ctx, `UPDATE systems SET default_core_id = ? WHERE id = ? AND (default_core_id IS NULL OR default_core_id = '')`, info.ID, e.ID); err != nil {
			return err
		}
		if oldVersion != "" && oldVersion != version {
			if _, err := tx.ExecContext(ctx, `DELETE FROM core_packages WHERE core_id = ? AND version = ?`, info.ID, oldVersion); err != nil {
				return err
			}
		}
		return tx.Commit()
	}()
	if err != nil {
		cleanup()
		return zero, internal(err)
	}
	if oldVersion != "" && oldVersion != version {
		os.RemoveAll(filepath.Join(s.dataDir, "cores", info.ID, oldVersion))
	}
	m, err := s.loadInstalledCores(ctx, e.ID)
	if err != nil {
		return zero, err
	}
	for _, c := range m[e.ID] {
		if c.CoreID == info.ID {
			return c, nil
		}
	}
	return zero, ErrNotFound
}

func (s *Service) coreForInstall(ctx context.Context, systemID, coreID string) (SystemEntry, catalogCore, error) {
	if !ValidCoreID(coreID) {
		return SystemEntry{}, catalogCore{}, badRequest("Invalid core id")
	}
	e, err := s.GetRegistryEntry(ctx, systemID)
	if err != nil {
		return e, catalogCore{}, err
	}
	c, ok := s.catalogCore(coreID)
	if !ok {
		return e, c, badRequest("The core %s is not in the buildbot catalog. Check the source first", coreID)
	}
	if !e.matchesSystemID(c.SystemID) {
		return e, c, badRequest("The core %s is not a core for %s", coreID, e.Name)
	}
	return e, c, nil
}

func (s *Service) catalogSources(c catalogCore) []buildSource {
	var srcs []buildSource
	for _, p := range corePlatforms {
		if b, ok := c.Builds[p.ID]; ok {
			srcs = append(srcs, s.httpSource(p, b))
		}
	}
	return srcs
}

func (e SystemEntry) installed(coreID string) (InstalledCore, bool) {
	for _, c := range e.Cores {
		if c.CoreID == coreID {
			return c, true
		}
	}
	return InstalledCore{}, false
}

// InstallCore downloads the core from the buildbot for every served platform and installs it for the system. The
// first core installed becomes the default.
func (s *Service) InstallCore(ctx context.Context, systemID, coreID string) (_ InstalledCore, err error) {
	defer s.publishOK(&err, TopicSystems)
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	e, c, err := s.coreForInstall(ctx, systemID, coreID)
	if err != nil {
		return InstalledCore{}, err
	}
	if _, ok := e.installed(coreID); ok {
		return InstalledCore{}, badRequest("%s is already installed. Use Update", c.DisplayName)
	}
	return s.installCore(ctx, e, c.coreInfo, s.catalogSources(c), false)
}

// UpdateCore installs the newest buildbot build of an installed core and removes the old build after the new one
// is fully cached.
func (s *Service) UpdateCore(ctx context.Context, systemID, coreID string) (_ InstalledCore, err error) {
	defer s.publishOK(&err, TopicSystems)
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	e, c, err := s.coreForInstall(ctx, systemID, coreID)
	if err != nil {
		return InstalledCore{}, err
	}
	cur, ok := e.installed(coreID)
	if !ok {
		return InstalledCore{}, ErrNotFound
	}
	if cur.BuildDate != "" && c.newestDate() <= cur.BuildDate {
		return InstalledCore{}, badRequest("%s is up to date (build %s)", c.DisplayName, cur.BuildDate)
	}
	return s.installCore(ctx, e, c.coreInfo, s.catalogSources(c), true)
}

// RemoveCore removes an installed core with its packages and cache files. Removing the default core reassigns
// the default to another installed core (profiled cores first) or clears it.
func (s *Service) RemoveCore(ctx context.Context, systemID, coreID string) (err error) {
	defer s.publishOK(&err, TopicSystems)
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	e, err := s.GetRegistryEntry(ctx, systemID)
	if err != nil {
		return err
	}
	if _, ok := e.installed(coreID); !ok {
		return ErrNotFound
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, `DELETE FROM system_cores WHERE system_id = ? AND core_id = ?`, systemID, coreID); err != nil {
		return internal(err)
	}
	var others int
	if err := tx.QueryRowContext(ctx, `SELECT COUNT(*) FROM system_cores WHERE core_id = ?`, coreID).Scan(&others); err != nil {
		return internal(err)
	}
	if others == 0 {
		if _, err := tx.ExecContext(ctx, `DELETE FROM core_packages WHERE core_id = ?`, coreID); err != nil {
			return internal(err)
		}
	}
	if e.CoreID == coreID {
		next := ""
		for _, c := range e.Cores { // e.Cores is ordered by core id; profiled cores come first
			if c.CoreID == coreID {
				continue
			}
			if next == "" {
				next = c.CoreID
			}
			if !c.Experimental {
				next = c.CoreID
				break
			}
		}
		var v any
		if next != "" {
			v = next
		}
		if _, err := tx.ExecContext(ctx, `UPDATE systems SET default_core_id = ? WHERE id = ?`, v, systemID); err != nil {
			return internal(err)
		}
	}
	if err := tx.Commit(); err != nil {
		return internal(err)
	}
	if others == 0 {
		os.RemoveAll(filepath.Join(s.dataDir, "cores", coreID))
	}
	return nil
}

// SetDefaultCore makes an installed core the default core of a system.
func (s *Service) SetDefaultCore(ctx context.Context, systemID, coreID string) (err error) {
	defer s.publishOK(&err, TopicSystems)
	e, err := s.GetRegistryEntry(ctx, systemID)
	if err != nil {
		return err
	}
	if _, ok := e.installed(coreID); !ok {
		return ErrNotFound
	}
	_, err = s.db.ExecContext(ctx, `UPDATE systems SET default_core_id = ? WHERE id = ?`, coreID, systemID)
	return internal2(err)
}
