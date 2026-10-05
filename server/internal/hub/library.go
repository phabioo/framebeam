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

// System ist ein bekanntes Spielsystem. TODO(Phase 5): ersetzt durch die System-/Core-Registry.
type System struct {
	ID         string
	Name       string
	Extensions []string
}

var knownSystems = []System{{ID: "nds", Name: "Nintendo DS", Extensions: []string{".nds"}}}

// Systems liefert die bekannten Systeme.
func Systems() []System { return append([]System(nil), knownSystems...) }

// SystemName liefert den Anzeigenamen zu einer System-ID.
func SystemName(id string) (string, bool) {
	for _, s := range knownSystems {
		if s.ID == id {
			return s.Name, true
		}
	}
	return "", false
}

// SystemForFilename leitet das System aus der Dateiendung ab.
func SystemForFilename(filename string) (string, bool) {
	ext := strings.ToLower(filepath.Ext(filename))
	for _, s := range knownSystems {
		for _, e := range s.Extensions {
			if e == ext {
				return s.ID, true
			}
		}
	}
	return "", false
}

// Game ist ein Library-Eintrag; die ROM-Datei liegt unter <data-dir>/roms/<sha[:2]>/<sha>.
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

var sha256Re = regexp.MustCompile(`^[0-9a-f]{64}$`)

// ValidSHA256 prüft das Format eines ROM-Hashs (64 Hex-Zeichen, klein).
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

// AddROM streamt r in eine Temp-Datei im Datenverzeichnis, berechnet SHA-256 und legt die Datei atomar
// unter roms/<sha[:2]>/<sha> ab. system leer: aus der Dateiendung ableiten. title leer: Dateiname ohne
// Endung. Ein bereits vorhandenes ROM (gleicher Hash) ergibt einen Konfliktfehler.
func (s *Service) AddROM(ctx context.Context, r io.Reader, filename, title, system, uploadedBy string) (Game, error) {
	filename = cleanText(filepath.Base(strings.ReplaceAll(filename, `\`, "/")), 255)
	if filename == "" || filename == "." || filename == "/" {
		return Game{}, badRequest("Dateiname fehlt")
	}
	if system == "" {
		var ok bool
		if system, ok = SystemForFilename(filename); !ok {
			return Game{}, badRequest("System nicht aus der Dateiendung ableitbar")
		}
	} else if _, ok := SystemName(system); !ok {
		return Game{}, badRequest("Unbekanntes System")
	}
	title = cleanText(title, 200)
	if title == "" {
		title = strings.TrimSuffix(filename, filepath.Ext(filename))
	}
	if _, err := s.GetUser(ctx, uploadedBy); err != nil {
		if errors.Is(err, ErrNotFound) {
			return Game{}, badRequest("Uploader existiert nicht")
		}
		return Game{}, err
	}

	tmp, err := os.CreateTemp(filepath.Join(s.dataDir, "tmp"), "rom-*")
	if err != nil {
		return Game{}, internal(err)
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // nach erfolgreichem Rename wirkungslos
	h := sha256.New()
	size, err := io.Copy(io.MultiWriter(tmp, h), r)
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return Game{}, internal(err)
	}
	if size == 0 {
		return Game{}, badRequest("ROM-Datei ist leer")
	}
	sha := hex.EncodeToString(h.Sum(nil))

	var exists int
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM games WHERE rom_sha256 = ?`, sha).Scan(&exists); err != nil {
		return Game{}, internal(err)
	}
	if exists > 0 {
		return Game{}, conflict("Dieses ROM ist bereits in der Library")
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
		if isUnique(err) { // paralleler Upload desselben ROMs: Datei gehört dem anderen Eintrag
			return Game{}, conflict("Dieses ROM ist bereits in der Library")
		}
		os.Remove(dst)
		return Game{}, internal(err)
	}
	return g, nil
}

// ListGames liefert die Library nach Titel sortiert.
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

// GetGame liefert ein Spiel per ID.
func (s *Service) GetGame(ctx context.Context, id string) (Game, error) {
	return s.oneGame(ctx, `id = ?`, id)
}

// GetGameByHash liefert ein Spiel per ROM-SHA-256.
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

// OpenROM öffnet die ROM-Datei zu einem Hash; der Aufrufer schließt sie.
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

// DeleteGame entfernt Eintrag und ROM-Datei.
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

// StorageStats beschreibt die Speicherbelegung für die Library-Seite.
type StorageStats struct {
	GameCount int
	ROMBytes  int64
	// FreeBytes: freier Platz im Datenverzeichnis (0 auf Nicht-Linux).
	FreeBytes uint64
}

// Storage liefert die Speicherbelegung (Summe rom_size, freier Platz).
func (s *Service) Storage(ctx context.Context) (StorageStats, error) {
	var st StorageStats
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*), COALESCE(SUM(rom_size),0) FROM games`).Scan(&st.GameCount, &st.ROMBytes); err != nil {
		return StorageStats{}, internal(err)
	}
	st.FreeBytes = freeBytes(s.dataDir)
	return st, nil
}
