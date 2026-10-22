// Package backup creates, lists, verifies and restores Hub backup archives (ADR 0029 D2).
//
// An archive is one plain .tar file (framebeam-hub-<name>-<UTC timestamp>[-safety].tar). Members:
//
//	database/framebeam.db   consistent SQLite snapshot (VACUUM INTO) taken while the Hub keeps running
//	data/saves/...          saves, history blobs and snapshots
//	data/states/...         save states, when the Hub has that directory
//	data/plugins/...        installed plugin files
//	data/firmware/...       firmware provided by the admin
//	data/tls/...            the self-generated TLS certificate and key
//	data/roms/...           ROM library, only when "Include ROMs" is on
//	config/<file>           files outside the data directory (own TLS certificate and key, hub.env)
//	manifest.json           last member: Hub version, schema version, contents and the SHA-256 of every member
//
// The manifest is last so that every member is hashed while it is written (one pass, no temporary copy). The
// archive contains secrets (TLS key, hub.env, password hashes in the database): treat it like the data directory.
package backup
