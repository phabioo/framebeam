// Command framebeam-sign creates and signs the core index and the updates index of FrameBeam.
//
//	framebeam-sign keygen -out <seed-file>
//	framebeam-sign add -index <cores-index.json> -package <package.json>
//	framebeam-sign release-add -index <updates-index.json> -release <release.json> [-keep 5]
//	framebeam-sign sign -index <cores-index.json|updates-index.json> [-out <file>.sig]   (seed from env FRAMEBEAM_SIGNING_KEY)
//	framebeam-sign verify -index <file> -sig <file> -pub <base64 public key>
//	framebeam-sign pubkey                                             (seed from env FRAMEBEAM_SIGNING_KEY)
//
// The private key (seed) is never read from a flag and never printed.
package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"time"

	"github.com/phabioo/framebeam/server/internal/corepkg"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// warnOut receives non-fatal warnings (a variable for tests).
var warnOut io.Writer = os.Stderr

const signingKeyEnv = "FRAMEBEAM_SIGNING_KEY"

func main() {
	if err := run(os.Args[1:], os.Getenv, os.Stdout, time.Now); err != nil {
		fmt.Fprintln(os.Stderr, "Error:", err)
		os.Exit(1)
	}
}

func run(args []string, getenv func(string) string, out io.Writer, now func() time.Time) error {
	if len(args) == 0 {
		return errors.New("usage: framebeam-sign keygen|add|release-add|sign|verify|pubkey [flags]")
	}
	fs := flag.NewFlagSet(args[0], flag.ContinueOnError)
	switch args[0] {
	case "keygen":
		o := fs.String("out", "", "file for the base64 seed (created 0600, never overwritten)")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		return keygen(*o, out)
	case "add":
		idx := fs.String("index", "", "cores-index.json (created when missing)")
		pkg := fs.String("package", "", "package JSON (same shape as an index entry)")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		return add(*idx, *pkg, now)
	case "release-add":
		idx := fs.String("index", "", "updates-index.json (created when missing)")
		rel := fs.String("release", "", "release JSON (one object, same shape as an index entry)")
		keep := fs.Int("keep", 5, "releases to keep per product and channel (newest by SemVer)")
		allowFile := fs.Bool("allow-file-urls", false, "accept file:// artifact URLs (tests only; default https only)")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		return releaseAdd(*idx, *rel, *keep, *allowFile, now)
	case "sign":
		idx := fs.String("index", "", "cores-index.json or updates-index.json (detected by content)")
		o := fs.String("out", "", "signature file (default <index>.sig)")
		allowFile := fs.Bool("allow-file-urls", false, "accept file:// artifact URLs in an updates index (tests only)")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		return sign(*idx, *o, *allowFile, getenv)
	case "verify":
		idx := fs.String("index", "", "cores-index.json")
		sig := fs.String("sig", "", "signature file")
		pub := fs.String("pub", "", "base64 public key")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		return verify(*idx, *sig, *pub, out)
	case "pubkey":
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		seed, err := seedFromEnv(getenv)
		if err != nil {
			return err
		}
		return printKey(seed, out)
	}
	return fmt.Errorf("unknown command %q", args[0])
}

func seedFromEnv(getenv func(string) string) ([]byte, error) {
	v := getenv(signingKeyEnv)
	if v == "" {
		return nil, fmt.Errorf("environment variable %s is not set", signingKeyEnv)
	}
	return corepkg.ParseSeed(v)
}

func printKey(seed []byte, out io.Writer) error {
	pub, err := corepkg.PublicFromSeed(seed)
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "public_key=%s\nkey_id=%s\n", base64.StdEncoding.EncodeToString(pub), corepkg.KeyID(pub))
	return nil
}

func keygen(path string, out io.Writer) error {
	if path == "" {
		return errors.New("-out is missing")
	}
	seed := make([]byte, ed25519.SeedSize)
	if _, err := rand.Read(seed); err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return err
	}
	_, err = f.WriteString(base64.StdEncoding.EncodeToString(seed) + "\n")
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(path)
		return err
	}
	return printKey(seed, out)
}

