-- 0.11 Runs (ADR 0023 D9/D10): a run is a shared object plugins build on. The Hub stores shape only (settings,
-- per-participant per-plugin state, board trees and event payloads are opaque JSON) and never runs plugin code.

CREATE TABLE runs (
    id         TEXT PRIMARY KEY,
    name       TEXT NOT NULL,
    status     TEXT NOT NULL CHECK (status IN ('starting','active','paused','finished','abandoned')),
    archived   INTEGER NOT NULL DEFAULT 0,
    -- User ids without a foreign key: the history of a run (events, "paused by") survives the deletion of a user.
    created_by TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    status_by  TEXT NOT NULL,
    settings   TEXT NOT NULL DEFAULT '{}',
    -- Last event number; incremented in the same transaction as the event insert, so seq is gapless.
    last_seq   INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX runs_updated ON runs(updated_at);

-- Plugin versions a run uses (pinned at creation). No foreign key to the plugin store: removing a plugin version is
-- refused while a run that is not finished/abandoned uses it (checked by the service).
CREATE TABLE run_plugins (
    run_id    TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    plugin_id TEXT NOT NULL,
    version   TEXT NOT NULL,
    sha256    TEXT NOT NULL,
    position  INTEGER NOT NULL,
    PRIMARY KEY (run_id, plugin_id)
);
CREATE INDEX run_plugins_version ON run_plugins(plugin_id, version);

CREATE TABLE run_participants (
    run_id           TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    user_id          TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status           TEXT NOT NULL CHECK (status IN ('invited','joined','declined','left')),
    -- Hub library game, set on join. No foreign key on purpose (deleting a game must not fail on a run).
    game_id          TEXT,
    -- 0-7 in join order; NULL until the user joined for the first time, kept when leaving.
    color            INTEGER,
    invited_at       INTEGER NOT NULL,
    PRIMARY KEY (run_id, user_id)
);
CREATE INDEX run_participants_user ON run_participants(user_id);

-- State per participant and plugin (opaque JSON, own revision per plugin). A missing row means state {} with rev 0.
CREATE TABLE run_states (
    run_id     TEXT NOT NULL,
    user_id    TEXT NOT NULL,
    plugin_id  TEXT NOT NULL,
    state      TEXT NOT NULL,
    rev        INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY (run_id, user_id, plugin_id),
    FOREIGN KEY (run_id, user_id) REFERENCES run_participants(run_id, user_id) ON DELETE CASCADE
);

-- Append-only log. client_id makes appends idempotent; seq is per run.
CREATE TABLE run_events (
    run_id     TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    seq        INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    user_id    TEXT,
    user_name  TEXT NOT NULL,
    device_id  TEXT,
    plugin_id  TEXT,
    source     TEXT NOT NULL CHECK (source IN ('automatic','manual','hub')),
    type       TEXT NOT NULL,
    text       TEXT NOT NULL DEFAULT '',
    payload    TEXT NOT NULL DEFAULT '{}',
    client_id  TEXT NOT NULL,
    PRIMARY KEY (run_id, seq),
    UNIQUE (run_id, client_id)
);

-- Board tree (building blocks) per run and plugin, published by a joined participant's Player for the Hub web page.
CREATE TABLE run_boards (
    run_id     TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    plugin_id  TEXT NOT NULL,
    rev        INTEGER NOT NULL,
    tree       TEXT NOT NULL,
    updated_by TEXT NOT NULL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY (run_id, plugin_id)
);
