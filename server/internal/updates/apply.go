package updates

import (
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// Runner runs an external command (injectable; tests use a fake). Output is combined stdout/stderr.
type Runner func(ctx context.Context, name string, args ...string) ([]byte, error)

// LinuxPlatform is the platform string of this binary, e.g. linux-amd64.
func LinuxPlatform() string { return "linux-" + runtime.GOARCH }

// Result is the content of <data>/updates/last-result.json.
type Result struct {
	Version string    `json:"version"`
	Time    time.Time `json:"time"`
	OK      bool      `json:"ok"`
	Message string    `json:"message"`
}

// ReadResult returns the last apply result, or (nil, nil) if there is none.
func ReadResult(dataDir string) (*Result, error) {
	b, err := os.ReadFile(filepath.Join(dataDir, UpdatesDir, LastResultName))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	var r Result
	if err := json.Unmarshal(b, &r); err != nil {
		return nil, err
	}
	return &r, nil
}

// ApplyOptions configure ApplyStaged.
type ApplyOptions struct {
	DataDir string
	// RequestFile is deleted first (nothing happens when it is "" or missing).
	RequestFile string
	// Keys are the trusted signing keys: compiled-in plus FRAMEBEAM_HUB_CORE_TRUST_KEYS. Nothing from the data
	// directory is trusted without a valid signature.
	Keys []ed25519.PublicKey
	// CurrentVersion is the compiled-in version of the running binary.
	CurrentVersion string
	// Platform defaults to LinuxPlatform().
	Platform string
	// Run runs dpkg; required.
	Run Runner
	// Now defaults to time.Now. TempDir is the parent of the private temp directory ("" = system default).
	Now     func() time.Time
	TempDir string
}

const maxMetaBytes = 1 << 20

// ApplyStaged is the root helper behind `framebeam-hub update apply-staged`. The data directory is writable by
// the (less trusted) service user, so everything read from it is treated as untrusted: the index signature is
// verified with trusted keys only, the version must be strictly newer than the running one (no replay of old
// signed releases), symlinks inside the data directory are never followed, and the package is copied into a
// root-owned private directory and verified again there before dpkg sees it. The outcome is written to
// last-result.json (owned like the data directory).
func ApplyStaged(ctx context.Context, o ApplyOptions) (res Result, err error) {
	if o.Now == nil {
		o.Now = time.Now
	}
	if o.Platform == "" {
		o.Platform = LinuxPlatform()
	}
	if o.RequestFile != "" {
		if rerr := os.Remove(o.RequestFile); rerr != nil && !errors.Is(rerr, os.ErrNotExist) {
			return res, fmt.Errorf("remove request file: %w", rerr)
		}
	}
	root, err := os.OpenRoot(o.DataDir)
	if err != nil {
		return res, fmt.Errorf("open data directory: %w", err)
	}
	defer root.Close()
	defer func() {
		res.Time = o.Now().UTC().Truncate(time.Second)
		res.OK = err == nil
		if err != nil {
			res.Message = cleanMsg(err.Error())
		}
		if werr := writeResult(root, res); werr != nil && err == nil {
			err = fmt.Errorf("update installed but writing last-result.json failed: %w", werr)
		}
	}()

	if o.Run == nil {
		return res, errors.New("no command runner")
	}
	if len(o.Keys) == 0 {
		return res, errors.New("no trusted signing key configured")
	}
	cur, perr := ParseSemVer(o.CurrentVersion)
	if perr != nil {
		return res, fmt.Errorf("running version %q is not a release version; refusing to update", o.CurrentVersion)
	}
	stagedRel := path.Join(UpdatesDir, StagedDirName)
	for _, d := range []string{UpdatesDir, stagedRel} { // real directories only, no symlinks
		fi, lerr := root.Lstat(d)
		if lerr != nil {
			return res, fmt.Errorf("nothing staged: %w", lerr)
		}
		if !fi.IsDir() {
			return res, fmt.Errorf("%s is not a plain directory", d)
		}
	}
	read := func(name string) ([]byte, error) {
		f, oerr := openRegular(root, path.Join(stagedRel, name))
		if oerr != nil {
			return nil, oerr
		}
		defer f.Close()
		b, rerr := io.ReadAll(io.LimitReader(f, maxMetaBytes+1))
		if rerr != nil {
			return nil, rerr
		}
		if len(b) > maxMetaBytes {
			return nil, fmt.Errorf("%s is too large", name)
		}
		return b, nil
	}
	meta, err := read(StagedFileName)
	if err != nil {
		return res, fmt.Errorf("staged.json: %w", err)
	}
	var st Staged
	if err = json.Unmarshal(meta, &st); err != nil {
		return res, fmt.Errorf("staged.json: %w", err)
	}
	res.Version = st.Version
	idxData, err := read(IndexFileName)
	if err != nil {
		return res, fmt.Errorf("staged index: %w", err)
	}
	sig, err := read(SigFileName)
	if err != nil {
		return res, fmt.Errorf("staged index signature: %w", err)
	}
	idx, _, err := VerifyIndex(idxData, sig, o.Keys, true) // URLs are not used here, only the signed size and hash
	if err != nil {
		return res, err
	}
	cand, perr := ParseSemVer(st.Version)
	if perr != nil {
		return res, perr
	}
	var art *Artifact
	for i := range idx.Releases {
		r := &idx.Releases[i]
		if r.Product != ProductHub || r.Version != st.Version {
			continue
		}
		for j := range r.Artifacts {
			a := &r.Artifacts[j]
			if a.Platform == o.Platform && a.Kind == KindDeb && a.Name == st.Artifact {
				art = a
			}
		}
	}
	if art == nil {
		return res, fmt.Errorf("the signed index has no %s package %q for version %s", o.Platform, st.Artifact, st.Version)
	}
	if cand.Compare(cur) <= 0 {
		return res, fmt.Errorf("version %s is not newer than the running version %s; refusing to apply", st.Version, o.CurrentVersion)
	}

	src, err := openRegular(root, path.Join(stagedRel, art.Name))
	if err != nil {
		return res, fmt.Errorf("staged package: %w", err)
	}
	defer src.Close()
	tmp, err := os.MkdirTemp(o.TempDir, "framebeam-update-")
	if err != nil {
		return res, err
	}
	defer os.RemoveAll(tmp)
	copyPath := tmp + "/" + art.Name
	dst, err := os.OpenFile(copyPath, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return res, err
	}
	n, err := io.Copy(dst, io.LimitReader(src, art.Size+1))
	if cerr := dst.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return res, err
	}
	if n != art.Size {
		return res, fmt.Errorf("staged package has %d bytes, the signed index says %d", n, art.Size)
	}
	// Verify the root-owned copy itself (not the data directory file) before dpkg reads it.
	got, err := fileSHA256(copyPath)
	if err != nil {
		return res, err
	}
	if got != art.SHA256 {
		return res, errors.New("staged package SHA-256 does not match the signed index")
	}
	out, err := o.Run(ctx, "dpkg", "-i", copyPath)
	if err != nil {
		return res, fmt.Errorf("dpkg -i failed: %w: %s", err, tail(string(out), 400))
	}
	return res, nil
}