func add(indexPath, pkgPath string, now func() time.Time) error {
	if indexPath == "" || pkgPath == "" {
		return errors.New("-index and -package are required")
	}
	raw, err := os.ReadFile(pkgPath)
	if err != nil {
		return err
	}
	var p corepkg.Package
	if err := json.Unmarshal(raw, &p); err != nil {
		return fmt.Errorf("package file: %w", err)
	}
	if err := corepkg.ValidatePackage(p); err != nil {
		return fmt.Errorf("package file: %w", err)
	}
	idx := corepkg.Index{Schema: corepkg.Schema}
	switch data, err := os.ReadFile(indexPath); {
	case errors.Is(err, os.ErrNotExist):
	case err != nil:
		return err
	default:
		var errs []error
		if idx, errs = corepkg.ParseIndex(data); len(errs) > 0 {
			return fmt.Errorf("existing index is invalid: %w", errs[0])
		}
	}
	replaced := false
	for i, q := range idx.Packages {
		if q.CoreID == p.CoreID && q.Version == p.Version && q.Platform == p.Platform {
			idx.Packages[i], replaced = p, true
		}
	}
	if !replaced {
		idx.Packages = append(idx.Packages, p)
	}
	corepkg.SortPackages(idx.Packages)
	idx.GeneratedAt = now().UTC().Truncate(time.Second)
	b, err := json.MarshalIndent(idx, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(indexPath, append(b, '\n'), 0o644)
}

// addLegacyCompanions appends, per hub channel, the legacy companion of the newest regular release (see
// updates.LegacyCompanion). rels must not contain companions.
func addLegacyCompanions(rels []updates.Release) []updates.Release {
	newest := map[string]updates.Release{}
	for _, q := range rels {
		if q.Product != updates.ProductHub {
			continue
		}
		if cur, ok := newest[q.Channel]; !ok || updates.MustCompare(q.Version, cur.Version) > 0 {
			newest[q.Channel] = q
		}
	}
	for _, q := range newest {
		if c, ok := updates.LegacyCompanion(q); ok {
			rels = append(rels, c)
		}
	}
	updates.SortReleases(rels)
	return rels
}

// releaseAdd adds or replaces one release in the updates index (created when missing), keeps the newest keep
// releases per (product, channel) by SemVer and writes deterministic JSON.
func releaseAdd(indexPath, relPath string, keep int, allowFile bool, now func() time.Time) error {
	if indexPath == "" || relPath == "" {
		return errors.New("-index and -release are required")
	}
	if keep < 1 {
		return errors.New("-keep must be at least 1")
	}
	raw, err := os.ReadFile(relPath)
	if err != nil {
		return err
	}
	var r updates.Release
	if err := json.Unmarshal(raw, &r); err != nil {
		return fmt.Errorf("release file: %w", err)
	}
	if err := updates.ValidateReleaseOpts(r, allowFile); err != nil {
		return fmt.Errorf("release file: %w", err)
	}
	idx := updates.Index{Schema: updates.Schema}
	switch data, err := os.ReadFile(indexPath); {
	case errors.Is(err, os.ErrNotExist):
	case err != nil:
		return err
	default:
		var errs []error
		idx, errs = updates.ParseIndexOpts(data, allowFile)
		if updates.Fatal(errs) {
			return fmt.Errorf("existing index is invalid: %w", errs[0])
		}
		for _, e := range errs { // entries invalid under the current rules (e.g. the old channel "test") are dropped
			fmt.Fprintln(warnOut, "Warning: dropping release from the existing index:", e)
		}
	}
	rels := idx.Releases[:0:0]
	for _, q := range idx.Releases {
		if q.Product == r.Product && q.Channel == r.Channel && q.Version == r.Version {
			continue // replaced
		}
		if q.Product == updates.ProductHub && updates.IsLegacyCompanion(q.Version) {
			continue // regenerated below, not counted by keep
		}
		rels = append(rels, q)
	}
	idx.Releases = updates.Prune(append(rels, r), keep)
	idx.Releases = addLegacyCompanions(idx.Releases)
	idx.GeneratedAt = now().UTC().Truncate(time.Second)
	b, err := updates.Marshal(idx)
	if err != nil {
		return err
	}
	return os.WriteFile(indexPath, b, 0o644)
}

func sign(indexPath, outPath string, allowFile bool, getenv func(string) string) error {
	if indexPath == "" {
		return errors.New("-index is required")
	}
	if outPath == "" {
		outPath = indexPath + ".sig"
	}
	seed, err := seedFromEnv(getenv)
	if err != nil {
		return err
	}
	data, err := os.ReadFile(indexPath)
	if err != nil {
		return err
	}
	if updates.LooksLikeUpdatesIndex(data) {
		if _, errs := updates.ParseIndexOpts(data, allowFile); len(errs) > 0 {
			return fmt.Errorf("updates index is invalid: %w", errs[0])
		}
	} else if _, errs := corepkg.ParseIndex(data); len(errs) > 0 {
		return fmt.Errorf("index is invalid: %w", errs[0])
	}
	line, err := corepkg.Sign(data, seed)
	if err != nil {
		return err
	}
	return os.WriteFile(outPath, line, 0o644)
}

func verify(indexPath, sigPath, pub string, out io.Writer) error {
	if indexPath == "" || sigPath == "" || pub == "" {
		return errors.New("-index, -sig and -pub are required")
	}
	key, err := corepkg.ParsePublicKey(pub)
	if err != nil {
		return err
	}
	data, err := os.ReadFile(indexPath)
	if err != nil {
		return err
	}
	sig, err := os.ReadFile(sigPath)
	if err != nil {
		return err
	}
	if err := corepkg.Verify(data, sig, []ed25519.PublicKey{key}); err != nil {
		return err
	}
	fmt.Fprintf(out, "OK key_id=%s\n", corepkg.KeyID(key))
	return nil
}
