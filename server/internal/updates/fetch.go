package updates

import (
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
)

const (
	fetchTimeout    = time.Minute
	downloadTimeout = 30 * time.Minute
)

// NewHTTPClient returns the client for index and artifact downloads: proxy from the environment, timeouts,
// redirects only to https.
func NewHTTPClient() *http.Client {
	return &http.Client{Transport: &http.Transport{Proxy: http.ProxyFromEnvironment, TLSHandshakeTimeout: 15 * time.Second,
		ResponseHeaderTimeout: 30 * time.Second}}
}

// Source is where the index comes from. file:// is accepted for the index (offline use, tests); artifact URLs may
// be file:// only when the index itself was loaded from file://.
type Source struct {
	IndexURL string
	Client   *http.Client // nil = NewHTTPClient()
}

func parseSourceURL(raw string, allowFile bool) (*url.URL, error) {
	u, err := url.Parse(raw)
	if err != nil {
		return nil, errors.New("invalid URL")
	}
	switch {
	case u.Scheme == "https" && u.Host != "" && u.User == nil:
	case allowFile && u.Scheme == "file" && (u.Host == "" || u.Host == "localhost") && u.Path != "":
	default:
		if allowFile {
			return nil, errors.New("URL must be https or file://")
		}
		return nil, errors.New("URL must be https")
	}
	return u, nil
}

// FileURL returns the file:// URL of a local path (also for drive paths on Windows: file:///C:/dir/file).
func FileURL(p string) string {
	p = filepath.ToSlash(p)
	if !strings.HasPrefix(p, "/") {
		p = "/" + p
	}
	return (&url.URL{Scheme: "file", Path: p}).String()
}

// filePath is the local path of a file:// URL (the inverse of FileURL).
func filePath(u *url.URL) string {
	p := u.Path
	if runtime.GOOS == "windows" && len(p) >= 3 && p[0] == '/' && p[2] == ':' {
		p = p[1:]
	}
	return filepath.FromSlash(p)
}

// ValidateIndexURL checks an index URL (https or file://).
func ValidateIndexURL(raw string) error { _, err := parseSourceURL(raw, true); return err }

// IsFile reports whether the index is loaded from a file:// URL.
func (s Source) IsFile() bool {
	u, err := url.Parse(s.IndexURL)
	return err == nil && u.Scheme == "file"
}

func (s Source) client() *http.Client {
	hc := s.Client
	if hc == nil {
		hc = NewHTTPClient()
	}
	cp := *hc // never modify the injected client
	cp.CheckRedirect = func(req *http.Request, via []*http.Request) error {
		if len(via) >= 5 {
			return errors.New("too many redirects")
		}
		if req.URL.Scheme != "https" {
			return errors.New("redirect to a non-https URL refused")
		}
		return nil
	}
	return &cp
}

// open opens a URL for reading; allowFile permits file:// (the index, or artifacts of a file:// index).
func (s Source) open(ctx context.Context, raw string, allowFile bool) (io.ReadCloser, error) {
	u, err := parseSourceURL(raw, allowFile)
	if err != nil {
		return nil, err
	}
	if u.Scheme == "file" {
		f, err := os.Open(filePath(u))
		if err != nil {
			return nil, err
		}
		return f, nil
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, raw, nil)
	if err != nil {
		return nil, err
	}
	resp, err := s.client().Do(req)
	if err != nil {
		return nil, err
	}
	if resp.StatusCode != http.StatusOK {
		resp.Body.Close()
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	return resp.Body, nil
}

func (s Source) fetch(ctx context.Context, raw string, limit int64) ([]byte, error) {
	ctx, cancel := context.WithTimeout(ctx, fetchTimeout)
	defer cancel()
	rc, err := s.open(ctx, raw, true)
	if err != nil {
		return nil, err
	}
	defer rc.Close()
	b, err := io.ReadAll(io.LimitReader(rc, limit+1))
	if err != nil {
		return nil, err
	}
	if int64(len(b)) > limit {
		return nil, errors.New("response too large")
	}
	return b, nil
}

// Fetched is a verified index together with the exact bytes it was verified from.
type Fetched struct {
	Source  Source
	Data    []byte
	Sig     []byte
	Index   Index
	Skipped []error // releases that failed validation
}

// VerifyIndex verifies the signature over data and parses it.
func VerifyIndex(data, sig []byte, keys []ed25519.PublicKey, allowFile bool) (Index, []error, error) {
	if err := corepkg.Verify(data, sig, keys); err != nil {
		if errors.Is(err, corepkg.ErrNoTrustedKey) {
			return Index{}, nil, err
		}
		return Index{}, nil, fmt.Errorf("index signature rejected: %w", err)
	}
	idx, errs := ParseIndexOpts(data, allowFile)
	if Fatal(errs) {
		return Index{}, nil, fmt.Errorf("index rejected: %w", errs[0])
	}
	return idx, errs, nil
}

// LoadIndex fetches index and signature, verifies and parses them.
func (s Source) LoadIndex(ctx context.Context, keys []ed25519.PublicKey) (*Fetched, error) {
	if len(keys) == 0 {
		return nil, corepkg.ErrNoTrustedKey
	}
	data, err := s.fetch(ctx, s.IndexURL, MaxIndexBytes)
	if err != nil {
		return nil, fmt.Errorf("fetch index: %w", err)
	}
	sig, err := s.fetch(ctx, s.IndexURL+".sig", MaxSigBytes)
	if err != nil {
		return nil, fmt.Errorf("fetch index signature: %w", err)
	}
	idx, skipped, err := VerifyIndex(data, sig, keys, s.IsFile())
	if err != nil {
		return nil, err
	}
	return &Fetched{Source: s, Data: data, Sig: sig, Index: idx, Skipped: skipped}, nil
}

// download streams the artifact to w, enforcing the signed size, and returns the SHA-256 (hex).
func (s Source) download(ctx context.Context, a Artifact, w io.Writer) (string, error) {
	ctx, cancel := context.WithTimeout(ctx, downloadTimeout)
	defer cancel()
	rc, err := s.open(ctx, a.URL, s.IsFile())
	if err != nil {
		return "", err
	}
	defer rc.Close()
	h := sha256.New()
	n, err := io.Copy(io.MultiWriter(w, h), io.LimitReader(rc, a.Size+1))
	if err != nil {
		return "", err
	}
	if n != a.Size {
		return "", fmt.Errorf("size mismatch: got %d bytes, index says %d", n, a.Size)
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}
