package hub

import (
	"context"
	"fmt"
	"log/slog"
	"os"
	"strings"
	"sync"
)

// Game identity kinds (same names as the Player's fb.game.kind).
const (
	GameCodeNDS = "nds-gamecode"
	Game3DS     = "3ds-titleid"
	GameCodeGBA = "gba-gamecode"
	GameTitleGB = "gb-title"

	// gameCodeNone is stored for a game whose header was read without finding an identity, so the start-up backfill
	// does not read it again. It is exposed as "".
	gameCodeNone = "none"
)

// identityReader reads the identity of one header format. Names are the "identity" values of systems/<id>.json and
// of the Player manifests (client/emulation/game_identity.cpp).
type identityReader struct {
	kind string
	read func(f *os.File) (code string, version *int)
}

var identityReaders = map[string]identityReader{
	"nds_header": {GameCodeNDS, ndsIdentity},
	"3ds_ncsd":   {Game3DS, threeDSIdentity},
	"gb_header":  {GameTitleGB, gbIdentity},
	"gba_header": {GameCodeGBA, gbaIdentity},
}

var (
	identityOnce sync.Once
	identityBy   map[string]identityReader // system id -> reader
)

// identityFor returns the reader of a system (ok false = the system has no identity).
func identityFor(system string) (identityReader, bool) {
	identityOnce.Do(func() {
		identityBy = map[string]identityReader{}
		defs, err := loadSystemDefs() // validated by the tests; the embedded files cannot change at run time
		if err != nil {
			slog.Error("system definitions", "err", err)
			return
		}
		for _, d := range defs {
			if r, ok := identityReaders[d.Identity]; ok {
				identityBy[d.ID] = r
			}
		}
	})
	r, ok := identityBy[system]
	return r, ok
}

// ReadGameIdentity reads the identity of a ROM file from its header, following the Player
// (client/emulation/game_identity.cpp), with the header format given by the system's "identity". Only header bytes
// are read. Anything unreadable, encrypted or compressed without the magic, or of another system gives "", "".
func ReadGameIdentity(path, system string) (code, kind string) {
	code, kind, _ = ReadGameIdentityVersion(path, system)
	return code, kind
}

// ReadGameIdentityVersion is ReadGameIdentity plus the ROM version from the same header (NDS byte 0x1E, 3DS NCCH
// version, GBA byte 0xBC, GB byte 0x14C). The version is nil when the identity is unknown.
func ReadGameIdentityVersion(path, system string) (code, kind string, version *int) {
	r, ok := identityFor(system)
	if path == "" || !ok {
		return "", "", nil
	}
	f, err := os.Open(path)
	if err != nil {
		return "", "", nil
	}
	defer f.Close()
	code, version = r.read(f)
	if code == "" {
		return "", "", nil
	}
	return code, r.kind, version
}

// readAt reads exactly n bytes at off; a short read (end of file) reports false.
func readAt(f *os.File, off int64, n int) ([]byte, bool) {
	b := make([]byte, n)
	got, _ := f.ReadAt(b, off)
	return b, got == n
}

func le32(b []byte) uint32 {
	return uint32(b[0]) | uint32(b[1])<<8 | uint32(b[2])<<16 | uint32(b[3])<<24
}
func le64(b []byte) uint64 { return uint64(le32(b)) | uint64(le32(b[4:]))<<32 }

func printableASCII(b []byte) bool {
	for _, c := range b {
		if c < 0x20 || c > 0x7E {
			return false
		}
	}
	return true
}

func ndsIdentity(f *os.File) (string, *int) {
	b, ok := readAt(f, 0x0C, 4)
	if !ok || !printableASCII(b) {
		return "", nil
	}
	var ver *int
	if v, ok := readAt(f, 0x1E, 1); ok {
		n := int(v[0])
		ver = &n
	}
	return string(b), ver
}

func ncchIdentity(f *os.File, base int64) (string, *int) {
	h, ok := readAt(f, base, 0x120) // through the program ID at +0x118
	if !ok || string(h[0x100:0x104]) != "NCCH" {
		return "", nil
	}
	n := int(h[0x112]) | int(h[0x113])<<8 // NCCH version, u16 little-endian
	return fmt.Sprintf("%016X", le64(h[0x118:])), &n
}

func threeDSIdentity(f *os.File) (string, *int) {
	m, ok := readAt(f, 0x100, 4)
	if !ok {
		return "", nil
	}
	switch string(m) {
	case "NCCH":
		return ncchIdentity(f, 0)
	case "NCSD":
		off, ok := readAt(f, 0x120, 4) // partition table entry 0: offset in media units (0x200)
		if !ok {
			return "", nil
		}
		units := le32(off)
		if units == 0 {
			return "", nil
		}
		return ncchIdentity(f, int64(units)*0x200)
	}
	return "", nil
}

