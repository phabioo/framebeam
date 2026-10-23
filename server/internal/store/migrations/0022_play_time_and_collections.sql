-- 0.19 package 4: play time per user, game and run, favorites and collections per user (docs/roadmap.md, section 0.19).
-- Game ids carry no foreign key on purpose (like saves): a removed game keeps its play time in the user totals, and
-- the service deletes favorites and collection entries of a removed game itself.

-- plays: one row per stretch of time a device was in a game (presence in_game, set by the app). ended_at is NULL
-- while the play is open; last_seen_at is refreshed about every minute so a crash can close it where it was last
-- seen. Plays shorter than 10 s are deleted when they close. No foreign key to devices (a revoked or removed
-- device keeps its time in the user's totals); run_id becomes NULL when the run is deleted.
CREATE TABLE plays (
    id           TEXT PRIMARY KEY,
    user_id      TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_id    TEXT NOT NULL,
    game_id      TEXT NOT NULL,
    run_id       TEXT REFERENCES runs(id) ON DELETE SET NULL,
    started_at   INTEGER NOT NULL,
    last_seen_at INTEGER NOT NULL,
    ended_at     INTEGER
);
CREATE INDEX plays_user_game ON plays(user_id, game_id);
CREATE INDEX plays_game ON plays(game_id);
CREATE INDEX plays_run ON plays(run_id, user_id) WHERE run_id IS NOT NULL;
CREATE INDEX plays_open ON plays(device_id) WHERE ended_at IS NULL;

-- favorites: per user, follows the person to every PC.
CREATE TABLE favorites (
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id    TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (user_id, game_id)
);
CREATE INDEX favorites_game ON favorites(game_id);

-- collections: named lists of games per user. Deleting a collection never deletes games.
CREATE TABLE collections (
    id         TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
CREATE UNIQUE INDEX collections_user_name ON collections(user_id, name COLLATE NOCASE);

CREATE TABLE collection_games (
    collection_id TEXT NOT NULL REFERENCES collections(id) ON DELETE CASCADE,
    game_id       TEXT NOT NULL,
    added_at      INTEGER NOT NULL,
    PRIMARY KEY (collection_id, game_id)
);
CREATE INDEX collection_games_game ON collection_games(game_id);
