-- Package 7 (HC-2): deleting a game keeps its saves (no foreign key, 0002_saves.sql). When the same ROM is added
-- again, the saves must find their game again: the identity of a removed game that still has saves is kept here, and
-- the game created for the same ROM re-uses the old id (docs/architecture/03-saves.md, ADR 0025 amendment).
-- removed_games: identity as used by local ROMs (system + game code + kind + ROM version); game_code = '' when the
-- game had none (then only a file hash can bring it back).
CREATE TABLE removed_games (
    game_id        TEXT PRIMARY KEY,
    system         TEXT NOT NULL,
    game_code      TEXT NOT NULL DEFAULT '',
    game_code_kind TEXT NOT NULL DEFAULT '',
    game_version   INTEGER,
    removed_at     INTEGER NOT NULL
);

-- removed_game_roms: the SHA-256 of every file the removed game had (stored or only registered).
CREATE TABLE removed_game_roms (
    sha256  TEXT PRIMARY KEY,
    game_id TEXT NOT NULL REFERENCES removed_games(game_id) ON DELETE CASCADE
);
