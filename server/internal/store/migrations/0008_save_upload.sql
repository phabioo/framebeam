-- 0.8 save file upload (saves_v4): checkpoint/sync reason 'upload', history reason 'before_upload'.
-- SQLite cannot alter CHECK constraints, so both tables are rebuilt (nothing references them).

CREATE TABLE save_slots_new (
    user_id      TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id      TEXT NOT NULL,
    slot         TEXT NOT NULL,
    revision     INTEGER NOT NULL CHECK (revision >= 1),
    sha256       TEXT NOT NULL,
    size         INTEGER NOT NULL,
    device_id    TEXT NOT NULL,
    reason       TEXT NOT NULL CHECK (reason IN ('checkpoint','final','final_session_end','restore','upload')),
    created_at   INTEGER NOT NULL,
    history_high INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, game_id, slot)
);
INSERT INTO save_slots_new SELECT user_id, game_id, slot, revision, sha256, size, device_id, reason, created_at, history_high FROM save_slots;
DROP TABLE save_slots;
ALTER TABLE save_slots_new RENAME TO save_slots;

CREATE TABLE save_history_new (
    user_id       TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id       TEXT NOT NULL,
    slot          TEXT NOT NULL,
    version       INTEGER NOT NULL CHECK (version >= 1),
    revision      INTEGER NOT NULL CHECK (revision >= 1),
    sha256        TEXT NOT NULL,
    size          INTEGER NOT NULL,
    device_id     TEXT NOT NULL,
    sync_reason   TEXT NOT NULL CHECK (sync_reason IN ('checkpoint','final','final_session_end','restore','upload')),
    reason        TEXT NOT NULL CHECK (reason IN ('session_end','device_change','before_conflict_resolution','conflict_upload','manual_snapshot','before_restore','before_upload')),
    base_revision INTEGER,
    created_at    INTEGER NOT NULL,
    label         TEXT,
    PRIMARY KEY (user_id, game_id, slot, version)
);
INSERT INTO save_history_new SELECT user_id, game_id, slot, version, revision, sha256, size, device_id, sync_reason, reason, base_revision, created_at, label FROM save_history;
DROP TABLE save_history;
ALTER TABLE save_history_new RENAME TO save_history;
CREATE INDEX save_history_sha ON save_history(user_id, game_id, slot, sha256);
