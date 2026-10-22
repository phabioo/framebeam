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
	"strings"
	"time"
)

// Runner runs an external command (injectable; tests use a fake). Output is combined stdout/stderr.
type Runner func(ctx context.Context, name string, args ...string) ([]byte, error)

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
	// Keys are the trusted signing keys: compiled-in plus UZUME_SERVER_CORE_TRUST_KEYS. Nothing from the data
	// directory is trusted without a valid signature.
	Keys []ed25519.PublicKey
	// CurrentVersion is the compiled-in version of the running binary.
	CurrentVersion string
	// Platform defaults to Platform().
	Platform string
	// Run runs the installer (dpkg or msiexec); required.
	Run Runner
	// Now defaults to time.Now. TempDir is the parent of the private temp directory ("" = system default).
	Now     func() time.Time
	TempDir string
	// Protocol is the protocol_version of the running build; a rollback target must speak it (0 = not checked).
	Protocol int
	// PrepareDB writes the update settings (staged settings.json) into the restored database copy at dbPath before
	// it replaces the live database. Required for a rollback that restores a database.
	PrepareDB func(dbPath string, settings map[string]string) error
}

const maxMetaBytes = 1 << 20

// ApplyStaged is the root helper behind `uzume-server update apply-staged`. The data directory is writable by
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
		o.Platform = Platform()
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
	kind := KindFor(o.Platform)
	var art *Artifact
	var rel *Release
	for i := range idx.Releases {
		r := &idx.Releases[i]
		if r.Product != ProductHub || r.Version != st.Version {
			continue
		}
		for j := range r.Artifacts {
			a := &r.Artifacts[j]
			if a.Platform == o.Platform && a.Kind == kind && a.Name == st.Artifact {
				art, rel = a, r
			}
		}
	}
	if art == nil {
		return res, fmt.Errorf("the signed index has no %s package %q for version %s", o.Platform, st.Artifact, st.Version)
	}
	if st.Rollback {
		if err = checkRollbackTarget(o, st, cur, cand, idx, rel, kind); err != nil {
			return res, err
		}
	} else {
		if st.RestoreDB != "" {
			return res, errors.New("a database restore is only allowed when going back")
		}
		if cand.Compare(cur) <= 0 {
			return res, fmt.Errorf("version %s is not newer than the running version %s; refusing to apply", st.Version, o.CurrentVersion)
		}
	}

	src, err := openRegular(root, path.Join(stagedRel, art.Name))
	if err != nil {
		return res, fmt.Errorf("staged package: %w", err)
	}
	defer src.Close()
	if o.TempDir != "" {
		if err = os.MkdirAll(o.TempDir, 0o700); err != nil {
			return res, err
		}
	}
	tmp, err := os.MkdirTemp(o.TempDir, "uzume-update-")
	if err != nil {
		return res, err
	}
	defer os.RemoveAll(tmp)
	copyPath := filepath.Join(tmp, art.Name)
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
	// Verify the root-owned copy itself (not the data directory file) before the installer reads it.
	got, err := fileSHA256(copyPath)
	if err != nil {
		return res, err
	}
	if got != art.SHA256 {
		return res, errors.New("staged package SHA-256 does not match the signed index")
	}
	// The installer log goes into the private directory and is copied into the data directory afterwards through
	// root, so the (less trusted) service user cannot redirect what a privileged process writes.
	logPath := filepath.Join(tmp, MSILogName)
	name, args := InstallCommand(o.Platform, copyPath, logPath)
	var swap *dbSwap
	if st.Rollback && st.RestoreDB != "" {
		if swap, err = prepareDBSwap(ctx, root, o, st, read); err != nil {
			return res, err
		}
	}
	out, err := o.Run(ctx, name, args...)
	if swap != nil {
		swap.finish(ctx, o, err == nil)
	}
	if kind == KindMSI {
		copyInstallerLog(root, logPath)
	}
	if err != nil {
		return res, fmt.Errorf("%s failed: %w: %s", name, err, tail(string(out), 400))
	}
	return res, nil
}

