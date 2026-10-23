-- The browser admin is gone (0.18 package 12, ADR 0030 D2), so nothing reads or writes web sessions any more.
-- The index web_sessions_expires is dropped together with the table.
DROP TABLE web_sessions;
