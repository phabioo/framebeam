package hub

import (
	"context"
	"crypto/ed25519"
	"database/sql"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
)

// Handshake features for cores. cores_v2 = several cores per system (ADR 0020); cores_v1 stays for older Players.
const (
	FeatureCoresV1 = "cores_v1"
	FeatureCoresV2 = "cores_v2"
)

// Origins of a core package.
const (
	OriginBuildbot = "libretro-buildbot"
	OriginLegacy   = "framebeam" // packages of the retired signed FrameBeam source
)

// Defaults of the core source (ADR 0020 D1).
const (
	DefaultCoreBuildbotURL = "https://buildbot.libretro.com/nightly"
	DefaultCoreInfoURL     = "https://buildbot.libretro.com/assets/frontend/info.zip"
)

// Source state keys in the settings table.
const (
	settingCoreLastCheck   = "core_source_last_check"
	settingCoreLastSuccess = "core_source_last_success"
	settingCoreLastError   = "core_source_last_error"
)

const (
	coreFetchTimeout = time.Minute
	coreFileTimeout  = 15 * time.Minute

	defaultMaxLibraryBytes = int64(1) << 30   // extracted library of one core
	defaultMaxZipBytes     = int64(512) << 20 // downloaded core zip
)

type coreState struct {
	buildbotURL, infoURL string
	keys                 []ed25519.PublicKey // trusted keys of the updates index
	client               *http.Client
	maxLib, maxZip       int64
	catPath              string

	mu        sync.Mutex   // serializes install, update, remove and import
	refreshMu sync.Mutex   // serializes catalog refreshes
	catMu     sync.RWMutex // guards cat
	cat       catalog
	kickSync  chan struct{}
}

func (c *coreState) init(o Options) {
	c.buildbotURL = strings.TrimRight(o.CoreBuildbotURL, "/")
	if c.buildbotURL == "" {
		c.buildbotURL = DefaultCoreBuildbotURL
	}
	c.infoURL = o.CoreInfoURL
	if c.infoURL == "" {
		c.infoURL = DefaultCoreInfoURL
	}
	c.keys = o.CoreTrustKeys
	c.maxLib, c.maxZip = o.CoreMaxLibraryBytes, o.CoreMaxZipBytes
	if c.maxLib <= 0 {
		c.maxLib = defaultMaxLibraryBytes
	}
	if c.maxZip <= 0 {
		c.maxZip = defaultMaxZipBytes
	}
	hc := o.CoreHTTPClient
	if hc == nil {
		hc = &http.Client{Transport: &http.Transport{Proxy: http.ProxyFromEnvironment, TLSHandshakeTimeout: 15 * time.Second,
			ResponseHeaderTimeout: 30 * time.Second}}
	}
	cp := *hc // never modify the injected client
	cp.CheckRedirect = func(req *http.Request, via []*http.Request) error {
		if len(via) >= 5 {
			return errors.New("too many redirects")
		}
		if req.URL.Scheme != "https" {
			return errors.New("redirect to a non-https URL refused")
		}
		return nil
	}
	c.client = &cp
	c.kickSync = make(chan struct{}, 1)
	c.catPath = filepath.Join(o.DataDir, "core-catalog.json")
	c.cat = loadCatalogFile(c.catPath)
}

func (s *Service) corePath(coreID, version, platform string, rest ...string) string {
	return filepath.Join(append([]string{s.dataDir, "cores", coreID, version, platform}, rest...)...)
}

// ---- Source state ----

// CoreSourceStatus is the state of the core source (libretro buildbot) and its catalog.
type CoreSourceStatus struct {
	BuildbotURL, InfoURL string
	LastCheck            *time.Time
	LastSuccess          *time.Time
	LastError            string
	Cores                int // cores with a build in the cached catalog
}

// CoreSource returns the state of the core source.
func (s *Service) CoreSource(ctx context.Context) (CoreSourceStatus, error) {
	st := CoreSourceStatus{BuildbotURL: s.cores.buildbotURL, InfoURL: s.cores.infoURL}
	for key, dst := range map[string]**time.Time{settingCoreLastCheck: &st.LastCheck, settingCoreLastSuccess: &st.LastSuccess} {
		v, ok, err := s.getSetting(ctx, key)
		if err != nil {
			return st, err
		}
		if n, perr := strconv.ParseInt(v, 10, 64); ok && perr == nil {
			t := time.Unix(n, 0).UTC()
			*dst = &t
		}
	}
	var err error
	if st.LastError, _, err = s.getSetting(ctx, settingCoreLastError); err != nil {
		return st, err
	}
	s.cores.catMu.RLock()
	st.Cores = len(s.cores.cat.Cores)
	s.cores.catMu.RUnlock()
	return st, nil
}

func (s *Service) setCoreState(ctx context.Context, key, value string) {
	_ = s.setSetting(ctx, key, value)
}

// fetchCore downloads a small resource (info.zip, .index-extended) into memory, at most limit bytes.
func (s *Service) fetchCore(ctx context.Context, rawURL string, limit int64) ([]byte, error) {
	if err := checkHTTPS(rawURL); err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(ctx, coreFetchTimeout)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return nil, err
	}
	resp, err := s.cores.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	b, err := io.ReadAll(io.LimitReader(resp.Body, limit+1))
	if err != nil {
		return nil, err
	}
	if int64(len(b)) > limit {
		return nil, errors.New("response too large")
	}
	return b, nil
}

