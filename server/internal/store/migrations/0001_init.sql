-- Times: Unix seconds (UTC).
CREATE TABLE hub (
    id   TEXT PRIMARY KEY,
    name TEXT NOT NULL
);

CREATE TABLE users (
    id            TEXT PRIMARY KEY,
    username      TEXT NOT NULL UNIQUE COLLATE NOCASE,
    display_name  TEXT NOT NULL,
    role          TEXT NOT NULL CHECK (role IN ('admin','user')),
    password_hash TEXT,
    created_at    INTEGER NOT NULL
);

CREATE TABLE devices (
    id              TEXT PRIMARY KEY,
    user_id         TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name            TEXT NOT NULL,
    platform        TEXT NOT NULL,
    arch            TEXT NOT NULL,
    player_version  TEXT NOT NULL,
    credential_hash TEXT NOT NULL,
    status          TEXT NOT NULL CHECK (status IN ('trusted','revoked')),
    created_at      INTEGER NOT NULL,
    last_seen_at    INTEGER,
    revoked_at      INTEGER
);
CREATE INDEX devices_user ON devices(user_id);

CREATE TABLE access_tokens (
    token_hash TEXT PRIMARY KEY,
    device_id  TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    expires_at INTEGER NOT NULL
);
CREATE INDEX access_tokens_device ON access_tokens(device_id);
CREATE INDEX access_tokens_expires ON access_tokens(expires_at);

CREATE TABLE pairing_requests (
    id                TEXT PRIMARY KEY,
    poll_token_hash   TEXT NOT NULL,
    device_id         TEXT NOT NULL,
    device_name       TEXT NOT NULL,
    platform          TEXT NOT NULL,
    arch              TEXT NOT NULL,
    player_version    TEXT NOT NULL,
    protocol_version  INTEGER NOT NULL,
    remote_addr       TEXT NOT NULL,
    status            TEXT NOT NULL CHECK (status IN ('pending','approved','denied','expired','consumed')),
    user_id           TEXT REFERENCES users(id) ON DELETE CASCADE,
    created_at        INTEGER NOT NULL,
    expires_at        INTEGER NOT NULL
);
CREATE INDEX pairing_requests_status ON pairing_requests(status, expires_at);
CREATE INDEX pairing_requests_addr ON pairing_requests(remote_addr, created_at);

CREATE TABLE games (
    id          TEXT PRIMARY KEY,
    title       TEXT NOT NULL,
    system      TEXT NOT NULL,
    rom_sha256  TEXT NOT NULL UNIQUE,
    rom_size    INTEGER NOT NULL,
    filename    TEXT NOT NULL,
    uploaded_by TEXT NOT NULL REFERENCES users(id),
    added_at    INTEGER NOT NULL
);

CREATE TABLE web_sessions (
    token_hash TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    csrf_token TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL
);
CREATE INDEX web_sessions_expires ON web_sessions(expires_at);

CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
