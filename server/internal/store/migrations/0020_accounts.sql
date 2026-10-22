-- Server accounts (0.18 package 4, ADR 0030 D5). Every column is nullable or has a default, so existing users and
-- devices keep working without a password.

-- devices: how the device was signed in (approval = admin approved a pairing request, invite = onboarding invite or
-- pairing code, password = user name and password, local = app on the same PC as the server), the app install it
-- belongs to (several users can be signed in on one PC, one device row each) and the "Confirm it's you" state.
ALTER TABLE devices ADD COLUMN sign_in_kind TEXT NOT NULL DEFAULT 'approval' CHECK (sign_in_kind IN ('approval','invite','password','local'));
ALTER TABLE devices ADD COLUMN install_id TEXT;
ALTER TABLE devices ADD COLUMN confirmed_until INTEGER;
ALTER TABLE devices ADD COLUMN confirm_failures INTEGER NOT NULL DEFAULT 0;
ALTER TABLE devices ADD COLUMN confirm_failed_at INTEGER;
ALTER TABLE devices ADD COLUMN confirm_locked_until INTEGER;
CREATE INDEX devices_install ON devices(install_id) WHERE install_id IS NOT NULL;

-- users: password bookkeeping and the sign-in lockout.
ALTER TABLE users ADD COLUMN password_changed_at INTEGER;
ALTER TABLE users ADD COLUMN must_change_password INTEGER NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN sign_in_failures INTEGER NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN sign_in_failed_at INTEGER;
ALTER TABLE users ADD COLUMN sign_in_locked_until INTEGER;

-- pairing_requests: the sign-in kind and install the device row gets when the request is polled.
ALTER TABLE pairing_requests ADD COLUMN sign_in_kind TEXT NOT NULL DEFAULT 'approval' CHECK (sign_in_kind IN ('approval','invite','password','local'));
ALTER TABLE pairing_requests ADD COLUMN install_id TEXT;
