package hub

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"errors"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
	"unicode"

	"github.com/google/uuid"
)

// System is a known game system (library view of the registry, see systems.go).
type System struct {
	ID         string
	Name       string
	Extensions []string
}

// Systems returns the known systems (registry order by ID).
func (s *Service) Systems(ctx context.Context) ([]System, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT id, display_name, extensions FROM systems ORDER BY id`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	var out []System
	for rows.Next() {
		var sys System
		var ext string
		if err := rows.Scan(&sys.ID, &sys.Name, &ext); err != nil {
			return nil, internal(err)
		}
		sys.Extensions = splitList(ext)
		out = append(out, sys)
	}
	return out, rows.Err()
}

// systemForFilename derives the system from the file extension.
func systemForFilename(systems []System, filename string) (string, bool) {
	ext := strings.ToLower(filepath.Ext(filename))
	for _, s := range systems {
		for _, e := range s.Extensions {
			if e == ext {
				return s.ID, true
			}
		}
	}
	return "", false
}

// Game is a library entry; the ROM file lives at <data-dir>/roms/<sha[:2]>/<sha>.
type Game struct {
	ID         string
	Title      string
	System     string
	ROMSHA256  string
	ROMSize    int64
	Filename   string
	UploadedBy string
	AddedAt    time.Time
}

// MaxROMBytes is the upload limit for ROMs (4 GiB), shared by the web interface and the API.
const MaxROMBytes int64 = 4 << 30

var sha256Re = regexp.MustCompile(`^[0-9a-f]{64}$`)

// ValidSHA256 checks the format of a ROM hash (64 lowercase hex characters).
func ValidSHA256(s string) bool { return sha256Re.MatchString(s) }

func (s *Service) romPath(sha string) string {
	return filepath.Join(s.dataDir, "roms", sha[:2], sha)
}

const gameCols = `id, title, system, rom_sha256, rom_size, filename, uploaded_by, added_at`

func scanGame(r scanner) (Game, error) {
	var g Game
	var added int64
	if err := r.Scan(&g.ID, &g.Title, &g.System, &g.ROMSHA256, &g.ROMSize, &g.Filename, &g.UploadedBy, &added); err != nil {
		return Game{}, err
	}
	g.AddedAt = time.Unix(added, 0).UTC()
	return g, nil
}

func cleanText(v string, max int) string {
	v = strings.Map(func(r rune) rune {
		if unicode.IsControl(r) {
			return -1
		}
		return r
	}, v)
	v = strings.TrimSpace(v)
	if len(v) > max {
		v = strings.TrimSpace(v[:max])
	}
	return v
}

// AddROM streams r into a temp file in the data directory, computes SHA-256 and places the file atomically
// at roms/<sha[:2]>/<sha>. Empty system: derive from the file extension. Empty title: file name without
// extension. A ROM that already exists (same hash) results in a conflict error.
func (s *Service) AddROM(ctx context.Context, r io.Reader, filename, title, system, uploadedBy string) (Game, error) {
	filename = cleanText(filepath.Base(strings.ReplaceAll(filename, `\`, "/")), 255)
	if filename == "" || filename == "." || filename == "/" {
		return Game{}, badRequest("File name missing")
	}
	systems, err := s.Systems(ctx)
	if err != nil {
		return Game{}, err
	}
	if system == "" {
		var ok bool
		if system, ok = systemForFilename(systems, filename); !ok {
			return Game{}, badRequest("System cannot be derived from the file extension")
		}
	} else {
		known := false
		for _, sys := range systems {
			known = known || sys.ID == system
		}
		if !known {
			return Game{}, badRequest("Unknown system")
		}
	}
	title = cleanText(title, 200)
	if title == "" {
		title = strings.TrimSuffix(filename, filepath.Ext(filename))
	}
	if _, err := s.GetUser(ctx, uploadedBy); err != nil {
		if errors.Is(err, ErrNotFound) {
			return Game{}, badRequest("Uploader does not exist")
		}
		return Game{}, err
	}

	tmp, err := os.CreateTemp(filepath.Join(s.dataDir, "tmp"), "rom-*")
	if err != nil {
		return Game{}, internal(err)
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // no effect after a successful rename
	h := sha256.New()
	size, err := io.Copy(io.MultiWriter(tmp, h), r)
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return Game{}, internal(err)
	}
	if size == 0 {
		return Game{}, badRequest("ROM file is empty")
	}
	sha := hex.EncodeToString(h.Sum(nil))

	var exists int
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM games WHERE rom_sha256 = ?`, sha).Scan(&exists); err != nil {
		return Game{}, internal(err)
	}
	if exists > 0 {
		return Game{}, s.duplicateROM(ctx, sha)
	}
	dst := s.romPath(sha)
	if err := os.MkdirAll(filepath.Dir(dst), 0o750); err != nil {
		return Game{}, internal(err)
	}
	if err := os.Chmod(tmpName, 0o640); err != nil {
		return Game{}, internal(err)
	}
	if err := os.Rename(tmpName, dst); err != nil {
		return Game{}, internal(err)
	}
	g := Game{ID: uuid.NewString(), Title: title, System: system, ROMSHA256: sha, ROMSize: size, Filename: filename,
		UploadedBy: uploadedBy, AddedAt: s.Now().Truncate(time.Second)}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO games(`+gameCols+`) VALUES (?,?,?,?,?,?,?,?)`,
		g.ID, g.Title, g.System, g.ROMSHA256, g.ROMSize, g.Filename, g.UploadedBy, g.AddedAt.Unix()); err != nil {
		if isUnique(err) { // parallel upload of the same ROM: the file belongs to the other entry
			return Game{}, s.duplicateROM(ctx, sha)
		}
		os.Remove(dst)
		return Game{}, internal(err)
	}
	return g, nil
}

// duplicateROM builds the conflict error for a ROM that is already in the library (with the existing game ID).
func (s *Service) duplicateROM(ctx context.Context, sha string) error {
	e := conflict("This ROM is already in the library")
	if g, err := s.GetGameByHash(ctx, sha); err == nil {
		e.ExistingGameID = g.ID
	}
	return e
}

// ListGames returns the library sorted by title.
func (s *Service) ListGames(ctx context.Context) ([]Game, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT `+gameCols+` FROM games ORDER BY title COLLATE NOCASE, added_at`)
	if err != nil {
		return nil, internal(err)
	}
	defer rows.Close()
	out := []Game{}
	for rows.Next() {
		g, err := scanGame(rows)
		if err != nil {
			return nil, internal(err)
		}
		out = append(out, g)
	}
	return out, rows.Err()
}

