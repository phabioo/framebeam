-- Save sync (phase 3, ADR 0005). Metadata only; content lives under <data-dir>/saves/<user>/<game>/<slot>/<sha256>.
-- game_id has no foreign key on purpose: deleting a game from the library must never delete saves.
-- device_id has no foreign key: the history keeps the ID even if a device row disappears with its user.

-- Current checkpoint of a slot ("Rev N").
CREATE TABLE save_slots (
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id    TEXT NOT NULL,
    slot       TEXT NOT NULL,
    revision   INTEGER NOT NULL CHECK (revision >= 1),
    sha256     TEXT NOT NULL,
    size       INTEGER NOT NULL,
    device_id  TEXT NOT NULL,
    reason     TEXT NOT NULL CHECK (reason IN ('checkpoint','final','final_session_end')),
    created_at INTEGER NOT NULL,
    PRIMARY KEY (user_id, game_id, slot)
);

-- Permanent history ("vN", own per-slot counter). revision = captured checkpoint revision (for
-- conflict_upload: the Hub revision the upload conflicted with); base_revision only for conflict_upload.
CREATE TABLE save_history (
    user_id       TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id       TEXT NOT NULL,
    slot          TEXT NOT NULL,
    version       INTEGER NOT NULL CHECK (version >= 1),
    revision      INTEGER NOT NULL CHECK (revision >= 1),
    sha256        TEXT NOT NULL,
    size          INTEGER NOT NULL,
    device_id     TEXT NOT NULL,
    sync_reason   TEXT NOT NULL CHECK (sync_reason IN ('checkpoint','final','final_session_end')),
    reason        TEXT NOT NULL CHECK (reason IN ('session_end','device_change','before_conflict_resolution','conflict_upload','manual_snapshot')),
    base_revision INTEGER,
    created_at    INTEGER NOT NULL,
    PRIMARY KEY (user_id, game_id, slot, version)
);
CREATE INDEX save_history_sha ON save_history(user_id, game_id, slot, sha256);

-- Save conflicts. Hub side: snapshot (for open conflicts the live checkpoint is shown instead).
CREATE TABLE save_conflicts (
    id              TEXT PRIMARY KEY,
    user_id         TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id         TEXT NOT NULL,
    slot            TEXT NOT NULL,
    status          TEXT NOT NULL CHECK (status IN ('open','resolved_hub','resolved_local')),
    device_id       TEXT NOT NULL, -- uploader of the secured upload
    secured_version INTEGER NOT NULL,
    hub_revision    INTEGER NOT NULL,
    hub_sha256      TEXT NOT NULL,
    hub_device_id   TEXT NOT NULL,
    hub_created_at  INTEGER NOT NULL,
    created_at      INTEGER NOT NULL,
    resolved_at     INTEGER,
    resolved_by     TEXT
);
-- At most one open conflict per slot and device.
CREATE UNIQUE INDEX save_conflicts_open ON save_conflicts(user_id, game_id, slot, device_id) WHERE status = 'open';
CREATE INDEX save_conflicts_slot ON save_conflicts(user_id, game_id, slot);
