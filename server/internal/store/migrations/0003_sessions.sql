-- Sessions (phase 4, ADR 0006). Live connection state stays in memory; these tables hold metadata only.

-- Codec capabilities reported by the last handshake (NULL = not reported yet).
ALTER TABLE devices ADD COLUMN h264_encode INTEGER;
ALTER TABLE devices ADD COLUMN h264_decode INTEGER;

-- A Session belongs to one owner device and one game. game_id has no foreign key on purpose
-- (deleting a game from the library must not fail because of a diagnostic row); the title is kept.
CREATE TABLE sessions (
    id              TEXT PRIMARY KEY,
    game_id         TEXT NOT NULL,
    game_title      TEXT NOT NULL,
    owner_device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    owner_user_id   TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    visibility      TEXT NOT NULL CHECK (visibility IN ('private','hub_users','invite_only')),
    created_at      INTEGER NOT NULL,
    ended_at        INTEGER,
    end_reason      TEXT
);
-- At most one active Session per device.
CREATE UNIQUE INDEX sessions_active_device ON sessions(owner_device_id) WHERE ended_at IS NULL;
CREATE INDEX sessions_ended ON sessions(ended_at);

-- Invites expire with the Session (ended_at); "joined" is derived from session_viewers.
CREATE TABLE session_invites (
    session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    state      TEXT NOT NULL CHECK (state IN ('invited','declined')),
    created_at INTEGER NOT NULL,
    PRIMARY KEY (session_id, user_id)
);

CREATE TABLE session_viewers (
    viewer_id   TEXT PRIMARY KEY,
    session_id  TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_id   TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    joined_at   INTEGER NOT NULL,
    left_at     INTEGER,
    left_reason TEXT
);
-- A device is an active viewer of a Session at most once.
CREATE UNIQUE INDEX session_viewers_active ON session_viewers(session_id, device_id) WHERE left_at IS NULL;
