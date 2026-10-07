package hubtest

import (
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/hub"
)

// CoreSource is a fake trusted core source: an HTTPS test server with a signed index and dummy core files
// (random-looking bytes, never real cores) and a test signing key.
type CoreSource struct {
	Server *httptest.Server
	Pub    ed25519.PublicKey
	seed   []byte

	mu       sync.Mutex
	packages []corepkg.Package
	files    map[string][]byte // URL path -> content
	index    []byte
	sig      []byte
	// Down makes every request fail with 503.
	Down bool
	Hits map[string]int
}

// NewCoreSource starts the source; it stops with the test.
func NewCoreSource(t *testing.T) *CoreSource {
	t.Helper()
	c := &CoreSource{files: map[string][]byte{}, Hits: map[string]int{}, seed: make([]byte, ed25519.SeedSize)}
	c.seed[0] = 42
	c.Pub, _ = corepkg.PublicFromSeed(c.seed)
	c.Server = httptest.NewTLSServer(http.HandlerFunc(c.serve))
	t.Cleanup(c.Server.Close)
	c.Publish(t)
	return c
}

func (c *CoreSource) serve(w http.ResponseWriter, r *http.Request) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.Hits[r.URL.Path]++
	if c.Down {
		http.Error(w, "down", http.StatusServiceUnavailable)
		return
	}
	switch r.URL.Path {
	case "/cores-index.json":
		w.Write(c.index)
	case "/cores-index.json.sig":
		w.Write(c.sig)
	default:
		b, ok := c.files[r.URL.Path]
		if !ok {
			http.NotFound(w, r)
			return
		}
		w.Write(b)
	}
}

// IndexURL is the URL of cores-index.json.
func (c *CoreSource) IndexURL() string { return c.Server.URL + "/cores-index.json" }

// PublicKeyB64 is the base64 public key for --core-trust-key.
func (c *CoreSource) PublicKeyB64() string { return base64.StdEncoding.EncodeToString(c.Pub) }

// Apply points the Hub options at this source and trusts its key.
func (c *CoreSource) Apply(o *hub.Options) {
	o.CoreIndexURL = c.IndexURL()
	o.CoreTrustKeys = append(o.CoreTrustKeys, c.Pub)
	o.CoreHTTPClient = c.Server.Client()
}

// FileURL is the URL a package file is served from.
func (c *CoreSource) FileURL(coreID, version, platform, name string) string {
	return c.Server.URL + "/files/" + coreID + "-" + version + "-" + platform + "-" + name
}

// AddPackage adds a package (library with the given content plus a small license file) and republishes the index.
func (c *CoreSource) AddPackage(t *testing.T, coreID, version, platform string, lib []byte) corepkg.Package {
	t.Helper()
	p := corepkg.Package{CoreID: coreID, Version: version, Platform: platform, License: "GPL-3.0",
		SourceURL: "https://example.org/" + coreID, SourceRef: "v" + version, Origin: "test"}
	for _, f := range []struct {
		name, role string
		data       []byte
	}{{coreID + ".so", corepkg.RoleLibrary, lib}, {"LICENSE.txt", corepkg.RoleLicense, []byte("license of " + coreID)}} {
		h := sha256.Sum256(f.data)
		u := c.FileURL(coreID, version, platform, f.name)
		p.Files = append(p.Files, corepkg.File{Name: f.name, Role: f.role, Size: int64(len(f.data)), SHA256: hex.EncodeToString(h[:]), URL: u})
		c.mu.Lock()
		c.files[u[len(c.Server.URL):]] = f.data
		c.mu.Unlock()
	}
	c.mu.Lock()
	c.packages = append(c.packages, p)
	c.mu.Unlock()
	c.Publish(t)
	return p
}

// SetFile replaces the served bytes of a file (to simulate size/hash mismatches) without touching the index.
func (c *CoreSource) SetFile(urlPath string, data []byte) {
	c.mu.Lock()
	c.files[urlPath] = data
	c.mu.Unlock()
}

// Publish re-marshals and re-signs the index.
func (c *CoreSource) Publish(t *testing.T) {
	t.Helper()
	c.mu.Lock()
	defer c.mu.Unlock()
	idx, err := json.Marshal(corepkg.Index{Schema: 1, GeneratedAt: time.Now().UTC(), Packages: c.packages})
	if err != nil {
		t.Fatal(err)
	}
	sig, err := corepkg.Sign(idx, c.seed)
	if err != nil {
		t.Fatal(err)
	}
	c.index, c.sig = idx, sig
}

// Tamper changes the served index without re-signing it.
func (c *CoreSource) Tamper() {
	c.mu.Lock()
	c.index = append([]byte(" "), c.index...)
	c.mu.Unlock()
}

// Index returns the current index and signature bytes (e.g. to write them into an import directory).
func (c *CoreSource) Index() (index, sig []byte) {
	c.mu.Lock()
	defer c.mu.Unlock()
	return append([]byte(nil), c.index...), append([]byte(nil), c.sig...)
}

// FileBytes returns the served bytes of a URL path.
func (c *CoreSource) FileBytes(urlPath string) []byte {
	c.mu.Lock()
	defer c.mu.Unlock()
	return append([]byte(nil), c.files[urlPath]...)
}

// RemovePackage removes all platforms of a (core, version) from the index and republishes it.
func (c *CoreSource) RemovePackage(t *testing.T, coreID, version string) {
	t.Helper()
	c.mu.Lock()
	kept := c.packages[:0:0]
	for _, p := range c.packages {
		if !(p.CoreID == coreID && p.Version == version) {
			kept = append(kept, p)
		}
	}
	c.packages = kept
	c.mu.Unlock()
	c.Publish(t)
}
