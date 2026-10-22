package hub

import (
	"strings"
	"testing"
)

func TestNewInviteCodeUsesUZPrefixAndRoundTrips(t *testing.T) {
	code, err := newInviteCode()
	if err != nil || !strings.HasPrefix(code, "UZ-") || len(code) != 12 {
		t.Fatalf("%q %v", code, err)
	}
	if got := normalizeInviteCode(strings.ToLower(strings.ReplaceAll(code, "-", " "))); got != code {
		t.Fatalf("normalize(%q) = %q", code, got)
	}
}

func TestNormalizeInviteCode(t *testing.T) {
	for in, want := range map[string]string{
		"FB-2345-6789": "", // only the UZ- prefix exists
		"uz-2345-6789": "UZ-2345-6789",
		"UZ23456789":   "UZ-2345-6789",
		"XX-2345-6789": "",
		"UZ-2345-678":  "",
		"UZ-2345-6781": "", // 1 is not in the alphabet
		"":             "",
	} {
		if got := normalizeInviteCode(in); got != want {
			t.Errorf("normalizeInviteCode(%q) = %q, want %q", in, got, want)
		}
	}
}
