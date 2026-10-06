package hub

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// Handshake features announced by the Hub (additive; protocol_version unchanged).
const (
	FeatureUsersV1    = "users_v1"
	FeatureUploadsV1  = "uploads_v1"
	FeatureFirmwareV1 = "firmware_v1"
)

// Problem codes of the core check. Both are warnings: compatible stays true.
const (
	ProblemCoreMissing         Code = "core_missing"
	ProblemCoreVersionMismatch Code = "core_version_mismatch"
)

// FirmwareMode of a system.
type FirmwareMode string

const (
	FirmwareBuiltin FirmwareMode = "builtin"
	FirmwareNative  FirmwareMode = "native"
)

// FirmwareState is the status of a firmware file.
type FirmwareState string

const (
	FirmwareValid    FirmwareState = "valid"
	FirmwareMismatch FirmwareState = "mismatch" // pinned hash differs from the stored file
	FirmwareMissing  FirmwareState = "missing"  // required (mode native) and not provided
	FirmwareOptional FirmwareState = "optional" // not provided, not required
)

// FirmwareFile is a firmware/BIOS file of a system with its current state. SHA256/Size are empty/0 when absent.
type FirmwareFile struct {
	ID          string
	DisplayName string
	Sizes       []int64
	Required    bool
	Present     bool
	Size        int64
	SHA256      string
	// Pinned is the admin-pinned expected SHA-256 (empty = none).
	Pinned string
	State  FirmwareState
}

// SystemEntry is a registry entry: system, preferred core, expected version, provisioning and firmware.
type SystemEntry struct {
	ID                  string
	Name                string
	Extensions          []string
	CoreID              string
	CoreName            string
	ExpectedCoreVersion string // empty = any version
	Platforms           []string
	Provisioning        string
	InputProfile        string
	DisplayProfile      string
	FirmwareMode        FirmwareMode
	Firmware            []FirmwareFile
}

func splitList(v string) []string {
	var out []string
	for _, p := range strings.Split(v, ",") {
		if p = strings.TrimSpace(p); p != "" {
			out = append(out, p)
		}
	}
	return out
}

func (s *Service) firmwarePath(systemID, fileID string) string {
	return filepath.Join(s.dataDir, "firmware", systemID, fileID)
}

// ListRegistry returns all registry entries with their firmware files and states.
func (s *Service) ListRegistry(ctx context.Context) ([]SystemEntry, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT id, display_name, extensions, preferred_core_id, preferred_core_name,
		COALESCE(expected_core_version,''), platforms, provisioning, input_profile, display_profile, firmware_mode FROM systems ORDER BY id`)
	if err != nil {
		return nil, internal(err)
	}
	var out []SystemEntry
	for rows.Next() {
		var e SystemEntry
		var ext, plat string
		if err := rows.Scan(&e.ID, &e.Name, &ext, &e.CoreID, &e.CoreName, &e.ExpectedCoreVersion, &plat, &e.Provisioning,
			&e.InputProfile, &e.DisplayProfile, &e.FirmwareMode); err != nil {
			rows.Close()
			return nil, internal(err)
		}
		e.Extensions, e.Platforms = splitList(ext), splitList(plat)
		out = append(out, e)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return nil, internal(err)
	}
	rows.Close()
	for i := range out {
		files, err := s.systemFirmware(ctx, out[i].ID, out[i].FirmwareMode)
		if err != nil {
			return nil, err
		}
		out[i].Firmware = files
	}
	return out, nil
}

// GetRegistryEntry returns one entry (ErrNotFound if unknown).
func (s *Service) GetRegistryEntry(ctx context.Context, systemID string) (SystemEntry, error) {
	all, err := s.ListRegistry(ctx)
	if err != nil {
		return SystemEntry{}, err
	}
	for _, e := range all {
		if e.ID == systemID {
			return e, nil
		}
	}
	return SystemEntry{}, ErrNotFound
}

func (s *Service) systemFirmware(ctx context.Context, systemID string, mode FirmwareMode) ([]FirmwareFile, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT d.file_id, d.display_name, d.sizes, COALESCE(f.sha256,''), COALESCE(f.size,0), f.sha256 IS NOT NULL,
		COALESCE(p.sha256,'') FROM firmware_defs d
		LEFT JOIN firmware_files f ON f.system_id = d.system_id AND f.file_id = d.file_id
		LEFT JOIN firmware_pins p ON p.system_id = d.system_id AND p.file_id = d.file_id
		WHERE d.system_id = ? ORDER BY d.sort, d.file_id`, systemID)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	var out []FirmwareFile
	for rows.Next() {
		var f FirmwareFile
		var sizes string
		if err := rows.Scan(&f.ID, &f.DisplayName, &sizes, &f.SHA256, &f.Size, &f.Present, &f.Pinned); err != nil {
			return nil, internal(err)
		}
		for _, p := range splitList(sizes) {
			if n, err := strconv.ParseInt(p, 10, 64); err == nil {
				f.Sizes = append(f.Sizes, n)
			}
		}
		f.Required = mode == FirmwareNative
		switch {
		case !f.Present && f.Required:
			f.State = FirmwareMissing
		case !f.Present:
			f.State = FirmwareOptional
		case f.Pinned != "" && f.Pinned != f.SHA256:
			f.State = FirmwareMismatch
		default:
			f.State = FirmwareValid
		}
		out = append(out, f)
	}
	return out, rows.Err()
}

