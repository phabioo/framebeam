package tlsutil

import (
	"crypto/tls"
	"errors"
	"os"
	"regexp"
	"testing"
)

// The vectors were computed independently (Python: first 60 bits of the SHA-256, 5 bits per Crockford symbol).
func TestSecurityCodeVectors(t *testing.T) {
	seq := make([]byte, 32)
	for i := range seq {
		seq[i] = byte(i)
	}
	for _, c := range []struct {
		der  []byte
		want string
	}{
		{[]byte("uzume test certificate"), "QX7A-7H6Y-J729"},
		{nil, "WERC-8GMR-ZGE1"},
		{seq, "CC6W-TAB6-RGSP"},
	} {
		if got := SecurityCodeDER(c.der); got != c.want {
			t.Errorf("SecurityCodeDER(%q) = %s, want %s", c.der, got, c.want)
		}
	}
	if got := SecurityCode(tls.Certificate{Certificate: [][]byte{[]byte("uzume test certificate")}}); got != "QX7A-7H6Y-J729" {
		t.Errorf("SecurityCode = %s", got)
	}
	if SecurityCode(tls.Certificate{}) != "" {
		t.Error("no certificate must give no code")
	}
}

func TestSecurityCodeShapeAndMatch(t *testing.T) {
	c, err := EnsureSelfSigned(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	code := SecurityCode(c)
	if !regexp.MustCompile(`^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$`).MatchString(code) {
		t.Fatalf("shape: %q", code)
	}
	typed := " " + code[:4] + " " + code[5:9] + code[10:] + " "
	if !SecurityCodesMatch(code, typed) || !SecurityCodesMatch(code, lower(code)) {
		t.Fatal("spaces, hyphens and case must not matter")
	}
	if SecurityCodesMatch(code, "AAAA-AAAA-AAAA") || SecurityCodesMatch("", "") {
		t.Fatal("different or empty codes must not match")
	}
}

func lower(s string) string {
	b := []byte(s)
	for i, c := range b {
		if c >= 'A' && c <= 'Z' {
			b[i] = c + 32
		}
	}
	return string(b)
}

func TestLoadSelfSignedReadOnly(t *testing.T) {
	dir := t.TempDir()
	if _, err := LoadSelfSigned(dir); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("want not exist, got %v", err)
	}
	if _, err := os.Stat(dir + "/tls"); err == nil {
		t.Fatal("LoadSelfSigned must not create anything")
	}
	want, err := EnsureSelfSigned(dir)
	if err != nil {
		t.Fatal(err)
	}
	got, err := LoadSelfSigned(dir)
	if err != nil || SecurityCode(got) != SecurityCode(want) {
		t.Fatalf("%v %v", err, got)
	}
}
