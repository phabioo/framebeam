package hub

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
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
	for _, e := range ents {
		if err := ctx.Err(); err != nil {
			return sum, err
		}
		name := e.Name()
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
