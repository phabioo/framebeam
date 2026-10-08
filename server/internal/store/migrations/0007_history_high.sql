-- Highest history version ever handed out per slot: deleting a snapshot must not free its number (vN stays unique).
ALTER TABLE save_slots ADD COLUMN history_high INTEGER NOT NULL DEFAULT 0;