// openRegular opens a regular file below root without following a symlink as the last element.
func openRegular(root *os.Root, name string) (*os.File, error) {
	fi, err := root.Lstat(name)
	if err != nil {
		return nil, err
	}
	if !fi.Mode().IsRegular() {
		return nil, fmt.Errorf("%s is not a regular file", name)
	}
	f, err := root.Open(name)
	if err != nil {
		return nil, err
	}
	fi2, err := f.Stat()
	if err != nil || !os.SameFile(fi, fi2) {
		f.Close()
		return nil, fmt.Errorf("%s changed while opening", name)
	}
	return f, nil
}

func fileSHA256(p string) (string, error) {
	f, err := os.Open(p)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// writeResult writes last-result.json below <data>/updates, owned by the owner of the data directory. An existing
// entry (also a symlink planted by the service user) is removed and the file created exclusively.
func writeResult(root *os.Root, res Result) error {
	rel := path.Join(UpdatesDir, LastResultName)
	if fi, err := root.Lstat(UpdatesDir); err != nil || !fi.IsDir() {
		return errors.New("updates directory is missing or not a plain directory")
	}
	if err := root.Remove(rel); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	f, err := root.OpenFile(rel, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o640)
	if err != nil {
		return err
	}
	b, _ := json.Marshal(res)
	_, err = f.Write(append(b, '\n'))
	if err == nil {
		if fi, serr := root.Stat("."); serr == nil {
			if uid, gid, ok := dirOwner(fi); ok {
				_ = f.Chown(uid, gid) // best effort: fails when not running as root, where ownership is already right
			}
		}
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	return err
}

func cleanMsg(s string) string {
	s = strings.Map(func(r rune) rune {
		if r < 0x20 || r == 0x7f {
			return ' '
		}
		return r
	}, s)
	return tail(s, 500)
}

func tail(s string, n int) string {
	s = strings.TrimSpace(s)
	if len(s) > n {
		return "..." + s[len(s)-n:]
	}
	return s
}
