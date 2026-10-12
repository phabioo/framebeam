package hub

import (
	"context"
	"fmt"
	"log/slog"
	"os"
)

// Game identity kinds (same names as the Player's fb.game.kind).
const (
	GameCodeNDS = "nds-gamecode"
	Game3DS     = "3ds-titleid"

	// gameCodeNone is stored for a game whose header was read without finding an identity, so the start-up backfill
	// does not read it again. It is exposed as "".
	gameCodeNone = "none"
)

// ReadGameIdentity reads the identity of a ROM file from its header, following the Player
// (client/emulation/game_identity.cpp): an NDS 4-character game code (4 printable ASCII bytes, 0x20..0x7E) at 0x0C, or the 3DS program ID as
// 16 uppercase hex digits from a .3ds/.cci (NCSD, first partition's NCCH) or a bare .cxi (NCCH). Only header bytes
// are read. Anything unreadable, encrypted or compressed without the magic, or of another system gives "", "".
func ReadGameIdentity(path, system string) (code, kind string) {
	code, kind, _ = ReadGameIdentityVersion(path, system)
	return code, kind
}

// ReadGameIdentityVersion is ReadGameIdentity plus the ROM version from the same header: the NDS header byte at
// 0x1E, or the 3DS NCCH version (u16 little-endian at NCCH+0x112). The version is nil when the identity is unknown.
func ReadGameIdentityVersion(path, system string) (code, kind string, version *int) {
	if path == "" || (system != "nds" && system != "3ds") {
		return "", "", nil
	}
	f, err := os.Open(path)
	if err != nil {
		return "", "", nil
	}
	defer f.Close()
	if system == "nds" {
		return ndsIdentity(f)
	}
	return threeDSIdentity(f)
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

func ndsIdentity(f *os.File) (string, string, *int) {
	b, ok := readAt(f, 0x0C, 4)
	if !ok {
		return "", "", nil
	}
	for _, c := range b {
		if c < 0x20 || c > 0x7E {
			return "", "", nil
		}
	}
	var ver *int
	if v, ok := readAt(f, 0x1E, 1); ok {
		n := int(v[0])
		ver = &n
	}
	return string(b), GameCodeNDS, ver
}

func ncchIdentity(f *os.File, base int64) (string, string, *int) {
	h, ok := readAt(f, base, 0x120) // through the program ID at +0x118
	if !ok || string(h[0x100:0x104]) != "NCCH" {
		return "", "", nil
	}
	n := int(h[0x112]) | int(h[0x113])<<8 // NCCH version, u16 little-endian
	return fmt.Sprintf("%016X", le64(h[0x118:])), Game3DS, &n
}

func threeDSIdentity(f *os.File) (string, string, *int) {
	m, ok := readAt(f, 0x100, 4)
	if !ok {
		return "", "", nil
	}
	switch string(m) {
	case "NCCH":
		return ncchIdentity(f, 0)
	case "NCSD":
		off, ok := readAt(f, 0x120, 4) // partition table entry 0: offset in media units (0x200)
		if !ok {
			return "", "", nil
		}
		units := le32(off)
		if units == 0 {
			return "", "", nil
		}
		return ncchIdentity(f, int64(units)*0x200)
	}
	return "", "", nil
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