// copyInstallerLog copies the msiexec log to <data>/updates/msiexec.log (best effort, capped at 4 MiB).
func copyInstallerLog(root *os.Root, logPath string) {
	src, err := os.Open(logPath)
	if err != nil {
		return
	}
	defer src.Close()
	rel := path.Join(UpdatesDir, MSILogName)
	_ = root.Remove(rel)
	dst, err := root.OpenFile(rel, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o640)
	if err != nil {
		return
	}
	_, _ = io.Copy(dst, io.LimitReader(src, 4<<20))
	_ = dst.Close()
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

// checkRollbackTarget decides whether the root helper may install st.Version although it is older than the running
// version. A downgrade must not be a replay of an old signed index or an arbitrary old release, so the signed
// index has to be fresh, the target has to speak the protocol version of the running build and an MSI has to
// allow being installed over a newer one.
func checkRollbackTarget(o ApplyOptions, st Staged, cur, cand SemVer, idx Index, rel *Release, kind string) error {
	if cand.Compare(cur) >= 0 {
		return fmt.Errorf("version %s is not older than the running version %s; this is not a rollback", st.Version, o.CurrentVersion)
	}
	if err := idx.CheckFresh(o.Now()); err != nil {
		return fmt.Errorf("refusing to go back: %w", err)
	}
	if o.Protocol > 0 && (rel.MinProtocolVersion > o.Protocol || rel.ProtocolVersion < o.Protocol) {
		return fmt.Errorf("version %s does not speak protocol version %d; refusing to go back", st.Version, o.Protocol)
	}
	if kind == KindMSI && !rel.MSIDowngrade {
		return fmt.Errorf("the installer of %s cannot be installed over a newer version; reinstall it by hand", st.Version)
	}
	if st.RestoreDB != "" {
		b, ok := ParseDBBackupName(st.RestoreDB)
		if !ok {
			return errors.New("invalid database backup name")
		}
		if rel.SchemaVersion > 0 && b.From > rel.SchemaVersion {
			return fmt.Errorf("database backup %s has schema %d, newer than the schema %d of version %s", st.RestoreDB, b.From, rel.SchemaVersion, st.Version)
		}
		if o.PrepareDB == nil {
			return errors.New("no database preparation configured")
		}
	}
	return nil
}

// Names of the database files the rollback moves.
const (
	dbFileName      = "framebeam.db"
	dbRestoringName = "framebeam.db.restoring"
)

var sqliteHeader = []byte("SQLite format 3\x00")

// ServiceCommand is the command that stops or starts the server service (action "stop" or "start").
func ServiceCommand(platform, action string) (name string, args []string) {
	if isWindowsPlatform(platform) {
		return "net.exe", []string{action, "FrameBeamHub"}
	}
	return "systemctl", []string{action, "framebeam-hub.service"}
}

// dbSwap is a prepared database replacement: the restored copy waits as framebeam.db.restoring until the service
// is stopped.
type dbSwap struct {
	root    *os.Root
	suffix  string
	started bool // the service was stopped by us
	swapped bool
	moved   []string
}

// prepareDBSwap copies the named pre-update backup into the data directory, checks it, lets o.PrepareDB write the
// update settings into it, and replaces the live database with it while the server service is stopped. Everything
// before the stop can fail without changing anything.
func prepareDBSwap(ctx context.Context, root *os.Root, o ApplyOptions, st Staged, read func(string) ([]byte, error)) (*dbSwap, error) {
	var settings map[string]string
	if b, err := read(SettingsFileName); err == nil {
		if err := json.Unmarshal(b, &settings); err != nil {
			return nil, fmt.Errorf("staged settings: %w", err)
		}
	}
	srcRel := path.Join(DBBackupDir, st.RestoreDB)
	src, err := openRegular(root, srcRel)
	if err != nil {
		return nil, fmt.Errorf("database backup: %w", err)
	}
	defer src.Close()
	head := make([]byte, len(sqliteHeader))
	if _, err := io.ReadFull(src, head); err != nil || string(head) != string(sqliteHeader) {
		return nil, errors.New("database backup is not an SQLite database")
	}
	if _, err := src.Seek(0, io.SeekStart); err != nil {
		return nil, err
	}
	_ = root.Remove(dbRestoringName)
	dst, err := root.OpenFile(dbRestoringName, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o640)
	if err != nil {
		return nil, err
	}
	_, err = io.Copy(dst, src)
	if err == nil {
		if fi, serr := root.Stat("."); serr == nil {
			if uid, gid, ok := dirOwner(fi); ok {
				_ = dst.Chown(uid, gid)
			}
		}
		err = dst.Sync()
	}
	if cerr := dst.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		_ = root.Remove(dbRestoringName)
		return nil, err
	}
	if err = o.PrepareDB(filepath.Join(o.DataDir, dbRestoringName), settings); err != nil {
		_ = root.Remove(dbRestoringName)
		return nil, fmt.Errorf("prepare restored database: %w", err)
	}
	sw := &dbSwap{root: root, suffix: ".rollback-" + o.Now().UTC().Format("20060102T150405Z")}
	name, args := ServiceCommand(o.Platform, "stop")
	if out, err := o.Run(ctx, name, args...); err != nil {
		_ = root.Remove(dbRestoringName)
		return nil, fmt.Errorf("stop the server: %w: %s", err, tail(string(out), 300))
	}
	sw.started = true
	for _, n := range []string{dbFileName, dbFileName + "-wal", dbFileName + "-shm"} {
		if _, lerr := root.Lstat(n); lerr != nil {
			continue
		}
		if err := root.Rename(n, n+sw.suffix); err != nil {
			sw.undo(ctx, o)
			return nil, fmt.Errorf("move the database aside: %w", err)
		}
		sw.moved = append(sw.moved, n)
	}
	if err := root.Rename(dbRestoringName, dbFileName); err != nil {
		sw.undo(ctx, o)
		return nil, fmt.Errorf("put the restored database in place: %w", err)
	}
	sw.swapped = true
	return sw, nil
}

// undo puts the original database files back and starts the service again.
func (sw *dbSwap) undo(ctx context.Context, o ApplyOptions) {
	if sw.swapped {
		_ = sw.root.Rename(dbFileName, dbFileName+".failed"+sw.suffix)
	}
	for _, n := range sw.moved {
		_ = sw.root.Rename(n+sw.suffix, n)
	}
	_ = sw.root.Remove(dbRestoringName)
	sw.swapped, sw.moved = false, nil
	if sw.started {
		name, args := ServiceCommand(o.Platform, "start")
		_, _ = o.Run(ctx, name, args...)
	}
}

// finish is called after the installer ran: on failure the original database comes back and the service starts.
func (sw *dbSwap) finish(ctx context.Context, o ApplyOptions, ok bool) {
	if !ok {
		sw.undo(ctx, o)
	}
}
