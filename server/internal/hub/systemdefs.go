package hub

import (
	"context"
	"database/sql"
	"embed"
	"encoding/json"
	"fmt"
	"io/fs"
	"regexp"
	"strconv"
	"strings"
)

// Systems are data-driven (ADR 0020 D4): one embedded JSON file per system in systems/. At start the Hub
// reconciles the files with the database (reconcileSystems); admin state is never touched.

//go:embed systems/*.json
var systemDefsFS embed.FS

var (
	systemIDRe  = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{0,63}$`)
	extensionRe = regexp.MustCompile(`^\.[a-z0-9][a-z0-9._-]{0,15}$`)
)

// systemDef is the content of one systems/<id>.json file.
type systemDef struct {
	ID             string        `json:"id"`
	DisplayName    string        `json:"display_name"`
	Extensions     []string      `json:"extensions"`
	LibretroIDs    []string      `json:"libretro_ids"`
	Platforms      []string      `json:"platforms"`
	Provisioning   string        `json:"provisioning"`
	InputProfile   string        `json:"input_profile"`
	DisplayProfile string        `json:"display_profile"`
	Firmware       []firmwareDef `json:"firmware"`
	ProfiledCores  []string      `json:"profiled_cores"`
}

// firmwareDef is a firmware/BIOS file of a system. File (the file name the core expects) is not stored in the
// database; it is checked against the Player system manifest. Order in the list is the display order. Whether a file is required is decided by the firmware mode of the system.
type firmwareDef struct {
	ID          string  `json:"id"`
	DisplayName string  `json:"display_name"`
	File        string  `json:"file"`
	Sizes       []int64 `json:"sizes"`
}

// loadSystemDefs reads and validates all embedded definitions, ordered by id.
func loadSystemDefs() ([]systemDef, error) {
	return loadSystemDefsFS(systemDefsFS, "systems")
}

func loadSystemDefsFS(fsys fs.FS, dir string) ([]systemDef, error) {
	entries, err := fs.ReadDir(fsys, dir)
	if err != nil {
		return nil, err
	}
	var defs []systemDef
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		data, err := fs.ReadFile(fsys, dir+"/"+e.Name())
		if err != nil {
			return nil, err
		}
		var d systemDef
		dec := json.NewDecoder(strings.NewReader(string(data)))
		dec.DisallowUnknownFields()
		if err := dec.Decode(&d); err != nil {
			return nil, fmt.Errorf("system definition %s: %w", e.Name(), err)
		}
		if d.ID+".json" != e.Name() {
			return nil, fmt.Errorf("system definition %s: id %q does not match the file name", e.Name(), d.ID)
		}
		defs = append(defs, d)
	}
	return defs, validateSystemDefs(defs)
}

func validateSystemDefs(defs []systemDef) error {
	ids := map[string]bool{}
	exts := map[string]string{}
	for _, d := range defs {
		if !systemIDRe.MatchString(d.ID) {
			return fmt.Errorf("system %q: invalid id", d.ID)
		}
		if ids[d.ID] {
			return fmt.Errorf("system %q: defined twice", d.ID)
		}
		ids[d.ID] = true
		if strings.TrimSpace(d.DisplayName) == "" || d.InputProfile == "" || d.DisplayProfile == "" || len(d.Platforms) == 0 {
			return fmt.Errorf("system %q: display_name, input_profile, display_profile and platforms are required", d.ID)
		}
		if len(d.Extensions) == 0 {
			return fmt.Errorf("system %q: at least one extension is required", d.ID)
		}
		for _, e := range d.Extensions {
			if !extensionRe.MatchString(e) {
				return fmt.Errorf("system %q: extension %q must be lowercase with a leading dot", d.ID, e)
			}
			if other, ok := exts[e]; ok {
				return fmt.Errorf("extension %q is used by both %q and %q", e, other, d.ID)
			}
			exts[e] = d.ID
		}
		if len(d.LibretroIDs) == 0 {
			return fmt.Errorf("system %q: at least one libretro id is required", d.ID)
		}
		for _, l := range d.LibretroIDs {
			if !systemIDRe.MatchString(l) {
				return fmt.Errorf("system %q: invalid libretro id %q", d.ID, l)
			}
		}
		fw := map[string]bool{}
		for _, f := range d.Firmware {
			if !systemIDRe.MatchString(f.ID) || fw[f.ID] || strings.TrimSpace(f.DisplayName) == "" || len(f.Sizes) == 0 {
				return fmt.Errorf("system %q: invalid firmware entry %q", d.ID, f.ID)
			}
			fw[f.ID] = true
			for _, sz := range f.Sizes {
				if sz <= 0 {
					return fmt.Errorf("system %q: firmware %q: invalid size", d.ID, f.ID)
				}
			}
		}
		for _, c := range d.ProfiledCores {
			if !ValidCoreID(c) {
				return fmt.Errorf("system %q: invalid profiled core id %q", d.ID, c)
			}
		}
	}
	return nil
}

func joinInts(v []int64) string {
	s := make([]string, len(v))
	for i, n := range v {
		s[i] = strconv.FormatInt(n, 10)
	}
	return strings.Join(s, ",")
}

// reconcileSystems brings the systems, firmware_defs and profiled_cores tables in line with the definition
// files, in one transaction. It inserts missing systems (firmware_mode builtin, no core) and updates only the
// definition columns of existing ones; admin state (default core, firmware mode, installed cores, uploaded
// firmware, pins, preferred core columns) is never touched. Systems without a file stay as they are, and
// firmware_defs rows are never deleted.
func reconcileSystems(ctx context.Context, db *sql.DB) error {
	defs, err := loadSystemDefs()
	if err != nil {
		return err
	}
	tx, err := db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, `DELETE FROM profiled_cores`); err != nil {
		return err
	}
	for _, d := range defs {
		j := func(v []string) string { return strings.Join(v, ",") }
		if _, err := tx.ExecContext(ctx, `INSERT INTO systems(id, display_name, extensions, preferred_core_id, preferred_core_name,
				expected_core_version, platforms, provisioning, input_profile, display_profile, firmware_mode, libretro_ids)
			VALUES (?, ?, ?, '', '', NULL, ?, ?, ?, ?, 'builtin', ?)
			ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, extensions = excluded.extensions,
				platforms = excluded.platforms, provisioning = excluded.provisioning, input_profile = excluded.input_profile,
				display_profile = excluded.display_profile, libretro_ids = excluded.libretro_ids`,
			d.ID, d.DisplayName, j(d.Extensions), j(d.Platforms), d.Provisioning, d.InputProfile, d.DisplayProfile,
			j(d.LibretroIDs)); err != nil {
			return err
		}
		for i, f := range d.Firmware {
			if _, err := tx.ExecContext(ctx, `INSERT INTO firmware_defs(system_id, file_id, display_name, sizes, sort)
				VALUES (?, ?, ?, ?, ?)
				ON CONFLICT(system_id, file_id) DO UPDATE SET display_name = excluded.display_name,
					sizes = excluded.sizes, sort = excluded.sort`,
				d.ID, f.ID, f.DisplayName, joinInts(f.Sizes), i+1); err != nil {
				return err
			}
		}
		for _, c := range d.ProfiledCores {
			if _, err := tx.ExecContext(ctx, `INSERT OR IGNORE INTO profiled_cores(core_id) VALUES (?)`, c); err != nil {
				return err
			}
		}
	}
	return tx.Commit()
}

// matchesCore reports whether a catalog core is offered for the system: its libretro system id belongs to the
// system and its supported extensions (when it lists any) share one with the system (without dot, any case).
// Installed cores are not re-checked, so a core that stops matching stays installed.
func (e SystemEntry) matchesCore(ci coreInfo) bool {
	if !e.matchesSystemID(ci.SystemID) {
		return false
	}
	if len(ci.Extensions) == 0 {
		return true
	}
	mine := map[string]bool{}
	for _, x := range e.Extensions {
		mine[strings.TrimPrefix(strings.ToLower(x), ".")] = true
	}
	for _, x := range ci.Extensions {
		if mine[strings.TrimPrefix(strings.ToLower(strings.TrimSpace(x)), ".")] {
			return true
		}
	}
	return false
}
