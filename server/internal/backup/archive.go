package backup

import (
	"archive/tar"
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/sysmetrics"
)

// FormatVersion is the archive format written by this version.
const FormatVersion = 1

// ManifestName is the name of the manifest member (the last one).
const ManifestName = "manifest.json"

// DBMember is the name of the database snapshot member.
const DBMember = "database/uzume.db"

// Groups of an archive, in the order they are written.
const (
	GroupDatabase    = "database"
	GroupSaves       = "saves"
	GroupStates      = "states"
	GroupPlugins     = "plugins"
	GroupFirmware    = "firmware"
	GroupSettings    = "settings"
	GroupScreenshots = "screenshots"
	GroupROMs        = "roms"
)

// dataGroups maps data directories to groups. Optional groups are only written when the directory exists.
var dataGroups = []struct {
	Group, Dir string
	Optional   bool
}{
	{GroupSaves, "saves", false},
	{GroupStates, "states", true},
	{GroupPlugins, "plugins", false},
	{GroupFirmware, "firmware", false},
	{GroupSettings, "tls", false},
	{GroupScreenshots, "screenshots", true},
	{GroupROMs, "roms", false},
}

// groupLabel is the text of the progress phase.
var groupLabel = map[string]string{
	GroupDatabase: "database", GroupSaves: "saves and snapshots", GroupStates: "save states", GroupPlugins: "plugins",
	GroupFirmware: "firmware", GroupSettings: "settings", GroupScreenshots: "screenshots", GroupROMs: "ROMs",
}

