package web

import (
	"crypto/sha256"
	"encoding/hex"
	"io/fs"
	"net/http"
	"strings"
	"sync"
)

var (
	assetHashesOnce sync.Once
	assetHashes     map[string]string
)

// contentHash returns the first 10 hex characters of the SHA-256 of data.
func contentHash(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])[:10]
}

// staticHashes hashes every embedded static file once.
func staticHashes() map[string]string {
	assetHashesOnce.Do(func() {
		assetHashes = map[string]string{}
		_ = fs.WalkDir(staticFS, "static", func(p string, d fs.DirEntry, err error) error {
			if err != nil || d.IsDir() {
				return nil
			}
			if data, err := staticFS.ReadFile(p); err == nil {
				assetHashes[strings.TrimPrefix(p, "static/")] = contentHash(data)
			}
			return nil
		})
	})
	return assetHashes
}

// assetURL returns the cache-busting URL of an embedded static file: /static/<name>?v=<hash>.
func assetURL(name string) string {
	if h, ok := staticHashes()[name]; ok {
		return "/static/" + name + "?v=" + h
	}
	return "/static/" + name
}

// staticCacheControl lets a request whose v query matches the current content hash be cached for a year;
// everything else must revalidate so a stale URL never keeps an outdated file after a Hub update.
func staticCacheControl(r *http.Request) string {
	if v := r.URL.Query().Get("v"); v != "" {
		if h, ok := staticHashes()[strings.TrimPrefix(r.URL.Path, "/static/")]; ok && h == v {
			return "public, max-age=31536000, immutable"
		}
	}
	return "no-cache"
}