// GetGame returns a game by ID.
func (s *Service) GetGame(ctx context.Context, id string) (Game, error) {
	return s.oneGame(ctx, `id = ?`, id)
}

// GetGameByHash returns a game by ROM SHA-256.
func (s *Service) GetGameByHash(ctx context.Context, sha string) (Game, error) {
	return s.oneGame(ctx, `rom_sha256 = ?`, sha)
}

func (s *Service) oneGame(ctx context.Context, where string, arg string) (Game, error) {
	g, err := scanGame(s.db.QueryRowContext(ctx, `SELECT `+gameCols+` FROM games WHERE `+where, arg))
	if errors.Is(err, sql.ErrNoRows) {
		return Game{}, ErrNotFound
	}
	if err != nil {
		return Game{}, internal(err)
	}
	return g, nil
}

// OpenROM opens the ROM file for a hash; the caller closes it.
func (s *Service) OpenROM(ctx context.Context, sha string) (*os.File, Game, error) {
	if !ValidSHA256(sha) {
		return nil, Game{}, ErrNotFound
	}
	g, err := s.GetGameByHash(ctx, sha)
	if err != nil {
		return nil, Game{}, err
	}
	f, err := os.Open(s.romPath(sha))
	if errors.Is(err, os.ErrNotExist) {
		return nil, Game{}, ErrNotFound
	}
	if err != nil {
		return nil, Game{}, internal(err)
	}
	return f, g, nil
}

// DeleteGame removes the entry and the ROM file.
func (s *Service) DeleteGame(ctx context.Context, id string) error {
	g, err := s.GetGame(ctx, id)
	if err != nil {
		return err
	}
	if _, err := s.db.ExecContext(ctx, `DELETE FROM games WHERE id = ?`, g.ID); err != nil {
		return internal(err)
	}
	if err := os.Remove(s.romPath(g.ROMSHA256)); err != nil && !errors.Is(err, os.ErrNotExist) {
		return internal(err)
	}
	return nil
}

// StorageStats describes storage usage for the Library page.
type StorageStats struct {
	GameCount int
	ROMBytes  int64
	// FreeBytes: free space in the data directory (0 on non-Linux).
	FreeBytes uint64
}

// Storage returns storage usage (sum of rom_size, free space).
func (s *Service) Storage(ctx context.Context) (StorageStats, error) {
	var st StorageStats
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*), COALESCE(SUM(rom_size),0) FROM games`).Scan(&st.GameCount, &st.ROMBytes); err != nil {
		return StorageStats{}, internal(err)
	}
	st.FreeBytes = freeBytes(s.dataDir)
	return st, nil
}