// Member is one file of the archive.
type Member struct {
	Name   string `json:"name"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
	Group  string `json:"group"`
}

// Manifest describes an archive.
type Manifest struct {
	Format        int       `json:"format"`
	HubName       string    `json:"hub_name"`
	HubID         string    `json:"hub_id,omitempty"`
	HubVersion    string    `json:"hub_version"`
	SchemaVersion int       `json:"schema_version"`
	Created       time.Time `json:"created"`
	Safety        bool      `json:"safety,omitempty"`
	IncludesROMs  bool      `json:"includes_roms"`
	Groups        []string  `json:"groups"`
	TotalSize     int64     `json:"total_size"`
	// Skipped counts files that vanished while the backup ran (for example removed by the save history sweep).
	Skipped int      `json:"skipped,omitempty"`
	Members []Member `json:"members"`
}

// HasGroup reports whether the archive holds the group.
func (m *Manifest) HasGroup(g string) bool {
	for _, x := range m.Groups {
		if x == g {
			return true
		}
	}
	return false
}

var groupShort = map[string]string{GroupSaves: "saves", GroupStates: "states", GroupPlugins: "plugins", GroupFirmware: "firmware", GroupSettings: "settings", GroupScreenshots: "screenshots"}

// ContentsText is the "Database · saves · states · plugins" text of the list.
func (m *Manifest) ContentsText() string {
	parts := []string{"Database"}
	for _, g := range []string{GroupSaves, GroupStates, GroupPlugins, GroupFirmware, GroupSettings, GroupScreenshots} {
		if m.HasGroup(g) {
			parts = append(parts, groupShort[g])
		}
	}
	if m.HasGroup(GroupROMs) {
		parts = append(parts, "ROMs")
	}
	return strings.Join(parts, " · ")
}

// ---- names ----

var nameRe = regexp.MustCompile(`^uzume-server-([a-z0-9][a-z0-9-]*)-(\d{8}T\d{6}Z)(-safety)?\.tar$`)

const tsLayout = "20060102T150405Z"

// Slug turns a server name into the part of a file name: lower case letters and digits, other runs become one dash.
func Slug(name string) string {
	var b strings.Builder
	dash := false
	for _, r := range strings.ToLower(name) {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') {
			b.WriteRune(r)
			dash = false
		} else if !dash && b.Len() > 0 {
			b.WriteByte('-')
			dash = true
		}
	}
	s := strings.Trim(b.String(), "-")
	if len(s) > 40 {
		s = strings.Trim(s[:40], "-")
	}
	if s == "" {
		return "hub"
	}
	return s
}

// FileName is uzume-server-<slug>-<UTC timestamp>[-safety].tar.
func FileName(hubName string, t time.Time, safety bool) string {
	n := "uzume-server-" + Slug(hubName) + "-" + t.UTC().Format(tsLayout)
	if safety {
		n += "-safety"
	}
	return n + ".tar"
}

// ParseName reports whether name is an archive file name and returns its timestamp and kind.
func ParseName(name string) (ts time.Time, safety, ok bool) {
	m := nameRe.FindStringSubmatch(name)
	if m == nil {
		return time.Time{}, false, false
	}
	t, err := time.Parse(tsLayout, m[2])
	if err != nil {
		return time.Time{}, false, false
	}
	return t, m[3] != "", true
}

// SafeName reports whether name is a plain backup file name: it matches the naming scheme and has no
// path separators or parent references.
func SafeName(name string) bool {
	if strings.Contains(name, "..") || strings.ContainsAny(name, `/\`) || !filepath.IsLocal(name) || name != filepath.Base(name) {
		return false
	}
	_, _, ok := ParseName(name)
	return ok
}

// ---- errors ----

// NoSpaceError: the target has too little free space. The server never deletes backups to make room (decision 26).
type NoSpaceError struct {
	Dir        string
	Need, Free uint64
}

func (e *NoSpaceError) Error() string {
	return fmt.Sprintf("Not enough space on %s: needs %s, %s free.", e.Dir, FormatBytes(e.Need), FormatBytes(e.Free))
}

// NotWritableError: the target folder cannot be written.
type NotWritableError struct {
	Dir string
	Err error
}

func (e *NotWritableError) Error() string { return fmt.Sprintf("Cannot write to %s: %v", e.Dir, e.Err) }
func (e *NotWritableError) Unwrap() error { return e.Err }

// ChecksumError: a member does not match the manifest.
type ChecksumError struct{ Member, Group string }

func (e *ChecksumError) Error() string {
	g := e.Group
	if g == "" {
		g = e.Member
	}
	return "checksum mismatch in " + g + " (" + e.Member + ")"
}

// DamagedError: the file is not a readable archive.
type DamagedError struct{ Reason string }

func (e *DamagedError) Error() string { return "The backup file is damaged (" + e.Reason + ")." }

// NewerError: the archive was made by a newer server than this one.
type NewerError struct {
	Schema, Known int
	Format        bool
}

func (e *NewerError) Error() string {
	if e.Format {
		return "This backup was made by a newer Uzume server (archive format). Install the matching server version first."
	}
	return fmt.Sprintf("This backup was made by a newer Uzume server (database schema %d, this server knows %d). Install the matching server version first.", e.Schema, e.Known)
}

// FormatBytes is a short size text ("1.4 GB").
func FormatBytes(n uint64) string {
	const unit = 1024.0
	if n < 1024 {
		return fmt.Sprintf("%d B", n)
	}
	v := float64(n)
	for _, s := range []string{"B", "KB", "MB", "GB", "TB"} {
		if v < unit || s == "TB" {
			return fmt.Sprintf("%.1f %s", v, s)
		}
		v /= unit
	}
	return ""
}

// ---- space and folder checks ----

// FreeFunc returns the free bytes of the volume holding dir.
type FreeFunc func(dir string) (free uint64, ok bool)

// DiskFree is the default FreeFunc.
func DiskFree(dir string) (uint64, bool) {
	_, free, ok := sysmetrics.Disk(dir)
	return free, ok
}

// CheckWritable creates dir if needed and proves that a file can be written there.
func CheckWritable(dir string) error {
	if err := os.MkdirAll(dir, 0o750); err != nil {
		return &NotWritableError{dir, err}
	}
	f, err := os.CreateTemp(dir, ".fb-write-test-*")
	if err != nil {
		return &NotWritableError{dir, err}
	}
	name := f.Name()
	_, werr := f.Write([]byte("x"))
	cerr := f.Close()
	os.Remove(name)
	if werr != nil {
		return &NotWritableError{dir, werr}
	}
	if cerr != nil {
		return &NotWritableError{dir, cerr}
	}
	return nil
}

// ---- create ----

// Extra is a file outside the data directory that goes into config/.
type Extra struct{ Name, Path string }

// Progress is reported while a backup or restore runs.
type Progress struct {
	Phase       string
	Done, Total int64
}

// Options for Create.
type Options struct {
	DataDir, TargetDir         string
	HubName, HubID, HubVersion string
	SchemaVersion              int
	Safety, IncludeROMs        bool
	// Snapshot writes a consistent copy of the database to dest (the file does not exist yet).
	Snapshot func(dest string) error
	Extras   []Extra
	// Lock, when set, is called around the database snapshot and the file listing; it returns the unlock function.
	Lock     func() func()
	Now      func() time.Time
	Progress func(Progress)
	Free     FreeFunc
}

// Result of Create.
type Result struct {
	Path, Name string
	Size       int64
	Manifest   Manifest
	Took       time.Duration
}

type entry struct {
	name, abs, group string
	size             int64
	dir              bool
}

// Create writes a backup archive into opt.TargetDir. It writes to a hidden partial file and renames it when
// complete; on any error or cancellation the partial file is removed and nothing else is touched.
func Create(ctx context.Context, opt Options) (res Result, err error) {
	start := time.Now()
	now := opt.Now
	if now == nil {
		now = time.Now
	}
	free := opt.Free
	if free == nil {
		free = DiskFree
	}
	report := func(p Progress) {
		if opt.Progress != nil {
			opt.Progress(p)
		}
	}
	if err := CheckWritable(opt.TargetDir); err != nil {
		return res, err
	}
	report(Progress{Phase: groupLabel[GroupDatabase]})

	tmpDir := filepath.Join(opt.DataDir, "tmp")
	if err := os.MkdirAll(tmpDir, 0o750); err != nil {
		return res, err
	}
	dbTmp, err := os.MkdirTemp(tmpDir, "backup-db-*")
	if err != nil {
		return res, err
	}
	defer os.RemoveAll(dbTmp)
	dbFile := filepath.Join(dbTmp, "uzume.db")

	unlock := func() {}
	if opt.Lock != nil {
		unlock = opt.Lock()
	}
	// Snapshot first, then list the files: a blob stored after the snapshot is an orphan (harmless), a blob the
	// snapshot references is already on disk.
	serr := opt.Snapshot(dbFile)
	var entries []entry
	var total int64
	if serr == nil {
		entries, total, serr = plan(opt, dbFile)
	}
	unlock()
	if serr != nil {
		return res, fmt.Errorf("database snapshot: %w", serr)
	}
	if err := ctx.Err(); err != nil {
		return res, err
	}

	need := uint64(total) + uint64(len(entries))*1536 + 1<<20
	if f, ok := free(opt.TargetDir); ok && f < need {
		return res, &NoSpaceError{Dir: opt.TargetDir, Need: need, Free: f}
	}

	created := now().UTC().Truncate(time.Second)
	final := filepath.Join(opt.TargetDir, FileName(opt.HubName, created, opt.Safety))
	for i := 0; ; i++ { // two backups in one second: move on by a second
		if _, e := os.Lstat(final); errors.Is(e, os.ErrNotExist) {
			break
		}
		created = created.Add(time.Second)
		final = filepath.Join(opt.TargetDir, FileName(opt.HubName, created, opt.Safety))
		if i > 100 {
			return res, errors.New("no free backup file name")
		}
	}
	partial := filepath.Join(opt.TargetDir, "."+filepath.Base(final)+".partial")
	f, err := os.OpenFile(partial, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o600)
	if err != nil {
		return res, &NotWritableError{opt.TargetDir, err}
	}
	defer func() {
		if err != nil {
			f.Close()
			os.Remove(partial)
		}
	}()
	bw := bufio.NewWriterSize(f, 1<<20)
	tw := tar.NewWriter(bw)

	m := Manifest{Format: FormatVersion, HubName: opt.HubName, HubID: opt.HubID, HubVersion: opt.HubVersion,
		SchemaVersion: opt.SchemaVersion, Created: created, Safety: opt.Safety, IncludesROMs: opt.IncludeROMs}
	seen := map[string]bool{}
	var done int64
	lastReport := time.Time{}
	for _, e := range entries {
		if err = ctx.Err(); err != nil {
			return res, err
		}
		if !seen[e.group] {
			seen[e.group] = true
			m.Groups = append(m.Groups, e.group)
			report(Progress{Phase: groupLabel[e.group], Done: done, Total: total})
		}
		if e.dir {
			if err = tw.WriteHeader(&tar.Header{Typeflag: tar.TypeDir, Name: e.name + "/", Mode: 0o750, ModTime: created}); err != nil {
				return res, err
			}
			continue
		}
		group := e.group
		mem, skipped, cerr := addFile(ctx, tw, e, func(n int64) {
			done += n
			if time.Since(lastReport) > 200*time.Millisecond {
				lastReport = time.Now()
				report(Progress{Phase: groupLabel[group], Done: done, Total: total})
			}
		})
		if cerr != nil {
			err = cerr
			return res, err
		}
		if skipped {
			m.Skipped++
			continue
		}
		m.Members = append(m.Members, mem)
		m.TotalSize += mem.Size
	}
	report(Progress{Phase: "finishing", Done: total, Total: total})
	js, err := json.MarshalIndent(m, "", " ")
	if err != nil {
		return res, err
	}
	if err = tw.WriteHeader(&tar.Header{Typeflag: tar.TypeReg, Name: ManifestName, Mode: 0o600, Size: int64(len(js)), ModTime: created}); err != nil {
		return res, err
	}
	if _, err = tw.Write(js); err != nil {
		return res, err
	}
	if err = tw.Close(); err != nil {
		return res, err
	}
	if err = bw.Flush(); err != nil {
		return res, err
	}
	if err = f.Sync(); err != nil {
		return res, err
	}
	if err = f.Close(); err != nil {
		return res, err
	}
	if err = os.Rename(partial, final); err != nil {
		return res, err
	}
	var size int64
	if st, e := os.Stat(final); e == nil {
		size = st.Size()
	}
	return Result{Path: final, Name: filepath.Base(final), Size: size, Manifest: m, Took: time.Since(start)}, nil
}

// plan lists everything that goes into the archive and the number of payload bytes.
func plan(opt Options, dbFile string) ([]entry, int64, error) {
	var out []entry
	var total int64
	st, err := os.Stat(dbFile)
	if err != nil {
		return nil, 0, err
	}
	out = append(out, entry{name: DBMember, abs: dbFile, group: GroupDatabase, size: st.Size()})
	total += st.Size()
	target, _ := filepath.Abs(opt.TargetDir)
	for _, g := range dataGroups {
		if g.Group == GroupROMs && !opt.IncludeROMs {
			continue
		}
		root := filepath.Join(opt.DataDir, g.Dir)
		if _, err := os.Stat(root); err != nil && (g.Optional || g.Group == GroupROMs) {
			continue
		}
		out = append(out, entry{name: "data/" + g.Dir, abs: root, group: g.Group, dir: true})
		err := filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
			if err != nil {
				if errors.Is(err, os.ErrNotExist) {
					return nil
				}
				return err
			}
			if p == root {
				return nil
			}
			if abs, _ := filepath.Abs(p); abs == target {
				return filepath.SkipDir
			}
			rel, _ := filepath.Rel(opt.DataDir, p)
			name := "data/" + filepath.ToSlash(rel)
			switch {
			case d.IsDir():
				out = append(out, entry{name: name, abs: p, group: g.Group, dir: true})
			case d.Type().IsRegular():
				info, err := d.Info()
				if err != nil {
					return nil // vanished
				}
				out = append(out, entry{name: name, abs: p, group: g.Group, size: info.Size()})
				total += info.Size()
			}
			return nil
		})
		if err != nil {
			return nil, 0, err
		}
	}
	for _, x := range opt.Extras {
		info, err := os.Stat(x.Path)
		if err != nil || !info.Mode().IsRegular() {
			continue // optional: absent files are left out
		}
		f, err := os.Open(x.Path)
		if err != nil {
			continue // unreadable for the server user
		}
		f.Close()
		out = append(out, entry{name: "config/" + path.Base(x.Name), abs: x.Path, group: GroupSettings, size: info.Size()})
		total += info.Size()
	}
	return out, total, nil
}

type ctxReader struct {
	ctx context.Context
	r   io.Reader
}

func (c ctxReader) Read(p []byte) (int, error) {
	if err := c.ctx.Err(); err != nil {
		return 0, err
	}
	return c.r.Read(p)
}

// addFile copies one file into the archive. A file that vanished is skipped; a file that shrank is an error.
func addFile(ctx context.Context, tw *tar.Writer, e entry, tick func(int64)) (Member, bool, error) {
	f, err := os.Open(e.abs)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return Member{}, true, nil
		}
		return Member{}, false, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return Member{}, false, err
	}
	size := info.Size() // fixed now; a file that grows later is cut here (the next backup has the rest)
	hdr := &tar.Header{Typeflag: tar.TypeReg, Name: e.name, Mode: int64(info.Mode().Perm()), Size: size, ModTime: info.ModTime()}
	if err := tw.WriteHeader(hdr); err != nil {
		return Member{}, false, err
	}
	h := sha256.New()
	n, err := io.CopyN(io.MultiWriter(tw, h, tickWriter(tick)), ctxReader{ctx, f}, size)
	if err != nil {
		if errors.Is(err, io.EOF) {
			return Member{}, false, fmt.Errorf("%s changed while it was read", e.name)
		}
		return Member{}, false, err
	}
	return Member{Name: e.name, Size: n, SHA256: hex.EncodeToString(h.Sum(nil)), Group: e.group}, false, nil
}

type tickWriter func(int64)

func (p tickWriter) Write(b []byte) (int, error) { p(int64(len(b))); return len(b), nil }

// ---- read ----

// ReadManifest reads the manifest of an archive without reading the payload (header scan with seeks).
func ReadManifest(file string) (Manifest, error) {
	f, err := os.Open(file)
	if err != nil {
		return Manifest{}, err
	}
	defer f.Close()
	tr := tar.NewReader(f)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			return Manifest{}, &DamagedError{"manifest missing"}
		}
		if err != nil {
			return Manifest{}, &DamagedError{"unreadable archive"}
		}
		if h.Name != ManifestName {
			continue
		}
		if h.Size > 256<<20 {
			return Manifest{}, &DamagedError{"manifest too large"}
		}
		var m Manifest
		if err := json.NewDecoder(io.LimitReader(tr, h.Size)).Decode(&m); err != nil {
			return Manifest{}, &DamagedError{"manifest unreadable"}
		}
		return m, nil
	}
}

// Info is a backup file found in a folder.
type Info struct {
	Name, Path string
	Size       int64
	Time       time.Time // from the manifest, else from the file name
	Safety     bool
	Manifest   *Manifest // nil when unreadable
	Err        string
}

// List returns the archives in dir, newest first. Files that do not look like archives are ignored; an archive
// whose manifest cannot be read is listed with Err.
func List(dir string) ([]Info, error) {
	ents, err := os.ReadDir(dir)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil, nil
		}
		return nil, err
	}
	var out []Info
	for _, e := range ents {
		ts, safety, ok := ParseName(e.Name())
		if !ok || !e.Type().IsRegular() {
			continue
		}
		info, err := e.Info()
		if err != nil {
			continue
		}
		it := Info{Name: e.Name(), Path: filepath.Join(dir, e.Name()), Size: info.Size(), Time: ts, Safety: safety}
		if m, err := ReadManifest(it.Path); err != nil {
			it.Err = err.Error()
		} else {
			it.Manifest = &m
			if !m.Created.IsZero() {
				it.Time = m.Created
			}
		}
		out = append(out, it)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Time.After(out[j].Time) })
	return out, nil
}

// Find returns the path of the archive called name in dir (name must be a plain archive file name).
func Find(dir, name string) (string, error) {
	if !SafeName(name) {
		return "", os.ErrNotExist
	}
	if strings.Contains(name, "..") || strings.ContainsAny(name, `/\`) || !filepath.IsLocal(name) {
		return "", os.ErrNotExist
	}
	p := filepath.Join(dir, filepath.Base(name))
	if st, err := os.Stat(p); err != nil || !st.Mode().IsRegular() {
		return "", os.ErrNotExist
	}
	return p, nil
}

// ---- verify and extract ----

// stageRel maps an archive member name to a path below the staging directory.
func stageRel(name string) (string, bool) {
	name = strings.TrimSuffix(name, "/")
	if name == "" || strings.HasPrefix(name, "/") || strings.Contains(name, `\`) || path.Clean(name) != name {
		return "", false
	}
	for _, seg := range strings.Split(name, "/") {
		if seg == ".." || seg == "" {
			return "", false
		}
	}
	switch {
	case name == DBMember:
		return "uzume.db", true
	case strings.HasPrefix(name, "data/"):
		rest := name[len("data/"):]
		top, _, _ := strings.Cut(rest, "/")
		for _, g := range dataGroups {
			if g.Dir == top {
				return rest, true
			}
		}
	case strings.HasPrefix(name, "config/"):
		rest := name[len("config/"):]
		if rest != "" && !strings.Contains(rest, "/") {
			return "restored-config/" + rest, true
		}
	}
	return "", false
}

// ExtractOptions for Extract.
type ExtractOptions struct {
	// MaxSchema is the newest database schema this server knows; a newer archive is rejected before anything is read.
	MaxSchema int
	// VerifyOnly checks every checksum and writes nothing.
	VerifyOnly bool
	Free       FreeFunc
	Progress   func(Progress)
}

// Extract verifies the archive and unpacks it into staging (below the data directory, so that the later swap is a
// rename). Every member is compared with the manifest while it is read. On any error staging is removed and the
// live data was never touched. It returns the manifest and the top-level items of staging.
func Extract(ctx context.Context, file, staging string, opt ExtractOptions) (Manifest, []string, error) {
	m, err := ReadManifest(file)
	if err != nil {
		return m, nil, err
	}
	if m.Format > FormatVersion {
		return m, nil, &NewerError{Format: true}
	}
	if opt.MaxSchema > 0 && m.SchemaVersion > opt.MaxSchema {
		return m, nil, &NewerError{Schema: m.SchemaVersion, Known: opt.MaxSchema}
	}
	if !opt.VerifyOnly {
		free := opt.Free
		if free == nil {
			free = DiskFree
		}
		parent := filepath.Dir(staging)
		if err := os.MkdirAll(parent, 0o750); err != nil {
			return m, nil, err
		}
		need := uint64(m.TotalSize) + 1<<20
		if f, ok := free(parent); ok && f < need {
			return m, nil, &NoSpaceError{Dir: parent, Need: need, Free: f}
		}
		os.RemoveAll(staging)
		if err := os.MkdirAll(staging, 0o750); err != nil {
			return m, nil, err
		}
	}
	items, err := extract(ctx, file, staging, &m, opt)
	if err != nil {
		if !opt.VerifyOnly {
			os.RemoveAll(staging)
		}
		return m, nil, err
	}
	return m, items, nil
}

func extract(ctx context.Context, file, staging string, m *Manifest, opt ExtractOptions) ([]string, error) {
	want := make(map[string]Member, len(m.Members))
	for _, x := range m.Members {
		if _, ok := stageRel(x.Name); !ok {
			return nil, &DamagedError{"unexpected member " + x.Name}
		}
		want[x.Name] = x
	}
	f, err := os.Open(file)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	tr := tar.NewReader(bufio.NewReaderSize(f, 1<<20))
	got := map[string]bool{}
	var done int64
	last := time.Time{}
	if !opt.VerifyOnly {
		for _, g := range dataGroups { // groups of the archive exist in staging even when empty
			if m.HasGroup(g.Group) {
				if err := os.MkdirAll(filepath.Join(staging, g.Dir), 0o750); err != nil {
					return nil, err
				}
			}
		}
	}
	for {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		h, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, &DamagedError{"unreadable archive"}
		}
		if h.Name == ManifestName {
			continue
		}
		rel, ok := stageRel(h.Name)
		if !ok {
			return nil, &DamagedError{"unexpected member " + h.Name}
		}
		switch h.Typeflag {
		case tar.TypeDir:
			if !opt.VerifyOnly {
				if err := os.MkdirAll(filepath.Join(staging, filepath.FromSlash(rel)), 0o750); err != nil {
					return nil, err
				}
			}
			continue
		case tar.TypeReg:
		default:
			return nil, &DamagedError{"unexpected entry type in " + h.Name}
		}
		mem, ok := want[h.Name]
		if !ok || got[h.Name] {
			return nil, &DamagedError{"member " + h.Name + " is not in the manifest"}
		}
		got[h.Name] = true
		if h.Size != mem.Size {
			return nil, &ChecksumError{Member: h.Name, Group: mem.Group}
		}
		hash := sha256.New()
		var dst io.Writer = hash
		var out *os.File
		if !opt.VerifyOnly {
			p := filepath.Join(staging, filepath.FromSlash(rel))
			if err := os.MkdirAll(filepath.Dir(p), 0o750); err != nil {
				return nil, err
			}
			out, err = os.OpenFile(p, os.O_WRONLY|os.O_CREATE|os.O_EXCL, os.FileMode(h.Mode&0o777)|0o600)
			if err != nil {
				return nil, err
			}
			dst = io.MultiWriter(out, hash)
		}
		n, cerr := io.Copy(dst, ctxReader{ctx, tr})
		if out != nil {
			if e := out.Close(); cerr == nil {
				cerr = e
			}
		}
		if cerr != nil {
			if errors.Is(cerr, context.Canceled) || errors.Is(cerr, context.DeadlineExceeded) {
				return nil, cerr
			}
			return nil, &DamagedError{"unreadable archive"}
		}
		if n != mem.Size || hex.EncodeToString(hash.Sum(nil)) != mem.SHA256 {
			return nil, &ChecksumError{Member: h.Name, Group: mem.Group}
		}
		done += n
		if opt.Progress != nil && time.Since(last) > 200*time.Millisecond {
			last = time.Now()
			opt.Progress(Progress{Phase: "verifying " + groupLabel[mem.Group], Done: done, Total: m.TotalSize})
		}
	}
	for name, mem := range want {
		if !got[name] {
			return nil, &DamagedError{"member " + name + " is missing (" + mem.Group + ")"}
		}
	}
	if opt.VerifyOnly {
		return nil, nil
	}
	if _, err := os.Stat(filepath.Join(staging, "uzume.db")); err != nil {
		return nil, &DamagedError{"database missing"}
	}
	ents, err := os.ReadDir(staging)
	if err != nil {
		return nil, err
	}
	var items []string
	for _, e := range ents {
		items = append(items, e.Name())
	}
	sort.Strings(items)
	return items, nil
}
