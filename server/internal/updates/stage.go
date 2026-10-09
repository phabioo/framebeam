package updates

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
)

// Names inside the data directory and request directory.
const (
	UpdatesDir      = "updates"
	StagedDirName   = "staged"
	IndexFileName   = "updates-index.json"
	SigFileName     = "updates-index.json.sig"
	StagedFileName  = "staged.json"
	LastResultName  = "last-result.json"
	RequestFileName = "update-request"
	// DefaultRequestDir is where the request file lives (systemd RuntimeDirectory of the Hub service).
	DefaultRequestDir = "/run/framebeam"
	// PackagedExecutable is the path of the Hub binary of a .deb install.
	PackagedExecutable = "/usr/bin/framebeam-hub"
)

// Staged is the content of staged.json.
type Staged struct {
	Version  string `json:"version"`
	Artifact string `json:"artifact"`
}

// StagedDir is <data>/updates/staged.
func StagedDir(dataDir string) string { return filepath.Join(dataDir, UpdatesDir, StagedDirName) }

// ReadStaged returns the staged update, or (nil, nil) when nothing is staged.
func ReadStaged(dataDir string) (*Staged, error) {
	b, err := os.ReadFile(filepath.Join(StagedDir(dataDir), StagedFileName))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	var st Staged
	if err := json.Unmarshal(b, &st); err != nil {
		return nil, err
	}
	return &st, nil
}

// ClearStaged removes the staged directory.
func ClearStaged(dataDir string) error { return os.RemoveAll(StagedDir(dataDir)) }

// Stage downloads the artifact of rel (already selected from f), verifies its size and SHA-256 against the
// signed index, and writes it to <data>/updates/staged/ together with the exact index and signature bytes and
// staged.json. A previous staged update is replaced. staged.json is written last, so a half-written stage is
// never applied.
func Stage(ctx context.Context, f *Fetched, rel Release, art Artifact, dataDir string) (Staged, error) {
	st := Staged{Version: rel.Version, Artifact: art.Name}
	if !ValidSemVer(rel.Version) || !nameRe.MatchString(art.Name) {
		return st, errors.New("invalid release or artifact name")
	}
	dir := StagedDir(dataDir)
	if err := os.MkdirAll(filepath.Join(dataDir, UpdatesDir), 0o750); err != nil {
		return st, err
	}
	if err := os.RemoveAll(dir); err != nil {
		return st, err
	}
	if err := os.Mkdir(dir, 0o750); err != nil {
		return st, err
	}
	fail := func(err error) (Staged, error) {
		_ = os.RemoveAll(dir)
		return st, err
	}
	tmp, err := os.CreateTemp(dir, ".download-*")
	if err != nil {
		return fail(err)
	}
	sum, err := f.Source.download(ctx, art, tmp)
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return fail(fmt.Errorf("download %s: %w", art.Name, err))
	}
	if sum != art.SHA256 {
		return fail(fmt.Errorf("download %s: SHA-256 does not match the signed index", art.Name))
	}
	if err := os.Rename(tmp.Name(), filepath.Join(dir, art.Name)); err != nil {
		return fail(err)
	}
	for name, data := range map[string][]byte{IndexFileName: f.Data, SigFileName: f.Sig} {
		if err := os.WriteFile(filepath.Join(dir, name), data, 0o640); err != nil {
			return fail(err)
		}
	}
	meta, _ := json.Marshal(st)
	if err := os.WriteFile(filepath.Join(dir, StagedFileName), append(meta, '\n'), 0o640); err != nil {
		return fail(err)
	}
	return st, nil
}

// RequestPath is <dir>/update-request.
func RequestPath(requestDir string) string { return filepath.Join(requestDir, RequestFileName) }

// RequestDirWritable reports whether requestDir exists and a file can be created in it.
func RequestDirWritable(requestDir string) bool {
	if requestDir == "" {
		return false
	}
	if st, err := os.Stat(requestDir); err != nil || !st.IsDir() {
		return false
	}
	f, err := os.CreateTemp(requestDir, ".probe-*")
	if err != nil {
		return false
	}
	name := f.Name()
	f.Close()
	os.Remove(name)
	return true
}

// WriteRequest creates the request file that triggers the root helper (systemd path unit).
func WriteRequest(requestDir, version string) error {
	if !RequestDirWritable(requestDir) {
		return fmt.Errorf("request directory %s does not exist or is not writable", requestDir)
	}
	tmp, err := os.CreateTemp(requestDir, ".request-*")
	if err != nil {
		return err
	}
	_, err = tmp.WriteString(version + "\n")
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Chmod(tmp.Name(), 0o640)
	}
	if err == nil {
		err = os.Rename(tmp.Name(), RequestPath(requestDir))
	}
	if err != nil {
		os.Remove(tmp.Name())
	}
	return err
}

// RequestPending reports whether a request file exists.
func RequestPending(requestDir string) bool {
	_, err := os.Lstat(RequestPath(requestDir))
	return err == nil
}

func requestExists(p string) bool {
	_, err := os.Lstat(p)
	return err == nil
}
