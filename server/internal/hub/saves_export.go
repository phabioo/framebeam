package hub

import (
	"archive/zip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

// SavesExportManifest is the saves.json file of a saves export (see WriteUserSavesExport).
type SavesExportManifest struct {
	User       SavesExportUser    `json:"user"`
	ExportedAt time.Time          `json:"exported_at"`
	Entries    []SavesExportEntry `json:"entries"`
}

// SavesExportUser names the owner of the exported saves.
type SavesExportUser struct {
	ID       string `json:"id"`
	Username string `json:"username"`
}

// SavesExportEntry is one file of the export: the current checkpoint of a slot or one history version.
type SavesExportEntry struct {
	GameID    string    `json:"game_id"`
	GameTitle string    `json:"game_title"`
	Slot      string    `json:"slot"`
	Kind      string    `json:"kind"`              // "current" or "history"
	Version   *int      `json:"version,omitempty"` // history version, absent for the current checkpoint
	Revision  int       `json:"revision"`
	Label     string    `json:"label,omitempty"`
	CreatedAt time.Time `json:"created_at"`
	SHA256    string    `json:"sha256"`
	Size      int64     `json:"size"`
	Path      string    `json:"path"`
}

// ExportName makes s safe as one path segment of a zip entry or a download file name: no path separators, no ".."
// segments, no control characters and no characters that Windows file names forbid. An empty result becomes fallback.
func ExportName(s, fallback string) string {
	var b strings.Builder
	for _, r := range s {
		switch {
		case r == utf8.RuneError || unicode.IsControl(r) || strings.ContainsRune(`/\:*?"<>|`, r):
			b.WriteByte('_')
		default:
			b.WriteRune(r)
		}
	}
	out := strings.TrimSpace(b.String())
	for strings.Contains(out, "..") {
		out = strings.ReplaceAll(out, "..", "_")
	}
	out = strings.Trim(out, ". ")
	if r := []rune(out); len(r) > 80 {
		out = strings.TrimRight(string(r[:80]), ". ")
	}
	if out == "" {
		return fallback
	}
	return out
}

// WriteUserSavesExport streams a zip with every save of the user to w: per game and slot the current checkpoint and every
// history version, read through the same paths as OpenSaveContent and OpenSaveVersion, plus the manifest saves.json.
// Layout: <game>/<slot>/current.sav and <game>/<slot>/history/v<version>-<label>.sav. ErrNotFound for an unknown user.
// The content is read file by file, never held in memory. A failure after the first byte aborts the stream.
func (s *Service) WriteUserSavesExport(ctx context.Context, userID string, w io.Writer) error {
	u, err := s.GetUser(ctx, userID)
	if err != nil {
		return err
	}
	slots, err := s.ListSaveSlots(ctx, u.ID)
	if err != nil {
		return err
	}
	now := time.Now().UTC()
	man := SavesExportManifest{User: SavesExportUser{ID: u.ID, Username: u.Username}, ExportedAt: now, Entries: []SavesExportEntry{}}
	zw := zip.NewWriter(w)
	used := map[string]bool{}
	gameDir := map[string]string{}
	dirTaken := map[string]string{}
	unique := func(p string) string {
		cand, i := p, 1
		for used[strings.ToLower(cand)] {
			i++
			cand = fmt.Sprintf("%s-%d", p, i)
		}
		used[strings.ToLower(cand)] = true
		return cand
	}
	add := func(name string, mod time.Time, f io.Reader) error {
		fw, err := zw.CreateHeader(&zip.FileHeader{Name: name, Method: zip.Deflate, Modified: mod})
		if err == nil {
			_, err = io.Copy(fw, f)
		}
		return err
	}
	for _, sl := range slots {
		dir, ok := gameDir[sl.GameID]
		if !ok {
			dir = ExportName(sl.GameTitle, sl.GameID)
			if owner, taken := dirTaken[strings.ToLower(dir)]; taken && owner != sl.GameID {
				dir += "-" + sl.GameID[:min(8, len(sl.GameID))]
			}
			dirTaken[strings.ToLower(dir)] = sl.GameID
			gameDir[sl.GameID] = dir
		}
		base := dir + "/" + ExportName(sl.Slot, "slot")
		f, cur, err := s.OpenSaveContent(ctx, u.ID, sl.GameID, sl.Slot)
		if err != nil {
			return err
		}
		p := unique(base + "/current")
		err = add(p+".sav", cur.CreatedAt, f)
		f.Close()
		if err != nil {
			return err
		}
		man.Entries = append(man.Entries, SavesExportEntry{GameID: sl.GameID, GameTitle: sl.GameTitle, Slot: sl.Slot, Kind: "current",
			Revision: cur.Revision, CreatedAt: cur.CreatedAt, SHA256: cur.SHA256, Size: cur.Size, Path: p + ".sav"})
		vs, err := s.ListSaveHistory(ctx, u.ID, sl.GameID, sl.Slot)
		if err != nil {
			return err
		}
		for _, v := range vs {
			label := ""
			if v.Label != nil {
				label = *v.Label
			}
			tag := label
			if tag == "" {
				tag = v.Reason
			}
			name := fmt.Sprintf("v%d", v.Version)
			if tag = ExportName(tag, ""); tag != "" {
				name += "-" + tag
			}
			f, _, err := s.OpenSaveVersion(ctx, u.ID, sl.GameID, sl.Slot, v.Version)
			if err != nil {
				return err
			}
			p := unique(base + "/history/" + name)
			err = add(p+".sav", v.CreatedAt, f)
			f.Close()
			if err != nil {
				return err
			}
			ver := v.Version
			man.Entries = append(man.Entries, SavesExportEntry{GameID: sl.GameID, GameTitle: sl.GameTitle, Slot: sl.Slot, Kind: "history",
				Version: &ver, Revision: v.Revision, Label: label, CreatedAt: v.CreatedAt, SHA256: v.SHA256, Size: v.Size, Path: p + ".sav"})
		}
	}
	mw, err := zw.CreateHeader(&zip.FileHeader{Name: "saves.json", Method: zip.Deflate, Modified: now})
	if err != nil {
		return err
	}
	enc := json.NewEncoder(mw)
	enc.SetIndent("", "  ")
	if err := enc.Encode(man); err != nil {
		return err
	}
	return zw.Close()
}
