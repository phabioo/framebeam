-- 0.12 Local ROMs: a game can have several ROM files, and a file can exist only on its owners' devices.
-- game_roms: every known file of a game. stored = 1: the file is on the Hub (roms/<sha[:2]>/<sha>), 0: registered
-- by a Player as a local file only. games.rom_* stays the primary file. The Hub never stores a local-only file.
CREATE TABLE game_roms (
    sha256   TEXT PRIMARY KEY,
    game_id  TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    size     INTEGER NOT NULL,
    filename TEXT NOT NULL,
    stored   INTEGER NOT NULL DEFAULT 0,
    added_by TEXT NOT NULL,
    added_at INTEGER NOT NULL
);
CREATE INDEX game_roms_game ON game_roms(game_id);

-- rom_owners: the users who registered a file as local (they see a local-only game).
CREATE TABLE rom_owners (
    sha256  TEXT NOT NULL REFERENCES game_roms(sha256) ON DELETE CASCADE,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    PRIMARY KEY (sha256, user_id)
);
CREATE INDEX rom_owners_user ON rom_owners(user_id);

INSERT INTO game_roms(sha256, game_id, size, filename, stored, added_by, added_at)
    SELECT rom_sha256, id, rom_size, filename, 1, uploaded_by, added_at FROM games;

-- ROM version of the identity (NDS header 0x1E, 3DS NCCH version); NULL = unknown. Filled on upload/import/register
-- and once at start for existing games.
ALTER TABLE games ADD COLUMN game_version INTEGER;

-- Resumable upload sessions; the data lives at <data>/tmp/uploads/<id>. hash_state is the marshaled SHA-256 state
-- after "offset" bytes, so finishing does not re-read the file. Sessions idle for 7 days are removed.
CREATE TABLE uploads (
    id         TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    filename   TEXT NOT NULL,
    size       INTEGER NOT NULL,
    sha256     TEXT,
    title      TEXT NOT NULL DEFAULT '',
    system     TEXT NOT NULL DEFAULT '',
    "offset"   INTEGER NOT NULL DEFAULT 0,
    hash_state BLOB,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);
CREATE INDEX uploads_user ON uploads(user_id);
