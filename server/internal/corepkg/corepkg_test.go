package corepkg

import (
	"crypto/ed25519"
	"encoding/base64"
	"encoding/json"
	"errors"
	"strings"
	"testing"
	"time"
)

const goodSHA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

func goodPkg() Package {
	return Package{CoreID: "melonds_ds", Version: "1.4.0", Platform: "linux-x64", License: "GPL-3.0",
		SourceURL: "https://example.org/src", SourceRef: "v1.4.0", Origin: "test",
		Files: []File{
			{Name: "lib.so", Role: RoleLibrary, Size: 10, SHA256: goodSHA, URL: "https://example.org/lib.so"},
			{Name: "LICENSE.txt", Role: RoleLicense, Size: 5, SHA256: goodSHA, URL: "https://example.org/l.txt"},
		}}
}

func indexBytes(t *testing.T, schema int, ps ...Package) []byte {
	t.Helper()
	b, err := json.Marshal(Index{Schema: schema, GeneratedAt: time.Date(2026, 10, 7, 12, 0, 0, 0, time.UTC), Packages: ps})
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestParseIndexOK(t *testing.T) {
	idx, errs := ParseIndex(indexBytes(t, 1, goodPkg()))
	if len(errs) != 0 || len(idx.Packages) != 1 || idx.Packages[0].Files[0].Name != "lib.so" {
		t.Fatalf("idx=%+v errs=%v", idx, errs)
	}
	// Unknown fields are ignored.
	raw := `{"schema":1,"generated_at":"2026-10-07T12:00:00Z","future":true,"packages":[]}`
	if _, errs := ParseIndex([]byte(raw)); len(errs) != 0 {
		t.Fatal(errs)
	}
}

func TestParseIndexFatal(t *testing.T) {
	dup := indexBytes(t, 1, goodPkg(), goodPkg())
	for name, data := range map[string][]byte{
		"unknown schema": indexBytes(t, 2),
		"duplicate":      dup,
		"not json":       []byte("nope"),
		"trailing":       append(indexBytes(t, 1), []byte(`{}`)...),
	} {
		idx, errs := ParseIndex(data)
		if !Fatal(errs) || len(idx.Packages) != 0 {
			t.Errorf("%s: want fatal, got %v", name, errs)
		}
	}
}

func TestParseIndexSkipsInvalidPackages(t *testing.T) {
	mut := func(f func(*Package)) Package {
		p := goodPkg()
		p.Files = append([]File(nil), p.Files...)
		f(&p)
		return p
	}
	bad := map[string]Package{
		"core id":      mut(func(p *Package) { p.CoreID = "Melon DS" }),
		"version":      mut(func(p *Package) { p.Version = "../1" }),
		"platform":     mut(func(p *Package) { p.Platform = "beos" }),
		"path name":    mut(func(p *Package) { p.Files[0].Name = "../lib.so" }),
		"slash name":   mut(func(p *Package) { p.Files[0].Name = "a/b.so" }),
		"dotdot name":  mut(func(p *Package) { p.Files[0].Name = "a..b" }),
		"http url":     mut(func(p *Package) { p.Files[0].URL = "http://example.org/lib.so" }),
		"no host url":  mut(func(p *Package) { p.Files[0].URL = "https:///x" }),
		"bad sha":      mut(func(p *Package) { p.Files[0].SHA256 = strings.ToUpper(goodSHA) }),
		"zero size":    mut(func(p *Package) { p.Files[0].Size = 0 }),
		"two libs":     mut(func(p *Package) { p.Files[1].Role = RoleLibrary }),
		"no lib":       mut(func(p *Package) { p.Files = p.Files[1:] }),
		"unknown role": mut(func(p *Package) { p.Files[1].Role = "bios" }),
		"duplicate nm": mut(func(p *Package) { p.Files[1].Name = "lib.so" }),
		"no files":     mut(func(p *Package) { p.Files = nil }),
	}
	for name, p := range bad {
		other := goodPkg()
		other.Platform = "windows-x64"
		idx, errs := ParseIndex(indexBytes(t, 1, p, other))
		var pe *PackageError
		if len(errs) != 1 || !errors.As(errs[0], &pe) || Fatal(errs) {
			t.Errorf("%s: want one package error, got %v", name, errs)
		}
		if len(idx.Packages) != 1 || idx.Packages[0].Platform != "windows-x64" {
			t.Errorf("%s: valid package lost: %+v", name, idx.Packages)
		}
	}
}

func TestSignVerify(t *testing.T) {
	seed := make([]byte, 32)
	seed[0] = 7
	pub, _ := PublicFromSeed(seed)
	idx := indexBytes(t, 1, goodPkg())
	sig, err := Sign(idx, seed)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(string(sig), "ed25519 "+KeyID(pub)+" ") || !strings.HasSuffix(string(sig), "\n") || len(KeyID(pub)) != 16 {
		t.Fatalf("sig line %q", sig)
	}
	keys := []ed25519.PublicKey{pub}
	if err := Verify(idx, sig, keys); err != nil {
		t.Fatal(err)
	}
	// Tampered index.
	if err := Verify(append([]byte(" "), idx...), sig, keys); err == nil {
		t.Fatal("tampered index verified")
	}
	// Wrong key (id differs) and same id but other key bytes.
	other := make([]byte, 32)
	other[0] = 9
	otherPub, _ := PublicFromSeed(other)
	if err := Verify(idx, sig, []ed25519.PublicKey{otherPub}); err == nil || !strings.Contains(err.Error(), "not trusted") {
		t.Fatalf("wrong key: %v", err)
	}
	// No keys.
	if err := Verify(idx, sig, nil); !errors.Is(err, ErrNoTrustedKey) {
		t.Fatalf("no key: %v", err)
	}
	// Bad signature lines.
	for _, line := range []string{"", "ed25519 " + KeyID(pub), "rsa " + KeyID(pub) + " AAAA", "ed25519 " + KeyID(pub) + " !!!",
		"ed25519 " + KeyID(pub) + " " + base64.StdEncoding.EncodeToString([]byte("short")), "ed25519 a b c d", string(sig) + string(sig)} {
		if err := Verify(idx, []byte(line), keys); err == nil {
			t.Errorf("bad sig line %q verified", line)
		}
	}
}

func TestKeyParsing(t *testing.T) {
	if _, err := ParsePublicKey("x"); err == nil {
		t.Fatal("bad pub")
	}
	if _, err := ParseSeed(base64.StdEncoding.EncodeToString([]byte("short"))); err == nil {
		t.Fatal("short seed")
	}
	if _, err := Sign(nil, []byte("short")); err == nil {
		t.Fatal("short seed signed")
	}
	if ks, err := TrustedKeys(nil); err != nil || len(ks) != len(DefaultTrustedKeys) {
		t.Fatal(ks, err)
	}
	if _, err := TrustedKeys([]string{"bad"}); err == nil {
		t.Fatal("bad trusted key accepted")
	}
}

func TestCompareVersions(t *testing.T) {
	for _, c := range []struct {
		a, b string
		want int
	}{{"1.10.0", "1.9.9", 1}, {"1.4.0", "1.4.0", 0}, {"1.4", "1.4.0", -1}, {"1.4.0", "1.4.0-rc1", -1}, {"2", "10", -1}, {"a", "b", -1}} {
		if got := CompareVersions(c.a, c.b); got != c.want {
			t.Errorf("%s vs %s: %d want %d", c.a, c.b, got, c.want)
		}
	}
}
