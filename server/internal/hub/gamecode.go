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
// (client/emulation/game_identity.cpp): an NDS 4-character game code ([A-Z0-9]) at 0x0C, or the 3DS program ID as
// 16 uppercase hex digits from a .3ds/.cci (NCSD, first partition's NCCH) or a bare .cxi (NCCH). Only header bytes
// are read. Anything unreadable, encrypted or compressed without the magic, or of another system gives "", "".
func ReadGameIdentity(path, system string) (code, kind string) {
	if path == "" || (system != "nds" && system != "3ds") {
		return "", ""
	}
	f, err := os.Open(path)
	if err != nil {
		return "", ""
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

func ndsIdentity(f *os.File) (string, string) {
	b, ok := readAt(f, 0x0C, 4)
	if !ok {
		return "", ""
	}
	for _, c := range b {
		if (c < 'A' || c > 'Z') && (c < '0' || c > '9') {
			return "", ""
		}
	}
	return string(b), GameCodeNDS
}

func ncchIdentity(f *os.File, base int64) (string, string) {
	h, ok := readAt(f, base, 0x120) // through the program ID at +0x118
	if !ok || string(h[0x100:0x104]) != "NCCH" {
		return "", ""
	}
	return fmt.Sprintf("%016X", le64(h[0x118:])), Game3DS
}

func threeDSIdentity(f *os.File) (string, string) {
	m, ok := readAt(f, 0x100, 4)
	if !ok {
		return "", ""
	}
	switch string(m) {
	case "NCCH":
		return ncchIdentity(f, 0)
	case "NCSD":
		off, ok := readAt(f, 0x120, 4) // partition table entry 0: offset in media units (0x200)
		if !ok {
			return "", ""
		}
		units := le32(off)
		if units == 0 {
			return "", ""
		}
		return ncchIdentity(f, int64(units)*0x200)
	}
	return "", ""
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
