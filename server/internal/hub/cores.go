package hub

import (
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
)

// FeatureCoresV1 is announced in the handshake: the Hub serves signed core packages.
const FeatureCoresV1 = "cores_v1"

// Source state keys in the settings table.
const (
	settingCoreLastCheck   = "core_source_last_check"
	settingCoreLastSuccess = "core_source_last_success"
	settingCoreLastError   = "core_source_last_error"
	settingCoreSkipped     = "core_source_skipped"
)

const (
	coreFetchTimeout = time.Minute
	coreFileTimeout  = 15 * time.Minute
)

type coreState struct {
	url      string
	keys     []ed25519.PublicKey
	client   *http.Client
	mu       sync.Mutex // serializes sync, download and import
	kickSync chan struct{}
	kickDL   chan struct{}
}

func (c *coreState) init(o Options) {
	c.url, c.keys = o.CoreIndexURL, o.CoreTrustKeys
	if c.url == "" {
		c.url = corepkg.DefaultIndexURL
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
	c.kickSync, c.kickDL = make(chan struct{}, 1), make(chan struct{}, 1)
}

// CoreIndexURL returns the configured index URL.
func (s *Service) CoreIndexURL() string { return s.cores.url }

func (s *Service) corePath(coreID, version, platform string, rest ...string) string {
	return filepath.Join(append([]string{s.dataDir, "cores", coreID, version, platform}, rest...)...)
}

// ---- Source state ----

// CoreSourceStatus is the state of the trusted core source.
type CoreSourceStatus struct {
	URL         string
	LastCheck   *time.Time
	LastSuccess *time.Time
	LastError   string
	Skipped     int // packages of the last index that failed validation
}

// CoreSource returns the state of the core source.
func (s *Service) CoreSource(ctx context.Context) (CoreSourceStatus, error) {
	st := CoreSourceStatus{URL: s.cores.url}
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
	v, _, err := s.getSetting(ctx, settingCoreSkipped)
	st.Skipped, _ = strconv.Atoi(v)
	return st, err
}

func (s *Service) setCoreState(ctx context.Context, key, value string) {
	_ = s.setSetting(ctx, key, value)
}

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

// ListCorePackages returns all known packages ordered by core, version (newest first) and platform.
func (s *Service) ListCorePackages(ctx context.Context) ([]CorePackage, error) {
	ps, err := s.loadCorePackages(ctx, ``)
	if err != nil {
		return nil, err
	}
	sort.SliceStable(ps, func(i, j int) bool {
		a, b := ps[i], ps[j]
		if a.CoreID != b.CoreID {
			return a.CoreID < b.CoreID
		}
		if a.Version != b.Version {
			return corepkg.CompareVersions(a.Version, b.Version) > 0
		}
		return a.Platform < b.Platform
	})
	return ps, nil
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

// CoreVersions lists the versions of a core known to the Hub, newest first.
func (s *Service) CoreVersions(ctx context.Context, coreID string) ([]string, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT DISTINCT version FROM core_packages WHERE core_id = ?`, coreID)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	var out []string
	for rows.Next() {
		var v string
		if err := rows.Scan(&v); err != nil {
			return nil, internal(err)
		}
		out = append(out, v)
	}
	sort.Slice(out, func(i, j int) bool { return corepkg.CompareVersions(out[i], out[j]) > 0 })
	return out, rows.Err()
}

// CoreServedVersion is the version of a core the Hub serves: expected if set, else the highest version with a
// completely cached package; "" when there is none.
func (s *Service) CoreServedVersion(ctx context.Context, coreID, expected string) (string, error) {
	if expected != "" {
		return expected, nil
	}
	rows, err := s.db.QueryContext(ctx, `SELECT DISTINCT p.version FROM core_packages p WHERE p.core_id = ? AND NOT EXISTS (
		SELECT 1 FROM core_package_files f WHERE f.core_id = p.core_id AND f.version = p.version AND f.platform = p.platform
		AND f.cached_at IS NULL)`, coreID)
	if err != nil {
		return "", internal(err)
	}
	defer rows.Close()
	best := ""
	for rows.Next() {
		var v string
		if err := rows.Scan(&v); err != nil {
			return "", internal(err)
		}
		if best == "" || corepkg.CompareVersions(v, best) > 0 {
			best = v
		}
	}
	return best, rows.Err()
}

// ---- Applying an index ----

// CoreSyncReport summarizes one sync or import.
type CoreSyncReport struct {
	Packages   int // packages of the index
	Skipped    int // invalid packages that were skipped
	Downloaded int // files downloaded or copied
	Problems   []string
}

type pkgKey struct{ core, version, platform string }

// applyIndex replaces the package rows with the index. Rows no longer listed are dropped unless their version is
// served to Players (selected by a system); changed files lose their cached copy.
func (s *Service) applyIndex(ctx context.Context, idx corepkg.Index) error {
	reg, err := s.ListRegistry(ctx)
	if err != nil {
		return err
	}
	protected := map[[2]string]bool{}
	for _, e := range reg {
		v, err := s.CoreServedVersion(ctx, e.CoreID, e.ExpectedCoreVersion)
		if err != nil {
			return err
		}
		if v != "" {
			protected[[2]string{e.CoreID, v}] = true
		}
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return internal(err)
	}
	defer tx.Rollback()
	listed := map[pkgKey]bool{}
	for _, p := range idx.Packages {
		listed[pkgKey{p.CoreID, p.Version, p.Platform}] = true
	}
	var removeDirs, removeFiles []string
	rows, err := tx.QueryContext(ctx, `SELECT core_id, version, platform FROM core_packages`)
	if err != nil {
		return internal(err)
	}
	var drop []pkgKey
	for rows.Next() {
		var k pkgKey
		if err := rows.Scan(&k.core, &k.version, &k.platform); err != nil {
			rows.Close()
			return internal(err)
		}
		if !listed[k] && !protected[[2]string{k.core, k.version}] {
			drop = append(drop, k)
		}
	}
	rows.Close()
	for _, k := range drop {
		if _, err := tx.ExecContext(ctx, `DELETE FROM core_packages WHERE core_id = ? AND version = ? AND platform = ?`, k.core, k.version, k.platform); err != nil {
			return internal(err)
		}
		removeDirs = append(removeDirs, s.corePath(k.core, k.version, k.platform))
	}
	now := s.now().Unix()
	for _, p := range idx.Packages {
		if _, err := tx.ExecContext(ctx, `INSERT INTO core_packages(core_id, version, platform, license, source_url, source_ref, origin, synced_at)
			VALUES (?,?,?,?,?,?,?,?) ON CONFLICT(core_id, version, platform) DO UPDATE SET license = excluded.license,
			source_url = excluded.source_url, source_ref = excluded.source_ref, origin = excluded.origin, synced_at = excluded.synced_at`,
			p.CoreID, p.Version, p.Platform, p.License, p.SourceURL, p.SourceRef, p.Origin, now); err != nil {
			return internal(err)
		}
		type old struct {
			size   int64
			sha    string
			cached bool
		}
		olds := map[string]old{}
		orows, err := tx.QueryContext(ctx, `SELECT name, size, sha256, cached_at IS NOT NULL FROM core_package_files
			WHERE core_id = ? AND version = ? AND platform = ?`, p.CoreID, p.Version, p.Platform)
		if err != nil {
			return internal(err)
		}
		for orows.Next() {
			var n string
			var o old
			if err := orows.Scan(&n, &o.size, &o.sha, &o.cached); err != nil {
				orows.Close()
				return internal(err)
			}
			olds[n] = o
		}
		orows.Close()
		names := map[string]bool{}
		for _, f := range p.Files {
			names[f.Name] = true
			o, had := olds[f.Name]
			same := had && o.size == f.Size && o.sha == f.SHA256
			if had && !same {
				removeFiles = append(removeFiles, s.corePath(p.CoreID, p.Version, p.Platform, f.Name))
			}
			if _, err := tx.ExecContext(ctx, `INSERT INTO core_package_files(core_id, version, platform, name, role, size, sha256, url, cached_at)
				VALUES (?,?,?,?,?,?,?,?,NULL) ON CONFLICT(core_id, version, platform, name) DO UPDATE SET role = excluded.role,
				size = excluded.size, sha256 = excluded.sha256, url = excluded.url,
				cached_at = CASE WHEN core_package_files.size = excluded.size AND core_package_files.sha256 = excluded.sha256
				THEN core_package_files.cached_at ELSE NULL END`,
				p.CoreID, p.Version, p.Platform, f.Name, f.Role, f.Size, f.SHA256, f.URL); err != nil {
				return internal(err)
			}
		}
		for n := range olds {
			if !names[n] {
				if _, err := tx.ExecContext(ctx, `DELETE FROM core_package_files WHERE core_id = ? AND version = ? AND platform = ? AND name = ?`,
					p.CoreID, p.Version, p.Platform, n); err != nil {
					return internal(err)
				}
				removeFiles = append(removeFiles, s.corePath(p.CoreID, p.Version, p.Platform, n))
			}
		}
	}
	if err := tx.Commit(); err != nil {
		return internal(err)
	}
	for _, d := range removeDirs {
		os.RemoveAll(d)
	}
	for _, f := range removeFiles {
		os.Remove(f)
	}
	return nil
}

// ---- Storing files ----

// storeCoreFile reads at most size bytes from r into a temp file next to the destination, checks size and SHA-256
// and renames it into the cache. Nothing is left behind on failure.
func (s *Service) storeCoreFile(ctx context.Context, k pkgKey, name string, size int64, sha string, r io.Reader) error {
	dir := s.corePath(k.core, k.version, k.platform)
	if err := os.MkdirAll(dir, 0o750); err != nil {
		return internal(err)
	}
	tmp, err := os.CreateTemp(dir, ".tmp-*")
	if err != nil {
		return internal(err)
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // no-op after a successful rename
	h := sha256.New()
	n, err := io.Copy(io.MultiWriter(tmp, h), io.LimitReader(r, size+1))
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return fmt.Errorf("%s: %w", name, err)
	}
	if n != size {
		return fmt.Errorf("%s: size mismatch (expected %d bytes)", name, size)
	}
	if hex.EncodeToString(h.Sum(nil)) != sha {
		return fmt.Errorf("%s: SHA-256 mismatch", name)
	}
	if err := os.Chmod(tmpName, 0o640); err != nil {
		return internal(err)
	}
	if err := os.Rename(tmpName, filepath.Join(dir, name)); err != nil {
		return internal(err)
	}
	if _, err := s.db.ExecContext(ctx, `UPDATE core_package_files SET cached_at = ? WHERE core_id = ? AND version = ? AND platform = ?
		AND name = ? AND sha256 = ?`, s.now().Unix(), k.core, k.version, k.platform, name, sha); err != nil {
		return internal(err)
	}
	return nil
}

type coreFileRow struct {
	pkgKey
	name, sha, url string
	size           int64
	cached         bool
}

func (s *Service) coreFileOnDisk(f coreFileRow) bool {
	st, err := os.Stat(s.corePath(f.core, f.version, f.platform, f.name))
	return err == nil && st.Mode().IsRegular() && st.Size() == f.size
}

func (s *Service) downloadCoreFile(ctx context.Context, f coreFileRow) error {
	ctx, cancel := context.WithTimeout(ctx, coreFileTimeout)
	defer cancel()
	u, err := url.Parse(f.url)
	if err != nil || u.Scheme != "https" {
		return fmt.Errorf("%s: URL must be https", f.name)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, f.url, nil)
	if err != nil {
		return fmt.Errorf("%s: %w", f.name, err)
	}
	resp, err := s.cores.client.Do(req)
	if err != nil {
		return fmt.Errorf("download %s: %w", f.name, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("download %s: HTTP %d", f.name, resp.StatusCode)
	}
	if resp.ContentLength > f.size {
		return fmt.Errorf("download %s: larger than the declared %d bytes", f.name, f.size)
	}
	return s.storeCoreFile(ctx, f.pkgKey, f.name, f.size, f.sha, resp.Body)
}

// downloadSelectedCores downloads every missing file of the packages whose (core, version) is the expected version
// of some system's preferred core, for all platforms present. Failures are collected; the others continue.
func (s *Service) downloadSelectedCores(ctx context.Context) (n int, problems []string, err error) {
	reg, err := s.ListRegistry(ctx)
	if err != nil {
		return 0, nil, err
	}
	// Target per system: the expected version, or (any version) the highest version known for the core.
	targets := map[[2]string]bool{}
	for _, e := range reg {
		v := e.ExpectedCoreVersion
		if v == "" {
			vs, err := s.CoreVersions(ctx, e.CoreID)
			if err != nil {
				return 0, nil, err
			}
			if len(vs) > 0 {
				v = vs[0]
			}
		}
		if v != "" {
			targets[[2]string{e.CoreID, v}] = true
		}
	}
	rows, err := s.db.QueryContext(ctx, `SELECT core_id, version, platform, name, size, sha256, url, cached_at IS NOT NULL
		FROM core_package_files ORDER BY core_id, version, platform, name`)
	if err != nil {
		return 0, nil, internal(err)
	}
	var todo []coreFileRow
	for rows.Next() {
		var f coreFileRow
		if err := rows.Scan(&f.core, &f.version, &f.platform, &f.name, &f.size, &f.sha, &f.url, &f.cached); err != nil {
			rows.Close()
			return 0, nil, internal(err)
		}
		if targets[[2]string{f.core, f.version}] && !(f.cached && s.coreFileOnDisk(f)) {
			todo = append(todo, f)
		}
	}
	rows.Close()
	if err := rows.Err(); err != nil {
		return 0, nil, internal(err)
	}
	for _, f := range todo {
		if ctx.Err() != nil {
			return n, problems, ctx.Err()
		}
		if err := s.downloadCoreFile(ctx, f); err != nil {
			problems = append(problems, err.Error())
			continue
		}
		n++
	}
	return n, problems, nil
}

// ---- Sync ----

func (s *Service) fetchCore(ctx context.Context, rawURL string, limit int64) ([]byte, error) {
	u, err := url.Parse(rawURL)
	if err != nil || u.Scheme != "https" || u.Host == "" {
		return nil, errors.New("the core source URL must be https")
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

// verifyIndex verifies the signature and parses the index. Skipped lists packages that failed validation.
func (s *Service) verifyIndex(data, sig []byte) (idx corepkg.Index, skipped []error, err error) {
	if err := corepkg.Verify(data, sig, s.cores.keys); err != nil {
		if errors.Is(err, corepkg.ErrNoTrustedKey) {
			return idx, nil, err
		}
		return idx, nil, fmt.Errorf("index signature rejected: %w", err)
	}
	idx, errs := corepkg.ParseIndex(data)
	if corepkg.Fatal(errs) {
		return idx, nil, fmt.Errorf("index rejected: %w", errs[0])
	}
	return idx, errs, nil
}

// SyncCores fetches the signed index, verifies it, records the packages and downloads the selected versions.
// Failures are recorded for the Systems & Cores page; the cache stays usable. Safe to call concurrently
// (calls are serialized).
func (s *Service) SyncCores(ctx context.Context) (CoreSyncReport, error) {
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	var rep CoreSyncReport
	s.setCoreState(ctx, settingCoreLastCheck, strconv.FormatInt(s.now().Unix(), 10))
	fail := func(err error) (CoreSyncReport, error) {
		s.setCoreState(ctx, settingCoreLastError, cleanText(err.Error(), 500))
		return rep, err
	}
	if len(s.cores.keys) == 0 {
		return fail(corepkg.ErrNoTrustedKey)
	}
	data, err := s.fetchCore(ctx, s.cores.url, corepkg.MaxIndexBytes)
	if err != nil {
		return fail(fmt.Errorf("fetch index: %w", err))
	}
	sig, err := s.fetchCore(ctx, s.cores.url+".sig", corepkg.MaxSigBytes)
	if err != nil {
		return fail(fmt.Errorf("fetch index signature: %w", err))
	}
	idx, skipped, err := s.verifyIndex(data, sig)
	if err != nil {
		return fail(err)
	}
	if err := s.applyIndex(ctx, idx); err != nil {
		return fail(err)
	}
	rep.Packages, rep.Skipped = len(idx.Packages), len(skipped)
	for _, e := range skipped {
		rep.Problems = append(rep.Problems, "skipped: "+e.Error())
	}
	s.setCoreState(ctx, settingCoreSkipped, strconv.Itoa(rep.Skipped))
	s.setCoreState(ctx, settingCoreLastSuccess, strconv.FormatInt(s.now().Unix(), 10))
	n, problems, err := s.downloadSelectedCores(ctx)
	rep.Downloaded = n
	if err != nil {
		return fail(err)
	}
	rep.Problems = append(rep.Problems, problems...)
	if len(problems) > 0 {
		return fail(fmt.Errorf("%d file(s) could not be downloaded: %s", len(problems), strings.Join(firstN(problems, 2), "; ")))
	}
	s.setCoreState(ctx, settingCoreLastError, "")
	return rep, nil
}

func firstN(s []string, n int) []string {
	if len(s) > n {
		return s[:n]
	}
	return s
}

// DownloadCores downloads the missing files of the selected versions without contacting the index source.
func (s *Service) DownloadCores(ctx context.Context) (CoreSyncReport, error) {
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	var rep CoreSyncReport
	n, problems, err := s.downloadSelectedCores(ctx)
	rep.Downloaded, rep.Problems = n, problems
	if err != nil {
		return rep, err
	}
	msg := ""
	if len(problems) > 0 {
		msg = fmt.Sprintf("%d file(s) could not be downloaded: %s", len(problems), strings.Join(firstN(problems, 2), "; "))
		s.setCoreState(ctx, settingCoreLastError, cleanText(msg, 500))
		return rep, errors.New(msg)
	}
	if n > 0 {
		s.setCoreState(ctx, settingCoreLastError, "")
	}
	return rep, nil
}

// TriggerCoreSync asks the background loop (RunCoreSync) for a sync now; it never blocks.
func (s *Service) TriggerCoreSync() {
	select {
	case s.cores.kickSync <- struct{}{}:
	default:
	}
}

// TriggerCoreDownload asks the background loop to download the selected versions; it never blocks.
func (s *Service) TriggerCoreDownload() {
	select {
	case s.cores.kickDL <- struct{}{}:
	default:
	}
}

// RunCoreSync syncs at once (in the caller's goroutine, so start it with go), then every `every`, on
// TriggerCoreSync and on TriggerCoreDownload, until ctx ends. report (may be nil) receives each result.
func (s *Service) RunCoreSync(ctx context.Context, every time.Duration, report func(CoreSyncReport, error)) {
	t := time.NewTicker(every)
	defer t.Stop()
	do := func(f func(context.Context) (CoreSyncReport, error)) {
		rep, err := f(ctx)
		if report != nil && ctx.Err() == nil {
			report(rep, err)
		}
	}
	do(s.SyncCores)
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			do(s.SyncCores)
		case <-s.cores.kickSync:
			do(s.SyncCores)
		case <-s.cores.kickDL:
			do(s.DownloadCores)
		}
	}
}

// ---- Offline import ----

// CoreImportSummary is the result of ImportCores.
type CoreImportSummary struct {
	Packages, Skipped     int
	Copied, AlreadyCached int
	Missing, Rejected     int
	Problems              []string
}

// ImportCores reads <dir>/cores-index.json and its .sig, verifies them like a sync, records the packages and
// copies the files found in dir (matched by the last path segment of their url, else by name) after checking
// size and SHA-256.
func (s *Service) ImportCores(ctx context.Context, dir string) (CoreImportSummary, error) {
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	var sum CoreImportSummary
	if len(s.cores.keys) == 0 {
		return sum, corepkg.ErrNoTrustedKey
	}
	readLimited := func(p string, limit int64) ([]byte, error) {
		f, err := os.Open(p)
		if err != nil {
			return nil, err
		}
		defer f.Close()
		b, err := io.ReadAll(io.LimitReader(f, limit+1))
		if err == nil && int64(len(b)) > limit {
			err = errors.New("file too large")
		}
		return b, err
	}
	data, err := readLimited(filepath.Join(dir, "cores-index.json"), corepkg.MaxIndexBytes)
	if err != nil {
		return sum, fmt.Errorf("read index: %w", err)
	}
	sig, err := readLimited(filepath.Join(dir, "cores-index.json.sig"), corepkg.MaxSigBytes)
	if err != nil {
		return sum, fmt.Errorf("read index signature: %w", err)
	}
	idx, skipped, err := s.verifyIndex(data, sig)
	if err != nil {
		return sum, err
	}
	if err := s.applyIndex(ctx, idx); err != nil {
		return sum, err
	}
	sum.Packages, sum.Skipped = len(idx.Packages), len(skipped)
	for _, p := range idx.Packages {
		k := pkgKey{p.CoreID, p.Version, p.Platform}
		for _, f := range p.Files {
			row := coreFileRow{pkgKey: k, name: f.Name, size: f.Size, sha: f.SHA256}
			var cached bool
			_ = s.db.QueryRowContext(ctx, `SELECT cached_at IS NOT NULL FROM core_package_files WHERE core_id = ? AND version = ? AND platform = ? AND name = ?`,
				k.core, k.version, k.platform, f.Name).Scan(&cached)
			if cached && s.coreFileOnDisk(row) {
				sum.AlreadyCached++
				continue
			}
			src := ""
			cands := []string{f.Name}
			if u, err := url.Parse(f.URL); err == nil {
				cands = append([]string{path.Base(u.Path)}, cands...)
			}
			for _, c := range cands {
				if !corepkg.ValidFileName(c) {
					continue
				}
				if st, err := os.Stat(filepath.Join(dir, c)); err == nil && st.Mode().IsRegular() {
					src = filepath.Join(dir, c)
					break
				}
			}
			if src == "" {
				sum.Missing++
				continue
			}
			in, err := os.Open(src)
			if err == nil {
				err = s.storeCoreFile(ctx, k, f.Name, f.Size, f.SHA256, in)
				in.Close()
			}
			if err != nil {
				sum.Rejected++
				sum.Problems = append(sum.Problems, fmt.Sprintf("%s %s %s: %v", k.core, k.version, k.platform, err))
				continue
			}
			sum.Copied++
		}
	}
	return sum, nil
}