// gbaIdentity: the 4-character game code at 0xAC (printable ASCII), version byte at 0xBC (GBATEK).
func gbaIdentity(f *os.File) (string, *int) {
	h, ok := readAt(f, 0, 0xC0)
	if !ok || !printableASCII(h[0xAC:0xB0]) {
		return "", nil
	}
	n := int(h[0xBC])
	return string(h[0xAC:0xB0]), &n
}

// headerText is the Player's text field rule: up to the first NUL, bytes outside 0x20..0x7E shown as '?', trimmed.
func headerText(b []byte) string {
	var sb strings.Builder
	for _, c := range b {
		if c == 0 {
			break
		}
		if c >= 0x20 && c <= 0x7E {
			sb.WriteByte(c)
		} else {
			sb.WriteByte('?')
		}
	}
	return strings.TrimSpace(sb.String())
}

// gbIdentity: the header title at 0x134 (11 bytes when the CGB flag at 0x143 has bit 7, else 16) is the code, only
// when the header checksum at 0x14D over 0x134..0x14C matches (Pan Docs); version byte at 0x14C.
func gbIdentity(f *os.File) (string, *int) {
	h, ok := readAt(f, 0, 0x150)
	if !ok {
		return "", nil
	}
	var sum byte
	for i := 0x134; i <= 0x14C; i++ {
		sum = sum - h[i] - 1
	}
	if sum != h[0x14D] {
		return "", nil
	}
	n := 16
	if h[0x143]&0x80 != 0 {
		n = 11
	}
	title := headerText(h[0x134 : 0x134+n])
	if title == "" {
		return "", nil
	}
	v := int(h[0x14C])
	return title, &v
}

// BackfillGameCodes reads the identity of every game that was never tried (kind empty) and stores it, or marks the
// game as tried when nothing is found, so unreadable files are not read again. It logs one summary line; failures
// are not fatal. Returns how many games got a code and how many were marked unreadable.
func (s *Service) BackfillGameCodes(ctx context.Context) (filled, unreadable int) {
	rows, err := s.db.QueryContext(ctx, `SELECT id, system, rom_sha256 FROM games WHERE game_code_kind = '' AND game_code = ''`)
	if err != nil {
		slog.Warn("game code backfill failed", "err", err)
		return 0, 0
	}
	type todo struct{ id, system, sha string }
	var list []todo
	for rows.Next() {
		var t todo
		if err := rows.Scan(&t.id, &t.system, &t.sha); err != nil {
			rows.Close()
			slog.Warn("game code backfill failed", "err", err)
			return 0, 0
		}
		list = append(list, t)
	}
	rows.Close()
	if len(list) == 0 {
		return 0, 0
	}
	failed := 0
	for _, t := range list {
		code, kind := "", gameCodeNone
		if ValidSHA256(t.sha) {
			if c, k := ReadGameIdentity(s.romPath(t.sha), t.system); k != "" {
				code, kind = c, k
			}
		}
		if _, err := s.db.ExecContext(ctx, `UPDATE games SET game_code = ?, game_code_kind = ? WHERE id = ?`, code, kind, t.id); err != nil {
			failed++
			continue
		}
		if kind == gameCodeNone {
			unreadable++
		} else {
			filled++
		}
	}
	slog.Info("game code backfill", "checked", len(list), "filled", filled, "unreadable", unreadable, "failed", failed)
	return filled, unreadable
}

// BackfillGameVersions reads the ROM version of every game that has an identity but no version yet, from a stored
// file of the game (header bytes only). Games without a readable stored file stay NULL. Returns how many were filled.
func (s *Service) BackfillGameVersions(ctx context.Context) int {
	rows, err := s.db.QueryContext(ctx, `SELECT g.id, g.system, r.sha256 FROM games g JOIN game_roms r ON r.game_id = g.id AND r.stored = 1
		WHERE g.game_version IS NULL AND g.game_code <> '' GROUP BY g.id`)
	if err != nil {
		slog.Warn("game version backfill failed", "err", err)
		return 0
	}
	type todo struct{ id, system, sha string }
	var list []todo
	for rows.Next() {
		var t todo
		if err := rows.Scan(&t.id, &t.system, &t.sha); err != nil {
			rows.Close()
			slog.Warn("game version backfill failed", "err", err)
			return 0
		}
		list = append(list, t)
	}
	rows.Close()
	filled := 0
	for _, t := range list {
		if !ValidSHA256(t.sha) {
			continue
		}
		_, _, v := ReadGameIdentityVersion(s.romPath(t.sha), t.system)
		if v == nil {
			continue
		}
		if _, err := s.db.ExecContext(ctx, `UPDATE games SET game_version = ? WHERE id = ?`, *v, t.id); err == nil {
			filled++
		}
	}
	if len(list) > 0 {
		slog.Info("game version backfill", "checked", len(list), "filled", filled)
	}
	return filled
}