func checkHTTPS(rawURL string) error {
	u, err := url.Parse(rawURL)
	if err != nil || u.Scheme != "https" || u.Host == "" || u.User != nil {
		return errors.New("the core source URL must be https")
	}
	return nil
}

func writeFileAtomic(path string, data []byte) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), ".tmp-*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name()) // no effect after the rename
	_, err = tmp.Write(data)
	if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Chmod(tmp.Name(), 0o640)
	}
	if err == nil {
		err = os.Rename(tmp.Name(), path)
	}
	return err
}

// ValidCoreID reports whether id is a valid core id (ADR 0020 D3).
func ValidCoreID(id string) bool { return corepkg.ValidCoreID(id) }

// ---- Queries ----

// CorePackageFile is a file of a core package; Available = in the Hub cache.
type CorePackageFile struct {
	Name, Role, SHA256 string
	Size               int64
	Available          bool
}

// CorePackage is a package known to the Hub.
type CorePackage struct {
	CoreID, Version, Platform, License, SourceURL, SourceRef, Origin string
	Files                                                            []CorePackageFile
}

// CachedFiles counts the available files.
func (p CorePackage) CachedFiles() int {
	n := 0
	for _, f := range p.Files {
		if f.Available {
			n++
		}
	}
	return n
}

// TotalSize is the sum of the file sizes.
func (p CorePackage) TotalSize() int64 {
	var n int64
	for _, f := range p.Files {
		n += f.Size
	}
	return n
}

func (s *Service) loadCorePackages(ctx context.Context, where string, args ...any) ([]CorePackage, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT core_id, version, platform, license, source_url, source_ref, origin FROM core_packages `+where, args...)
	if err != nil {
		return nil, internal(err)
	}
	var out []CorePackage
	for rows.Next() {
		var p CorePackage
		if err := rows.Scan(&p.CoreID, &p.Version, &p.Platform, &p.License, &p.SourceURL, &p.SourceRef, &p.Origin); err != nil {
			rows.Close()
			return nil, internal(err)
		}
		out = append(out, p)
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return nil, internal(err)
	}
	for i := range out {
		p := &out[i]
		frows, err := s.db.QueryContext(ctx, `SELECT name, role, size, sha256, cached_at IS NOT NULL FROM core_package_files
			WHERE core_id = ? AND version = ? AND platform = ? ORDER BY role <> 'library', name`, p.CoreID, p.Version, p.Platform) // library first
		if err != nil {
			return nil, internal(err)
		}
		for frows.Next() {
			var f CorePackageFile
			if err := frows.Scan(&f.Name, &f.Role, &f.Size, &f.SHA256, &f.Available); err != nil {
				frows.Close()
				return nil, internal(err)
			}
			if f.Available {
				st, serr := os.Stat(s.corePath(p.CoreID, p.Version, p.Platform, f.Name))
				f.Available = serr == nil && st.Size() == f.Size
			}
			p.Files = append(p.Files, f)
		}
		frows.Close()
		if err := frows.Err(); err != nil {
			return nil, internal(err)
		}
	}
	return out, nil
}

// GetCorePackage returns one package (ErrCorePackageNotFound if the Hub does not know it).
func (s *Service) GetCorePackage(ctx context.Context, coreID, version, platform string) (CorePackage, error) {
	if !corepkg.ValidCoreID(coreID) || !corepkg.ValidVersion(version) || !corepkg.PlatformValid(platform) {
		return CorePackage{}, ErrCorePackageNotFound
	}
	ps, err := s.loadCorePackages(ctx, `WHERE core_id = ? AND version = ? AND platform = ?`, coreID, version, platform)
	if err != nil {
		return CorePackage{}, err
	}
	if len(ps) == 0 {
		return CorePackage{}, ErrCorePackageNotFound
	}
	return ps[0], nil
}

// OpenCoreFile opens a cached core file for download; the caller closes it. ErrCoreFileNotAvailable if the file
// is unknown or not (completely) cached.
func (s *Service) OpenCoreFile(ctx context.Context, coreID, version, platform, name string) (*os.File, CorePackageFile, error) {
	if !corepkg.ValidCoreID(coreID) || !corepkg.ValidVersion(version) || !corepkg.PlatformValid(platform) || !corepkg.ValidFileName(name) {
		return nil, CorePackageFile{}, ErrCoreFileNotAvailable
	}
	var f CorePackageFile
	var cached bool
	err := s.db.QueryRowContext(ctx, `SELECT name, role, size, sha256, cached_at IS NOT NULL FROM core_package_files
		WHERE core_id = ? AND version = ? AND platform = ? AND name = ?`, coreID, version, platform, name).
		Scan(&f.Name, &f.Role, &f.Size, &f.SHA256, &cached)
	if errors.Is(err, sql.ErrNoRows) || (err == nil && !cached) {
		return nil, CorePackageFile{}, ErrCoreFileNotAvailable
	}
	if err != nil {
		return nil, CorePackageFile{}, internal(err)
	}
	fh, err := os.Open(s.corePath(coreID, version, platform, name))
	if errors.Is(err, os.ErrNotExist) {
		return nil, CorePackageFile{}, ErrCoreFileNotAvailable
	}
	if err != nil {
		return nil, CorePackageFile{}, internal(err)
	}
	if st, err := fh.Stat(); err != nil || st.Size() != f.Size {
		fh.Close()
		return nil, CorePackageFile{}, ErrCoreFileNotAvailable
	}
	f.Available = true
	return fh, f, nil
}
