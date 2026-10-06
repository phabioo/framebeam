-- Phase 5 (ADR 0007): users/invites, systems and cores registry, firmware, client reports.

-- Disabled users (admins can never be disabled). Display names are unique per Hub (case-insensitive).
ALTER TABLE users ADD COLUMN disabled_at INTEGER;
CREATE UNIQUE INDEX users_display_name_nocase ON users(display_name COLLATE NOCASE);

-- Onboarding invites. Only a hash of the code is stored (never the code itself).
CREATE TABLE invites (
    id                TEXT PRIMARY KEY,
    code_hash         TEXT NOT NULL UNIQUE,
    authorize_device  INTEGER NOT NULL CHECK (authorize_device IN (0,1)),
    status            TEXT NOT NULL CHECK (status IN ('active','redeemed','revoked')),
    created_by        TEXT REFERENCES users(id) ON DELETE SET NULL,
    created_at        INTEGER NOT NULL,
    expires_at        INTEGER NOT NULL,
    redeemed_at       INTEGER,
    redeemed_by       TEXT REFERENCES users(id) ON DELETE SET NULL,
    revoked_at        INTEGER
);
CREATE INDEX invites_status ON invites(status, expires_at);

-- Systems and cores registry (the Hub never runs cores). expected_core_version NULL = any version.
CREATE TABLE systems (
    id                    TEXT PRIMARY KEY,
    display_name          TEXT NOT NULL,
    extensions            TEXT NOT NULL,
    preferred_core_id     TEXT NOT NULL,
    preferred_core_name   TEXT NOT NULL,
    expected_core_version TEXT,
    platforms             TEXT NOT NULL,
    provisioning          TEXT NOT NULL,
    input_profile         TEXT NOT NULL,
    display_profile       TEXT NOT NULL,
    firmware_mode         TEXT NOT NULL CHECK (firmware_mode IN ('builtin','native'))
);
INSERT INTO systems(id, display_name, extensions, preferred_core_id, preferred_core_name, expected_core_version,
                    platforms, provisioning, input_profile, display_profile, firmware_mode)
VALUES ('nds', 'Nintendo DS', '.nds', 'melonds_ds', 'melonDS DS', '1.4.0',
        'windows-x86_64,linux-x86_64', 'Included in the Player', 'nds', 'dual_screen', 'builtin');

-- Firmware/BIOS files a system can use. sizes: allowed sizes in bytes (comma separated).
CREATE TABLE firmware_defs (
    system_id    TEXT NOT NULL REFERENCES systems(id) ON DELETE CASCADE,
    file_id      TEXT NOT NULL,
    display_name TEXT NOT NULL,
    sizes        TEXT NOT NULL,
    sort         INTEGER NOT NULL,
    PRIMARY KEY (system_id, file_id)
);
INSERT INTO firmware_defs(system_id, file_id, display_name, sizes, sort) VALUES
    ('nds', 'bios7',    'ARM7 BIOS',   '16384', 1),
    ('nds', 'bios9',    'ARM9 BIOS',   '4096', 2),
    ('nds', 'firmware', 'DS Firmware', '131072,262144,524288', 3);

-- Provided files; content lives under <data-dir>/firmware/<system>/<file_id>.
CREATE TABLE firmware_files (
    system_id   TEXT NOT NULL,
    file_id     TEXT NOT NULL,
    sha256      TEXT NOT NULL,
    size        INTEGER NOT NULL,
    uploaded_at INTEGER NOT NULL,
    PRIMARY KEY (system_id, file_id),
    FOREIGN KEY (system_id, file_id) REFERENCES firmware_defs(system_id, file_id) ON DELETE CASCADE
);

-- Optional admin-pinned expected SHA-256 (may exist without a file). No hashes are shipped.
CREATE TABLE firmware_pins (
    system_id TEXT NOT NULL,
    file_id   TEXT NOT NULL,
    sha256    TEXT NOT NULL,
    PRIMARY KEY (system_id, file_id),
    FOREIGN KEY (system_id, file_id) REFERENCES firmware_defs(system_id, file_id) ON DELETE CASCADE
);

-- Last handshake report per device.
CREATE TABLE device_reports (
    device_id        TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
    platform         TEXT NOT NULL,
    arch             TEXT NOT NULL,
    player_version   TEXT NOT NULL,
    protocol_version INTEGER NOT NULL,
    cores            TEXT NOT NULL,
    reported_at      INTEGER NOT NULL
);
