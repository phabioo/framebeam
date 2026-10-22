-- Problem reports (0.17, ADR 0029 D3): a Player sends a description and attachments to the Hub of its user; admins
-- see the list under Reports. The files live under <data>/reports/<id>/ (metadata only in SQLite). A report is kept
-- until an admin deletes it. User and device names are copied: the report stays readable when either is removed.
CREATE TABLE reports (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id        TEXT NOT NULL,
    user_name      TEXT NOT NULL,
    device_id      TEXT NOT NULL,
    device_name    TEXT NOT NULL,
    player_version TEXT NOT NULL DEFAULT '',
    os             TEXT NOT NULL DEFAULT '',
    title          TEXT NOT NULL,
    description    TEXT NOT NULL,
    event_title    TEXT NOT NULL DEFAULT '',
    event_time     INTEGER,
    created_at     INTEGER NOT NULL,
    status         TEXT NOT NULL DEFAULT 'new' CHECK (status IN ('new', 'seen')),
    seen_at        INTEGER,
    size           INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX reports_status ON reports(status, created_at);

CREATE TABLE report_files (
    report_id INTEGER NOT NULL REFERENCES reports(id) ON DELETE CASCADE,
    name      TEXT NOT NULL,
    size      INTEGER NOT NULL,
    PRIMARY KEY (report_id, name)
);
