// Package corepkg describes signed core indexes and packages (0.2 "Cores from the Hub"): parsing, validation,
// signing and verification. It is shared by the Hub and the framebeam-sign tool.
package corepkg

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Schema is the supported index schema version.
const Schema = 1

// Limits.
const (
	MaxIndexBytes = 4 << 20 // size of cores-index.json
	MaxSigBytes   = 4 << 10 // size of the signature file
	MaxFileBytes  = 1 << 30 // size of one core file
	MaxFilesPer   = 32      // files per package
	MaxPackages   = 4096    // packages per index
	maxTextLen    = 512
)

// File roles.
const (
	RoleLibrary = "library"
	RoleLicense = "license"
)

var (
	coreIDRe  = regexp.MustCompile(`^[a-z0-9_]{1,64}$`)
	versionRe = regexp.MustCompile(`^[0-9A-Za-z][0-9A-Za-z.+_-]{0,63}$`)
	nameRe    = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$`)
	sha256Re  = regexp.MustCompile(`^[0-9a-f]{64}$`)
)

var platforms = []string{"windows-x64", "linux-x64", "linux-arm64", "macos-x64", "macos-arm64"}

// PlatformValid reports whether p is one of the known platform strings.
func PlatformValid(p string) bool {
	for _, v := range platforms {
		if v == p {
			return true
		}
	}
	return false
}

// ValidCoreID, ValidVersion and ValidFileName check the identifier formats.
func ValidCoreID(s string) bool   { return coreIDRe.MatchString(s) }
func ValidVersion(s string) bool  { return versionRe.MatchString(s) }
func ValidFileName(s string) bool { return nameRe.MatchString(s) && !strings.Contains(s, "..") }

// File is one file of a package.
type File struct {
	Name   string `json:"name"`
	Role   string `json:"role"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
	URL    string `json:"url"`
}

// Package is one (core_id, version, platform) entry of the index.
type Package struct {
	CoreID    string `json:"core_id"`
	Version   string `json:"version"`
	Platform  string `json:"platform"`
	License   string `json:"license"`
	SourceURL string `json:"source_url"`
	SourceRef string `json:"source_ref"`
	Origin    string `json:"origin"`
	Files     []File `json:"files"`
}

// Index is the content of cores-index.json.
type Index struct {
	Schema      int       `json:"schema"`
	GeneratedAt time.Time `json:"generated_at"`
	Packages    []Package `json:"packages"`
}

// PackageError reports one package that failed validation and was skipped. Every other error returned by
// ParseIndex is fatal: the whole index must be rejected.
type PackageError struct {
	CoreID, Version, Platform string
	Err                       error
}

func (e *PackageError) Error() string {
	return fmt.Sprintf("package %s %s %s: %v", e.CoreID, e.Version, e.Platform, e.Err)
}
func (e *PackageError) Unwrap() error { return e.Err }

// Fatal reports whether errs contains an error that makes the index unusable.
func Fatal(errs []error) bool {
	for _, e := range errs {
		var pe *PackageError
		if !errors.As(e, &pe) {
			return true
		}
	}
	return false
}

// ParseIndex parses and validates an index. Packages that fail validation are skipped and reported as
// *PackageError; fatal errors (bad JSON, unknown schema, duplicate packages) return an empty Index.
func ParseIndex(data []byte) (Index, []error) {
	if len(data) > MaxIndexBytes {
		return Index{}, []error{errors.New("index is too large")}
	}
	var idx Index
	dec := json.NewDecoder(bytes.NewReader(data))
	if err := dec.Decode(&idx); err != nil {
		return Index{}, []error{fmt.Errorf("index is not valid JSON: %w", err)}
	}
	if dec.More() {
		return Index{}, []error{errors.New("index has trailing data")}
	}
	if idx.Schema != Schema {
		return Index{}, []error{fmt.Errorf("unknown index schema %d", idx.Schema)}
	}
	if len(idx.Packages) > MaxPackages {
		return Index{}, []error{errors.New("index has too many packages")}
	}
	seen := map[string]bool{}
	for _, p := range idx.Packages {
		k := p.CoreID + "\x00" + p.Version + "\x00" + p.Platform
		if seen[k] {
			return Index{}, []error{fmt.Errorf("duplicate package %s %s %s", p.CoreID, p.Version, p.Platform)}
		}
		seen[k] = true
	}
	var errs []error
	valid := idx.Packages[:0:0]
	for _, p := range idx.Packages {
		if err := ValidatePackage(p); err != nil {
			errs = append(errs, &PackageError{p.CoreID, p.Version, p.Platform, err})
			continue
		}
		valid = append(valid, p)
	}
	idx.Packages = valid
	return idx, errs
}

