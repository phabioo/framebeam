package main

import (
	"crypto/ed25519"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/pluginpkg"
)

// pluginSign validates a .fbplugin file and writes it with SIGNATURE.json added or replaced.
func pluginSign(in, out string, getenv func(string) string) error {
	if in == "" || out == "" {
		return errors.New("-in and -out are required")
	}
	seed, err := seedFromEnv(getenv)
	if err != nil {
		return err
	}
	data, err := os.ReadFile(in)
	if err != nil {
		return err
	}
	signed, err := pluginpkg.Sign(data, seed)
	if err != nil {
		return err
	}
	return writeFileAtomic(out, signed)
}

func writeFileAtomic(path string, data []byte) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), ".plugin-sign-*")
	if err != nil {
		return err
	}
	name := tmp.Name()
	_, err = tmp.Write(data)
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Chmod(name, 0o644)
	}
	if err == nil {
		err = os.Rename(name, path)
	}
	if err != nil {
		os.Remove(name)
	}
	return err
}

// pluginAdd adds or replaces the entry of (id, version) in the plugin index (created when missing). The plugin
// must carry a valid signature of a trusted key (the compiled-in keys and, when FRAMEBEAM_SIGNING_KEY is set, the
// key of that seed). Other entries are kept.
func pluginAdd(indexPath, pluginPath, rawURL string, allowFile bool, getenv func(string) string, now func() time.Time) error {
	if indexPath == "" || pluginPath == "" || rawURL == "" {
		return errors.New("-index, -plugin and -url are required")
	}
	data, err := os.ReadFile(pluginPath)
	if err != nil {
		return err
	}
	p, err := pluginpkg.Open(data)
	if err != nil {
		return err
	}
	keys, err := corepkg.TrustedKeys(nil)
	if err != nil {
		return err
	}
	if getenv(signingKeyEnv) != "" {
		seed, err := seedFromEnv(getenv)
		if err != nil {
			return err
		}
		pub, err := corepkg.PublicFromSeed(seed)
		if err != nil {
			return err
		}
		keys = append(keys, ed25519.PublicKey(pub))
	}
	switch v := p.Verify(keys); v.State {
	case pluginpkg.SigValid:
	case pluginpkg.SigInvalid:
		return fmt.Errorf("plugin signature is invalid: %s", v.Reason)
	default:
		return errors.New("plugin is not signed; run plugin-sign first")
	}
	e := pluginpkg.EntryFor(p, rawURL)
	if err := pluginpkg.ValidateEntry(e, allowFile); err != nil {
		return fmt.Errorf("index entry: %w", err)
	}
	idx := pluginpkg.Index{Format: pluginpkg.IndexFormat}
	switch raw, err := os.ReadFile(indexPath); {
	case errors.Is(err, os.ErrNotExist):
	case err != nil:
		return err
	default:
		var errs []error
		idx, errs = pluginpkg.ParseIndex(raw, allowFile)
		if pluginpkg.Fatal(errs) {
			return fmt.Errorf("existing index is invalid: %w", errs[0])
		}
		for _, e := range errs { // entries invalid under the current rules are dropped
			fmt.Fprintln(warnOut, "Warning: dropping entry from the existing index:", e)
		}
	}
	entries := idx.Plugins[:0:0]
	for _, q := range idx.Plugins {
		if q.ID == e.ID && q.Version == e.Version {
			continue // replaced
		}
		entries = append(entries, q)
	}
	idx.Plugins = append(entries, e)
	pluginpkg.SortEntries(idx.Plugins)
	idx.GeneratedAt = now().UTC().Truncate(time.Second)
	b, err := pluginpkg.MarshalIndex(idx)
	if err != nil {
		return err
	}
	return os.WriteFile(indexPath, b, 0o644)
}
