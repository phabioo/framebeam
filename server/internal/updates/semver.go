// Package updates implements the FrameBeam update feed (0.3 "Automatic updates"): the signed updates index,
// SemVer comparison, release selection, staging of a verified package and the root helper that applies it.
// Signing primitives come from package corepkg (ADR 0010).
package updates

import (
	"fmt"
	"regexp"
	"strings"
)

// SemVer is a parsed Semantic Version 2.0.0. Build metadata is dropped (it never takes part in precedence).
type SemVer struct {
	Major, Minor, Patch string // decimal strings without leading zeros (arbitrary size)
	Pre                 []string
}

var semverRe = regexp.MustCompile(`^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)` +
	`(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?` +
	`(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$`)

// ParseSemVer parses a SemVer 2.0 string (a leading "v" is not accepted).
func ParseSemVer(s string) (SemVer, error) {
	m := semverRe.FindStringSubmatch(s)
	if m == nil || len(s) > 128 {
		return SemVer{}, fmt.Errorf("%q is not a SemVer 2.0 version", s)
	}
	v := SemVer{Major: m[1], Minor: m[2], Patch: m[3]}
	if m[4] != "" {
		v.Pre = strings.Split(m[4], ".")
	}
	return v, nil
}

// ValidSemVer reports whether s is a SemVer 2.0 version.
func ValidSemVer(s string) bool { _, err := ParseSemVer(s); return err == nil }

func cmpNum(a, b string) int { // decimal strings without leading zeros
	if len(a) != len(b) {
		if len(a) < len(b) {
			return -1
		}
		return 1
	}
	return strings.Compare(a, b)
}

func isNumeric(s string) bool {
	for _, c := range s {
		if c < '0' || c > '9' {
			return false
		}
	}
	return s != ""
}

// Compare returns -1, 0 or 1 by SemVer precedence.
func (a SemVer) Compare(b SemVer) int {
	for _, p := range [][2]string{{a.Major, b.Major}, {a.Minor, b.Minor}, {a.Patch, b.Patch}} {
		if c := cmpNum(p[0], p[1]); c != 0 {
			return c
		}
	}
	switch {
	case len(a.Pre) == 0 && len(b.Pre) == 0:
		return 0
	case len(a.Pre) == 0:
		return 1 // a release is higher than any of its prereleases
	case len(b.Pre) == 0:
		return -1
	}
	for i := 0; i < len(a.Pre) && i < len(b.Pre); i++ {
		x, y := a.Pre[i], b.Pre[i]
		xn, yn := isNumeric(x), isNumeric(y)
		var c int
		switch {
		case xn && yn:
			c = cmpNum(x, y)
		case xn:
			c = -1 // numeric identifiers sort below alphanumeric ones
		case yn:
			c = 1
		default:
			c = strings.Compare(x, y)
		}
		if c != 0 {
			return c
		}
	}
	switch {
	case len(a.Pre) < len(b.Pre):
		return -1
	case len(a.Pre) > len(b.Pre):
		return 1
	}
	return 0
}

// CompareVersions compares two SemVer strings. Both must be valid.
func CompareVersions(a, b string) (int, error) {
	x, err := ParseSemVer(a)
	if err != nil {
		return 0, err
	}
	y, err := ParseSemVer(b)
	if err != nil {
		return 0, err
	}
	return x.Compare(y), nil
}

// MustCompare compares two versions that are known to be valid (index entries are validated); an invalid
// string sorts lowest.
func MustCompare(a, b string) int {
	x, errA := ParseSemVer(a)
	y, errB := ParseSemVer(b)
	switch {
	case errA != nil && errB != nil:
		return strings.Compare(a, b)
	case errA != nil:
		return -1
	case errB != nil:
		return 1
	}
	return x.Compare(y)
}