// ValidatePackage checks one package entry.
func ValidatePackage(p Package) error {
	switch {
	case !ValidCoreID(p.CoreID):
		return errors.New("invalid core_id")
	case !ValidVersion(p.Version):
		return errors.New("invalid version")
	case !PlatformValid(p.Platform):
		return errors.New("unknown platform")
	case len(p.License) > 128 || len(p.SourceURL) > maxTextLen || len(p.SourceRef) > maxTextLen || len(p.Origin) > maxTextLen:
		return errors.New("text field too long")
	case len(p.Files) == 0 || len(p.Files) > MaxFilesPer:
		return errors.New("a package needs 1 to 32 files")
	}
	libs := 0
	names := map[string]bool{}
	for _, f := range p.Files {
		if !ValidFileName(f.Name) {
			return fmt.Errorf("invalid file name %q", f.Name)
		}
		if names[f.Name] {
			return fmt.Errorf("duplicate file name %q", f.Name)
		}
		names[f.Name] = true
		switch f.Role {
		case RoleLibrary:
			libs++
		case RoleLicense:
		default:
			return fmt.Errorf("file %s: unknown role", f.Name)
		}
		if f.Size <= 0 || f.Size > MaxFileBytes {
			return fmt.Errorf("file %s: invalid size", f.Name)
		}
		if !sha256Re.MatchString(f.SHA256) {
			return fmt.Errorf("file %s: sha256 must be 64 lowercase hex characters", f.Name)
		}
		u, err := url.Parse(f.URL)
		if err != nil || u.Scheme != "https" || u.Host == "" || u.User != nil {
			return fmt.Errorf("file %s: url must be https", f.Name)
		}
	}
	if libs != 1 {
		return errors.New("a package needs exactly one library file")
	}
	return nil
}

// SortPackages orders packages by core_id, version (CompareVersions), platform.
func SortPackages(ps []Package) {
	sort.SliceStable(ps, func(i, j int) bool {
		a, b := ps[i], ps[j]
		if a.CoreID != b.CoreID {
			return a.CoreID < b.CoreID
		}
		if a.Version != b.Version {
			return CompareVersions(a.Version, b.Version) < 0
		}
		return a.Platform < b.Platform
	})
}

// CompareVersions compares dot-separated versions segment by segment (numerically when both segments are
// numbers, otherwise as strings), then falls back to a plain string compare. Returns -1, 0 or 1.
func CompareVersions(a, b string) int {
	as, bs := strings.Split(a, "."), strings.Split(b, ".")
	for i := 0; i < len(as) && i < len(bs); i++ {
		x, ex := strconv.ParseUint(as[i], 10, 64)
		y, ey := strconv.ParseUint(bs[i], 10, 64)
		var c int
		if ex == nil && ey == nil {
			c = cmpInt(x, y)
		} else {
			c = strings.Compare(as[i], bs[i])
		}
		if c != 0 {
			return c
		}
	}
	if c := cmpInt(uint64(len(as)), uint64(len(bs))); c != 0 {
		return c
	}
	return strings.Compare(a, b)
}

func cmpInt(a, b uint64) int {
	switch {
	case a < b:
		return -1
	case a > b:
		return 1
	}
	return 0
}
