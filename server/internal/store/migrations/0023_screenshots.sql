-- 0.19 package 5: screenshots per user and game, optionally tagged with a run (docs/roadmap.md, section 0.19).
-- The PNG files live under <data>/screenshots/<user_id>/<game_id>/<id>.png; this table holds metadata only.
-- A removed user takes the rows with them (the service removes the files); a removed game takes its rows and files
-- (the service deletes the rows first); a deleted run keeps the screenshots (run_id becomes NULL).
CREATE TABLE screenshots (
    id         TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id    TEXT NOT NULL REFERENCES games(id),
    run_id     TEXT REFERENCES runs(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL,
    width      INTEGER NOT NULL,
    height     INTEGER NOT NULL,
    size_bytes INTEGER NOT NULL,
    sha256     TEXT NOT NULL
);
CREATE INDEX screenshots_user_game ON screenshots(user_id, game_id);
CREATE INDEX screenshots_run ON screenshots(run_id) WHERE run_id IS NOT NULL;
