package hub

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

// ErrImportDirMissing: the configured import folder does not exist.
var ErrImportDirMissing = errors.New("import folder does not exist")

// ImportFailure is one file that could not be imported.
type ImportFailure struct {
	File, Reason string
}

// ImportSummary is the result of ImportFolder.
type ImportSummary struct {
	Added       int
	AlreadyHere int // already in the library (same SHA-256)
	Unsupported int // extension maps to no system
	Failed      []ImportFailure
}

// ScanProgress is the state of the running or last folder scan (Library "Scanning import folder").
type ScanProgress struct {
	Running     bool
	Done, Total int // files looked at / candidate files in the folder
	Added       int
	Summary     ImportSummary // result of the last finished scan
	Finished    bool          // a scan has finished since the server started
	LastError   string
	FinishedAt  time.Time
	StartedAt   time.Time
}

type scanState struct {
	mu sync.Mutex
	p  ScanProgress
}

// ScanProgress returns a copy of the scan state.
func (s *Service) ScanProgress() ScanProgress {
	s.scan.mu.Lock()
	defer s.scan.mu.Unlock()
	return s.scan.p
}

// BeginScan reserves the single scan slot; false when a scan is already running.
func (s *Service) BeginScan() bool {
	s.scan.mu.Lock()
	defer s.scan.mu.Unlock()
	if s.scan.p.Running {
		return false
	}
	s.scan.p = ScanProgress{Running: true, StartedAt: s.now()}
	return true
}

// EndScan stores the result of a scan started with BeginScan.
func (s *Service) EndScan(sum ImportSummary, err error) {
	s.scan.mu.Lock()
	s.scan.p.Running, s.scan.p.Finished, s.scan.p.Summary, s.scan.p.FinishedAt = false, true, sum, s.now()
	s.scan.p.LastError = ""
	if err != nil {
		s.scan.p.LastError = "The scan stopped early."
		if errors.Is(err, ErrImportDirMissing) {
			s.scan.p.LastError = "The import folder does not exist."
		}
	}
	s.scan.mu.Unlock()
	s.Publish(TopicLibrary)
}

func (s *Service) scanStep(total, done, added int) {
	s.scan.mu.Lock()
	if s.scan.p.Running {
		s.scan.p.Total, s.scan.p.Done, s.scan.p.Added = total, done, added
	}
	s.scan.mu.Unlock()
	s.Publish(TopicLibrary)
}

// ImportFolder imports every file directly inside dir whose extension maps to a system into the library
// (content-addressed, like an upload; title from the file name). Files already in the library by SHA-256 and
// files with unknown extensions are skipped. Source files are only read, never moved, changed or deleted.
// Subfolders and hidden files are ignored. Files above maxBytes (0: MaxROMBytes) fail. uploadedBy must be an existing user.
func (s *Service) ImportFolder(ctx context.Context, dir, uploadedBy string, maxBytes int64) (ImportSummary, error) {
	var sum ImportSummary
	if maxBytes <= 0 {
		maxBytes = MaxROMBytes
	}
	ents, err := os.ReadDir(dir)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return sum, ErrImportDirMissing
		}
		return sum, internal(err)
	}
	systems, err := s.Systems(ctx)
	if err != nil {
		return sum, err
	}
	sort.Slice(ents, func(i, j int) bool { return ents[i].Name() < ents[j].Name() })
	total, done := 0, 0
	for _, e := range ents {
		if !strings.HasPrefix(e.Name(), ".") && !e.IsDir() {
			total++
		}
	}
	s.scanStep(total, 0, 0)
	for _, e := range ents {
		if err := ctx.Err(); err != nil {
			return sum, err
		}
		name := e.Name()
		if !strings.HasPrefix(name, ".") && !e.IsDir() {
			done++
			s.scanStep(total, done, sum.Added)
		}
		if strings.HasPrefix(name, ".") || e.IsDir() {
			continue
		}
		full := filepath.Join(dir, name)
		st, err := os.Stat(full) // follows symlinks; only regular files are imported
		if err != nil || !st.Mode().IsRegular() {
			continue
		}
		if _, ok := systemForFilename(systems, name); !ok {
			sum.Unsupported++
			continue
		}
		if st.Size() > maxBytes {
			sum.Failed = append(sum.Failed, ImportFailure{name, "File is larger than the upload limit"})
			continue
		}
		f, err := os.Open(full)
		if err != nil {
			sum.Failed = append(sum.Failed, ImportFailure{name, "Cannot read file"})
			continue
		}
		_, err = s.AddROM(ctx, f, name, "", "", uploadedBy)
		f.Close()
		var he *Error
		switch {
		case err == nil:
			sum.Added++
		case errors.Is(err, ErrConflict):
			sum.AlreadyHere++
		case errors.As(err, &he) && he.Code == CodeBadRequest:
			sum.Failed = append(sum.Failed, ImportFailure{name, he.Message})
		default:
			return sum, fmt.Errorf("import %s: %w", name, err)
		}
	}
	return sum, nil
}
