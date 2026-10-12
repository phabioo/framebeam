-- 0.11 Game identity in the library (ADR 0023): the plugin game picker matches a plugin's `games` patterns against
-- the identity read from the ROM header on upload/import and once at start for existing games.
-- game_code_kind: '' (not read yet), 'nds-gamecode', '3ds-titleid', 'none' (read, no identity found; the start-up
-- backfill does not retry it; exposed as '').
ALTER TABLE games ADD COLUMN game_code TEXT NOT NULL DEFAULT '';
ALTER TABLE games ADD COLUMN game_code_kind TEXT NOT NULL DEFAULT '';
