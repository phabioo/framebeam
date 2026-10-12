-- 0.12 Game identity for gb, gbc and gba (data-driven readers): games of these systems that were marked 'none' (read,
-- no identity found) while the systems had no identity are read again by the start-up backfill.
UPDATE games SET game_code_kind = '' WHERE game_code_kind = 'none' AND game_code = '' AND system IN ('gb', 'gbc', 'gba');
