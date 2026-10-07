-- 0.2 "Cores from the Hub": signed core packages from the trusted source and their local file cache.
-- The files live in <data dir>/cores/<core_id>/<version>/<platform>/<name>; SQLite holds metadata only.

CREATE TABLE core_packages (
    core_id    TEXT NOT NULL,
    version    TEXT NOT NULL,
    platform   TEXT NOT NULL,
    license    TEXT NOT NULL,
    source_url TEXT NOT NULL,
    source_ref TEXT NOT NULL,
    origin     TEXT NOT NULL,
    synced_at  INTEGER NOT NULL,
    PRIMARY KEY (core_id, version, platform)
);

-- cached_at NULL = the file is not in the Hub cache (yet).
CREATE TABLE core_package_files (
    core_id   TEXT NOT NULL,
    version   TEXT NOT NULL,
    platform  TEXT NOT NULL,
    name      TEXT NOT NULL,
    role      TEXT NOT NULL CHECK (role IN ('library','license')),
    size      INTEGER NOT NULL,
    sha256    TEXT NOT NULL,
    url       TEXT NOT NULL,
    cached_at INTEGER,
    PRIMARY KEY (core_id, version, platform, name),
    FOREIGN KEY (core_id, version, platform) REFERENCES core_packages(core_id, version, platform) ON DELETE CASCADE
);
