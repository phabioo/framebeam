-- 0.14 UI round 3 (3l-2): automatic core updates per system and per core.
-- systems.core_auto_update: '' = use the Hub default (setting core_auto_update), 'on' or 'off' overrides it for the system.
-- system_cores.auto_update: '' = follow the system, 'off' = pinned (this core is never updated automatically).
ALTER TABLE systems ADD COLUMN core_auto_update TEXT NOT NULL DEFAULT '' CHECK (core_auto_update IN ('', 'on', 'off'));
ALTER TABLE system_cores ADD COLUMN auto_update TEXT NOT NULL DEFAULT '' CHECK (auto_update IN ('', 'off'));

-- 0.14 UI round 3 (3n-2): "Create user" makes the user and a one-time pairing code in one step. An invite with
-- for_user_id pairs a device with that existing user when redeemed (no new user is created).
ALTER TABLE invites ADD COLUMN for_user_id TEXT REFERENCES users(id) ON DELETE CASCADE;
