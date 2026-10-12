package hub

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// CoreImportSummary is the result of ImportCores.
type CoreImportSummary struct {
	Installed, Updated, Unchanged int
	Problems                      []string
}

// ImportCores installs buildbot zips from a directory (offline Hub, ADR 0020 D8):
// <dir>/<platform>/<core>_libretro.<suffix>.zip, optionally <dir>/info.zip (core metadata, else the cached
// catalog is used) and <dir>/<platform>/.index-extended (CRC32 and build date, else the CRC32 is only recorded and
// the file date is the build date). The same checks as for a download apply. A core goes to every system that
// matches it (matchesCore); the package is cached once and shared.
func (s *Service) ImportCores(ctx context.Context, dir string) (_ CoreImportSummary, err error) {
	defer s.publishOK(&err, TopicSystems)
	s.cores.mu.Lock()
	defer s.cores.mu.Unlock()
	var sum CoreImportSummary
	infos := map[string]coreInfo{}
	if b, rerr := readLimitedFile(filepath.Join(dir, "info.zip"), maxInfoZipBytes); rerr == nil {
		if infos, err = parseInfoZip(b); err != nil {
			return sum, err
		}
	} else if !os.IsNotExist(rerr) {
		return sum, fmt.Errorf("read info.zip: %w", rerr)
	}
	byCore := map[string][]buildSource{}
	for _, p := range corePlatforms {
		pdir := filepath.Join(dir, p.ID)
		ents, rerr := os.ReadDir(pdir)
		if rerr != nil {
			continue
		}
		var idx map[string]catalogBuild
		if b, rerr := readLimitedFile(filepath.Join(pdir, ".index-extended"), maxIndexExtBytes); rerr == nil {
			idx = parseIndexExtended(b, p.Suffix)
		}
		for _, ent := range ents {
			m := zipName.FindStringSubmatch(ent.Name())
			if m == nil || m[2] != p.Suffix {
				continue
			}
			full := filepath.Join(pdir, ent.Name())
			st, serr := os.Lstat(full)
			if serr != nil || !st.Mode().IsRegular() {
				sum.Problems = append(sum.Problems, fmt.Sprintf("%s/%s: not a regular file", p.ID, ent.Name()))
				continue
			}
			date := st.ModTime().UTC().Format("2006-01-02")
			crc := ""
			if b, ok := idx[m[1]]; ok {
				date, crc = b.Date, b.CRC32
			}
			path := full
			byCore[m[1]] = append(byCore[m[1]], buildSource{platform: p, date: date, crc: crc,
				srcURL: "import:" + p.ID + "/" + ent.Name(),
				open: func(context.Context) (io.ReadCloser, int64, error) {
					f, err := os.Open(path)
					if err != nil {
						return nil, 0, err
					}
					st, err := f.Stat()
					if err != nil {
						f.Close()
						return nil, 0, err
					}
					return f, st.Size(), nil
				}})
		}
	}
	ids := make([]string, 0, len(byCore))
	for id := range byCore {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	reg, err := s.ListRegistry(ctx)
	if err != nil {
		return sum, err
	}
	for _, id := range ids {
		info, ok := infos[id]
		if !ok {
			if c, cok := s.catalogCore(id); cok {
				info, ok = c.coreInfo, true
			}
		}
		if !ok {
			sum.Problems = append(sum.Problems, id+": no core info (add info.zip to the directory)")
			continue
		}
		var targets []SystemEntry
		for _, e := range reg {
			if e.matchesCore(info) {
				targets = append(targets, e)
			}
		}
		if len(targets) == 0 {
			sum.Problems = append(sum.Problems, fmt.Sprintf("%s: no supported system for %q", id, info.SystemID))
			continue
		}
		// The first system gets the real install (package and files are cached once); every other matching
		// system is attached to the same installed version.
		primary := targets[0]
		_, installed := primary.installed(id)
		_, ierr := s.installCore(ctx, primary, info, byCore[id], installed)
		switch {
		case errors.Is(ierr, errUnchangedBuild):
			sum.Unchanged++
		case ierr != nil:
			sum.Problems = append(sum.Problems, fmt.Sprintf("%s: %v", id, strings.TrimPrefix(ierr.Error(), "bad_request: ")))
			continue
		case installed:
			sum.Updated++
		default:
			sum.Installed++
		}
		for _, e := range targets[1:] {
			had, err := s.attachCore(ctx, e, primary.ID, id)
			switch {
			case err != nil:
				sum.Problems = append(sum.Problems, fmt.Sprintf("%s: %s: %v", id, e.ID, err))
			case had == attachNew:
				sum.Installed++
			case had == attachMoved:
				sum.Updated++
			default:
				sum.Unchanged++
			}
		}
		// The registry copy is stale after an install; refresh it for the next core.
		if reg, err = s.ListRegistry(ctx); err != nil {
			return sum, err
		}
	}
	return sum, nil
}

func readLimitedFile(p string, limit int64) ([]byte, error) {
	f, err := os.Open(p)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	b, err := io.ReadAll(io.LimitReader(f, limit+1))
	if err == nil && int64(len(b)) > limit {
		err = fmt.Errorf("%s is too large", filepath.Base(p))
	}
	return b, err
}

type attachResult int

const (
	attachSame  attachResult = iota // the system already has this version
	attachNew                       // the system did not have the core
	attachMoved                     // the system had another version and now uses the shared one
)

// attachCore makes the system use the installed version of a core that srcSystem already has (no new package or
// files). The default core is set when the system has none.
func (s *Service) attachCore(ctx context.Context, e SystemEntry, srcSystem, coreID string) (attachResult, error) {
	var cur string
	err := s.db.QueryRowContext(ctx, `SELECT version FROM system_cores WHERE system_id = ? AND core_id = ?`, e.ID, coreID).Scan(&cur)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return 0, internal(err)
	}
	var src string
	if err := s.db.QueryRowContext(ctx, `SELECT version FROM system_cores WHERE system_id = ? AND core_id = ?`, srcSystem, coreID).Scan(&src); err != nil {
		return 0, internal(err)
	}
	if cur == src {
		return attachSame, nil
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return 0, internal(err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, `INSERT INTO system_cores(system_id, core_id, version, installed_at, origin, display_name, license,
		required_hw_api, build_date, crc32, info)
		SELECT ?, core_id, version, installed_at, origin, display_name, license, required_hw_api, build_date, crc32, info
		FROM system_cores WHERE system_id = ? AND core_id = ?
		ON CONFLICT(system_id, core_id) DO UPDATE SET version = excluded.version, installed_at = excluded.installed_at,
		origin = excluded.origin, display_name = excluded.display_name, license = excluded.license,
		required_hw_api = excluded.required_hw_api, build_date = excluded.build_date, crc32 = excluded.crc32, info = excluded.info`,
		e.ID, srcSystem, coreID); err != nil {
		return 0, internal(err)
	}
	if _, err := tx.ExecContext(ctx, `UPDATE systems SET default_core_id = ? WHERE id = ? AND (default_core_id IS NULL OR default_core_id = '')`, coreID, e.ID); err != nil {
		return 0, internal(err)
	}
	stale, err := dropUnreferencedVersions(ctx, tx, coreID)
	if err != nil {
		return 0, internal(err)
	}
	if err := tx.Commit(); err != nil {
		return 0, internal(err)
	}
	for _, v := range stale {
		os.RemoveAll(filepath.Join(s.dataDir, "cores", coreID, v))
	}
	if cur == "" {
		return attachNew, nil
	}
	return attachMoved, nil
}
