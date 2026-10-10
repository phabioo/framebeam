package hubtest

import (
	"archive/zip"
	"bytes"
	"fmt"
	"hash/crc32"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// Platforms of the fake buildbot: FrameBeam platform, buildbot path, library suffix.
var buildbotPlatforms = []struct{ ID, Dir, Suffix string }{
	{"windows-x64", "windows/x86_64", ".dll"},
	{"linux-x64", "linux/x86_64", ".so"},
}

// BuildbotCore describes a core of the fake buildbot. Lib is the dummy library content (never a real core).
type BuildbotCore struct {
	ID, SystemID, DisplayName, License, RequiredHWAPI string
	Extensions                                        string   // supported_extensions of the info file, default "nds|bin"
	Date                                              string   // build date YYYY-MM-DD for all platforms
	Lib                                               []byte   // default: the core id repeated
	Platforms                                         []string // default: all
}

type fakeBuild struct {
	date string
	crc  string
	zip  []byte
}

// Buildbot is a fake libretro buildbot: an HTTPS test server with info.zip, .index-extended files and zips that
// hold dummy library files.
type Buildbot struct {
	Server *httptest.Server
	// Down makes every request fail with 503.
	Down bool
	Hits map[string]int

	mu     sync.Mutex
	infos  map[string]string                // core id -> info file content
	builds map[string]map[string]*fakeBuild // platform -> file name -> build
}

// NewBuildbot starts the fake buildbot; it stops with the test.
func NewBuildbot(t *testing.T) *Buildbot {
	t.Helper()
	b := &Buildbot{Hits: map[string]int{}, infos: map[string]string{}, builds: map[string]map[string]*fakeBuild{}}
	b.Server = httptest.NewTLSServer(http.HandlerFunc(b.serve))
	t.Cleanup(b.Server.Close)
	return b
}

// Apply points the Hub options at the fake buildbot.
func (b *Buildbot) Apply(o *hub.Options) {
	o.CoreBuildbotURL = b.Server.URL + "/nightly"
	o.CoreInfoURL = b.Server.URL + "/assets/frontend/info.zip"
	o.CoreHTTPClient = b.Server.Client()
}

// ZipURL is the URL of a core zip.
func (b *Buildbot) ZipURL(platform, coreID string) string {
	for _, p := range buildbotPlatforms {
		if p.ID == platform {
			return b.Server.URL + "/nightly/" + p.Dir + "/latest/" + coreID + "_libretro" + p.Suffix + ".zip"
		}
	}
	return ""
}

// MakeZip builds a zip from name -> content (sorted by name; names are taken as they are, so tests can add bad ones).
func MakeZip(t *testing.T, files map[string][]byte) []byte {
	t.Helper()
	b, err := makeZip(files)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func makeZip(files map[string][]byte) ([]byte, error) {
	names := make([]string, 0, len(files))
	for n := range files {
		names = append(names, n)
	}
	sort.Strings(names)
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)
	for _, n := range names {
		w, err := zw.Create(n)
		if err != nil {
			return nil, err
		}
		if _, err := w.Write(files[n]); err != nil {
			return nil, err
		}
	}
	if err := zw.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func infoText(c BuildbotCore) string {
	if c.Extensions == "" {
		c.Extensions = "nds|bin"
	}
	hw := ""
	if c.RequiredHWAPI != "" {
		hw = fmt.Sprintf("required_hw_api = %q\n", c.RequiredHWAPI)
	}
	return fmt.Sprintf("display_name = %q\ncorename = %q\nsystemid = %q\nlicense = %q\nsupported_extensions = %q\n"+
		"display_version = \"Git\"\nfirmware_count = 1\nfirmware0_desc = \"BIOS for %s\"\nnotes = \"(!) Needs BIOS|Other note\"\n%s",
		c.DisplayName, c.DisplayName, c.SystemID, c.License, c.Extensions, c.ID, hw)
}

// AddCore adds the core info and a one-file zip per platform. Defaults: display name = id, license GPLv2.
func (b *Buildbot) AddCore(t *testing.T, c BuildbotCore) {
	t.Helper()
	if c.DisplayName == "" {
		c.DisplayName = c.ID
	}
	if c.License == "" {
		c.License = "GPLv2"
	}
	if c.Date == "" {
		c.Date = "2026-10-09"
	}
	if c.Lib == nil {
		c.Lib = bytes.Repeat([]byte(c.ID+"!"), 40)
	}
	plats := c.Platforms
	if plats == nil {
		for _, p := range buildbotPlatforms {
			plats = append(plats, p.ID)
		}
	}
	b.mu.Lock()
	b.infos[c.ID] = infoText(c)
	b.mu.Unlock()
	for _, id := range plats {
		suffix := ""
		for _, p := range buildbotPlatforms {
			if p.ID == id {
				suffix = p.Suffix
			}
		}
		lib := append([]byte(nil), c.Lib...)
		b.SetZip(id, c.ID, c.Date, MakeZip(t, map[string][]byte{c.ID + "_libretro" + suffix: lib}))
	}
}

// libraryCRC is the CRC32 the real index lists: the one of the uncompressed library (the zip entry's CRC32). For
// something that is not a zip with a library, the CRC32 of the bytes.
func libraryCRC(z []byte) string {
	if zr, err := zip.NewReader(bytes.NewReader(z), int64(len(z))); err == nil {
		for _, f := range zr.File {
			if strings.Contains(f.Name, "_libretro.") {
				return fmt.Sprintf("%08x", f.CRC32)
			}
		}
	}
	return fmt.Sprintf("%08x", crc32.ChecksumIEEE(z))
}

// SetZip sets the zip of a core for a platform (the CRC32 of its library is listed in the index) and the build date.
func (b *Buildbot) SetZip(platform, coreID, date string, zipBytes []byte) {
	suffix := ""
	for _, p := range buildbotPlatforms {
		if p.ID == platform {
			suffix = p.Suffix
		}
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.builds[platform] == nil {
		b.builds[platform] = map[string]*fakeBuild{}
	}
	b.builds[platform][coreID+"_libretro"+suffix+".zip"] = &fakeBuild{date: date, crc: libraryCRC(zipBytes), zip: zipBytes}
}

// SetCRC overrides the CRC32 listed in the index (to simulate a corrupt download).
func (b *Buildbot) SetCRC(platform, coreID, crc string) {
	b.mu.Lock()
	defer b.mu.Unlock()
	for _, p := range buildbotPlatforms {
		if p.ID == platform {
			b.builds[platform][coreID+"_libretro"+p.Suffix+".zip"].crc = crc
		}
	}
}

// InfoZip returns the current info.zip.
func (b *Buildbot) InfoZip(t *testing.T) []byte {
	t.Helper()
	b.mu.Lock()
	defer b.mu.Unlock()
	files := map[string][]byte{}
	for id, txt := range b.infos {
		files[id+"_libretro.info"] = []byte(txt)
	}
	return MakeZip(t, files)
}

// IndexExtended returns the .index-extended of a platform.
func (b *Buildbot) IndexExtended(platform string) []byte {
	b.mu.Lock()
	defer b.mu.Unlock()
	var names []string
	for n := range b.builds[platform] {
		names = append(names, n)
	}
	sort.Strings(names)
	var sb strings.Builder
	for _, n := range names {
		bd := b.builds[platform][n]
		fmt.Fprintf(&sb, "%s %s %s\n", bd.date, bd.crc, n)
	}
	return []byte(sb.String())
}

// WriteImportDir writes the buildbot content as an import directory (ADR 0020 D8): info.zip and, per platform,
// the zips and an .index-extended.
func (b *Buildbot) WriteImportDir(t *testing.T, dir string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, "info.zip"), b.InfoZip(t), 0o644); err != nil {
		t.Fatal(err)
	}
	b.mu.Lock()
	snapshot := map[string]map[string][]byte{}
	for plat, files := range b.builds {
		snapshot[plat] = map[string][]byte{}
		for n, bd := range files {
			snapshot[plat][n] = bd.zip
		}
	}
	b.mu.Unlock()
	for plat, files := range snapshot {
		pd := filepath.Join(dir, plat)
		if err := os.MkdirAll(pd, 0o755); err != nil {
			t.Fatal(err)
		}
		for n, z := range files {
			if err := os.WriteFile(filepath.Join(pd, n), z, 0o644); err != nil {
				t.Fatal(err)
			}
		}
		if err := os.WriteFile(filepath.Join(pd, ".index-extended"), b.IndexExtended(plat), 0o644); err != nil {
			t.Fatal(err)
		}
	}
}

func (b *Buildbot) serve(w http.ResponseWriter, r *http.Request) {
	b.mu.Lock()
	b.Hits[r.URL.Path]++
	down := b.Down
	b.mu.Unlock()
	if down {
		http.Error(w, "down", http.StatusServiceUnavailable)
		return
	}
	if r.URL.Path == "/assets/frontend/info.zip" {
		b.mu.Lock()
		files := map[string][]byte{}
		for id, txt := range b.infos {
			files[id+"_libretro.info"] = []byte(txt)
		}
		b.mu.Unlock()
		z, _ := makeZip(files)
		w.Write(z)
		return
	}
	for _, p := range buildbotPlatforms {
		prefix := "/nightly/" + p.Dir + "/latest/"
		if !strings.HasPrefix(r.URL.Path, prefix) {
			continue
		}
		name := strings.TrimPrefix(r.URL.Path, prefix)
		if name == ".index-extended" {
			w.Write(b.IndexExtended(p.ID))
			return
		}
		b.mu.Lock()
		bd := b.builds[p.ID][name]
		b.mu.Unlock()
		if bd == nil {
			http.NotFound(w, r)
			return
		}
		w.Write(bd.zip)
		return
	}
	http.NotFound(w, r)
}
