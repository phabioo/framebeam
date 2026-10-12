-- 0.11 Plugins: .fbplugin files offered to Players by this Hub (docs/architecture/12-plugins.md section 16.10).
-- The files live under <data-dir>/plugins/<plugin_id>/<version>.fbplugin; this table holds only metadata.
-- Times: Unix seconds (UTC).
CREATE TABLE plugins (
    plugin_id   TEXT NOT NULL,
    version     TEXT NOT NULL,
    name        TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    api         INTEGER NOT NULL,
    manifest    TEXT NOT NULL,
    sha256      TEXT NOT NULL,
    size        INTEGER NOT NULL,
    origin      TEXT NOT NULL CHECK (origin IN ('official','unverified')),
    source      TEXT NOT NULL CHECK (source IN ('index','upload','import')),
    uploaded_by TEXT REFERENCES users(id) ON DELETE SET NULL,
    created_at  INTEGER NOT NULL,
    PRIMARY KEY (plugin_id, version)
);