// FirmwareProblems counts required firmware files (mode native) that are missing or mismatching (nav badge).
func (s *Service) FirmwareProblems(ctx context.Context) (int, error) {
	reg, err := s.ListRegistry(ctx)
	if err != nil {
		return 0, err
	}
	n := 0
	for _, e := range reg {
		for _, f := range e.Firmware {
			if f.Required && (f.State == FirmwareMissing || f.State == FirmwareMismatch) {
				n++
			}
		}
	}
	return n, nil
}

// SetExpectedCoreVersion sets the expected core version of a system (empty = any version).
func (s *Service) SetExpectedCoreVersion(ctx context.Context, systemID, version string) error {
	version = cleanText(version, 64)
	var v any
	if version != "" {
		v = version
	}
	res, err := s.db.ExecContext(ctx, `UPDATE systems SET expected_core_version = ? WHERE id = ?`, v, systemID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return nil
}

// SetFirmwareMode switches a system between builtin and native firmware.
func (s *Service) SetFirmwareMode(ctx context.Context, systemID string, mode FirmwareMode) error {
	if mode != FirmwareBuiltin && mode != FirmwareNative {
		return badRequest("Firmware mode must be builtin or native")
	}
	res, err := s.db.ExecContext(ctx, `UPDATE systems SET firmware_mode = ? WHERE id = ?`, string(mode), systemID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Service) firmwareDef(ctx context.Context, systemID, fileID string) (FirmwareFile, error) {
	e, err := s.GetRegistryEntry(ctx, systemID)
	if err != nil {
		return FirmwareFile{}, err
	}
	for _, f := range e.Firmware {
		if f.ID == fileID {
			return f, nil
		}
	}
	return FirmwareFile{}, ErrNotFound
}

// ProvideFirmware stores (or replaces) a firmware file from r. The size must match the file definition and, if
// the admin pinned an expected SHA-256, the content must match it. Nothing is stored on failure. The file is
// kept at <data dir>/firmware/<system>/<file_id> (0600); the bytes are never logged.
func (s *Service) ProvideFirmware(ctx context.Context, systemID, fileID string, r io.Reader) (FirmwareFile, error) {
	def, err := s.firmwareDef(ctx, systemID, fileID)
	if err != nil {
		return FirmwareFile{}, err
	}
	var max int64
	for _, n := range def.Sizes {
		if n > max {
			max = n
		}
	}
	dir := filepath.Join(s.dataDir, "firmware", systemID)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return FirmwareFile{}, internal(err)
	}
	tmp, err := os.CreateTemp(filepath.Join(s.dataDir, "tmp"), "fw-*")
	if err != nil {
		return FirmwareFile{}, internal(err)
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName)
	h := sha256.New()
	// Read at most one byte more than the largest allowed size: anything bigger is rejected without buffering it.
	size, err := io.Copy(io.MultiWriter(tmp, h), io.LimitReader(r, max+1))
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return FirmwareFile{}, internal(err)
	}
	ok := false
	for _, n := range def.Sizes {
		ok = ok || n == size
	}
	if !ok {
		got := strconv.FormatInt(size, 10)
		if size > max {
			got = "more than " + strconv.FormatInt(max, 10)
		}
		return FirmwareFile{}, badRequest("%s must be %s bytes (got %s)", def.DisplayName, sizeList(def.Sizes), got)
	}
	sha := hex.EncodeToString(h.Sum(nil))
	if def.Pinned != "" && def.Pinned != sha {
		return FirmwareFile{}, badRequest("%s does not match the pinned SHA-256", def.DisplayName)
	}
	if err := os.Chmod(tmpName, 0o600); err != nil {
		return FirmwareFile{}, internal(err)
	}
	if err := os.Rename(tmpName, s.firmwarePath(systemID, fileID)); err != nil {
		return FirmwareFile{}, internal(err)
	}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO firmware_files(system_id, file_id, sha256, size, uploaded_at) VALUES (?,?,?,?,?)
		ON CONFLICT(system_id, file_id) DO UPDATE SET sha256 = excluded.sha256, size = excluded.size, uploaded_at = excluded.uploaded_at`,
		systemID, fileID, sha, size, s.now().Unix()); err != nil {
		return FirmwareFile{}, internal(err)
	}
	return s.firmwareDef(ctx, systemID, fileID)
}

func sizeList(sizes []int64) string {
	parts := make([]string, len(sizes))
	for i, n := range sizes {
		parts[i] = strconv.FormatInt(n, 10)
	}
	return strings.Join(parts, " or ")
}

// RemoveFirmware deletes a provided file (a pinned hash stays). ErrNotFound if it was not provided.
func (s *Service) RemoveFirmware(ctx context.Context, systemID, fileID string) error {
	res, err := s.db.ExecContext(ctx, `DELETE FROM firmware_files WHERE system_id = ? AND file_id = ?`, systemID, fileID)
	if err != nil {
		return internal(err)
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return ErrNotFound
	}
	if err := os.Remove(s.firmwarePath(systemID, fileID)); err != nil && !errors.Is(err, os.ErrNotExist) {
		return internal(err)
	}
	return nil
}

// SetFirmwarePin sets (64 lowercase hex characters) or clears (empty) the expected SHA-256 of a file.
func (s *Service) SetFirmwarePin(ctx context.Context, systemID, fileID, sha string) error {
	sha = strings.ToLower(strings.TrimSpace(sha))
	if _, err := s.firmwareDef(ctx, systemID, fileID); err != nil {
		return err
	}
	if sha == "" {
		_, err := s.db.ExecContext(ctx, `DELETE FROM firmware_pins WHERE system_id = ? AND file_id = ?`, systemID, fileID)
		return internal2(err)
	}
	if !ValidSHA256(sha) {
		return badRequest("SHA-256 must be 64 hexadecimal characters")
	}
	_, err := s.db.ExecContext(ctx, `INSERT INTO firmware_pins(system_id, file_id, sha256) VALUES (?,?,?)
		ON CONFLICT(system_id, file_id) DO UPDATE SET sha256 = excluded.sha256`, systemID, fileID, sha)
	return internal2(err)
}

var idRe = regexp.MustCompile(`^[a-z0-9_-]{1,32}$`)

// OpenFirmware opens a provided firmware file for download; the caller closes it. ErrNotFound if unknown or absent.
func (s *Service) OpenFirmware(ctx context.Context, systemID, fileID string) (*os.File, FirmwareFile, error) {
	if !idRe.MatchString(systemID) || !idRe.MatchString(fileID) {
		return nil, FirmwareFile{}, ErrNotFound
	}
	def, err := s.firmwareDef(ctx, systemID, fileID)
	if err != nil {
		return nil, FirmwareFile{}, err
	}
	if !def.Present {
		return nil, FirmwareFile{}, ErrNotFound
	}
	f, err := os.Open(s.firmwarePath(systemID, fileID))
	if errors.Is(err, os.ErrNotExist) {
		return nil, FirmwareFile{}, ErrNotFound
	}
	if err != nil {
		return nil, FirmwareFile{}, internal(err)
	}
	return f, def, nil
}

// ---- Core reports and the handshake core check ----

// CoreReport is a core reported by a Player (id and version string).
type CoreReport struct {
	ID      string `json:"id"`
	Version string `json:"version"`
}

// checkCores compares the reported cores with the registry; both problems are warnings.
func checkCores(reg []SystemEntry, cores []CoreReport) []Problem {
	var out []Problem
	for _, e := range reg {
		var got *CoreReport
		for i := range cores {
			if cores[i].ID == e.CoreID {
				got = &cores[i]
				break
			}
		}
		switch {
		case got == nil:
			out = append(out, Problem{Code: ProblemCoreMissing, CoreID: e.CoreID,
				Detail: "Core " + e.CoreID + " (" + e.Name + ") is not installed"})
		case e.ExpectedCoreVersion != "" && got.Version != e.ExpectedCoreVersion:
			out = append(out, Problem{Code: ProblemCoreVersionMismatch, CoreID: e.CoreID,
				Detail: "Core " + e.CoreID + " is version " + got.Version + ", " + e.ExpectedCoreVersion + " expected"})
		}
	}
	return out
}

func (s *Service) storeReport(ctx context.Context, deviceID string, in HandshakeInput, cores []CoreReport) error {
	if cores == nil {
		cores = []CoreReport{}
	}
	js, err := json.Marshal(cores)
	if err != nil {
		return internal(err)
	}
	_, err = s.db.ExecContext(ctx, `INSERT INTO device_reports(device_id, platform, arch, player_version, protocol_version, cores, reported_at)
		VALUES (?,?,?,?,?,?,?) ON CONFLICT(device_id) DO UPDATE SET platform = excluded.platform, arch = excluded.arch,
		player_version = excluded.player_version, protocol_version = excluded.protocol_version, cores = excluded.cores,
		reported_at = excluded.reported_at`,
		deviceID, in.Platform, in.Arch, in.PlayerVersion, in.ProtocolVersion, string(js), s.now().Unix())
	return internal2(err)
}

// ClientStatus is the compatibility status of a reporting device for one system.
type ClientStatus string

const (
	ClientCompatible   ClientStatus = "compatible"
	ClientCoreMismatch ClientStatus = "core_version_mismatch"
	ClientCoreMissing  ClientStatus = "core_missing"
	ClientPlayerTooOld ClientStatus = "player_too_old"
)

// ClientReport is the last handshake report of a trusted device, evaluated for one system.
type ClientReport struct {
	DeviceID        string
	DeviceName      string
	Platform        string
	Arch            string
	PlayerVersion   string
	ProtocolVersion int
	CoreVersion     string // version of the system's preferred core; empty if not installed
	Status          ClientStatus
	// Expected is the expected core version (mismatch) or the minimum protocol version (player too old).
	Expected   string
	ReportedAt time.Time
}

// ListClientReports returns the last report of each trusted device, evaluated against the system.
func (s *Service) ListClientReports(ctx context.Context, e SystemEntry) ([]ClientReport, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT d.id, d.name, r.platform, r.arch, r.player_version, r.protocol_version, r.cores, r.reported_at
		FROM device_reports r JOIN devices d ON d.id = r.device_id WHERE d.status = 'trusted' ORDER BY d.name COLLATE NOCASE, d.id`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	minProto := s.Info().MinProtocolVersion
	var out []ClientReport
	for rows.Next() {
		var c ClientReport
		var cores string
		var at int64
		if err := rows.Scan(&c.DeviceID, &c.DeviceName, &c.Platform, &c.Arch, &c.PlayerVersion, &c.ProtocolVersion, &cores, &at); err != nil {
			return nil, internal(err)
		}
		c.ReportedAt = time.Unix(at, 0).UTC()
		var reported []CoreReport
		_ = json.Unmarshal([]byte(cores), &reported)
		for _, cr := range reported {
			if cr.ID == e.CoreID {
				c.CoreVersion = cr.Version
			}
		}
		switch {
		case c.ProtocolVersion < minProto:
			c.Status, c.Expected = ClientPlayerTooOld, strconv.Itoa(minProto)
		case c.CoreVersion == "":
			c.Status = ClientCoreMissing
		case e.ExpectedCoreVersion != "" && c.CoreVersion != e.ExpectedCoreVersion:
			c.Status, c.Expected = ClientCoreMismatch, e.ExpectedCoreVersion
		default:
			c.Status = ClientCompatible
		}
		out = append(out, c)
	}
	return out, rows.Err()
}
