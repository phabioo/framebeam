# server – FrameBeam Hub

Go hub with SQLite: library, saves, users, devices, signaling, web interface. Rules for agents: `CLAUDE.md`.

## Start and setup

```sh
framebeam-hub setup-admin -username <name>   # password as a single line from stdin; refused if an admin already exists
framebeam-hub renew-cert [-data-dir <dir>]   # renew the self-generated certificate now (refused with own cert/key)
# systemd install: use `sudo packaging/linux/install-hub.sh renew-cert` instead (runs as the service user, restarts the service)
framebeam-hub [flags]                        # server (HTTPS, self-signed certificate under <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP
```

Flags and environment (flag wins): `-data-dir`/`FRAMEBEAM_DATA_DIR` (default `/var/lib/framebeam`), `-listen`/`FRAMEBEAM_LISTEN` (`:8443`), `-name`/`FRAMEBEAM_NAME` (first start only), `-tls-cert`+`-tls-key` (`FRAMEBEAM_TLS_CERT`/`_KEY`, own certificate), `-ice-servers`/`FRAMEBEAM_ICE_SERVERS` (comma-separated `stun:` URLs; default none, a LAN needs none), `-dev`/`FRAMEBEAM_DEV` (HTTP instead of HTTPS). The certificate's SHA-256 fingerprint is printed to the log at startup. At startup the Hub renews its self-generated certificate when it is expired or expires within 30 days (ECDSA P-256, 10 years; the previous pair stays as `cert.pem.prev` / `key.pem.prev`); `framebeam-hub renew-cert [-data-dir <directory>]` renews on demand and refuses when `-tls-cert`/`-tls-key` are set; it prints the certificate directory it renewed. On a systemd install run `sudo packaging/linux/install-hub.sh renew-cert` instead of calling the binary directly (it uses the service's data dir and user, and restarts the service). Own certificates are never modified. A renewed certificate has a new fingerprint, which Players must confirm ([ADR 0009](../docs/adr/0009-finish-poc.md)). Without an admin the hub still starts; the web interface then redirects to `/setup` (form accepted from loopback clients only).

Layout of `internal/`: `api` (generated from OpenAPI via `make generate`, oapi-codegen), `config`, `store` (SQLite, migrations 0001-0005, backup before migrating an existing DB), `auth` (Argon2id, tokens), `tlsutil`, `updates` (signed updates index, SemVer, selection, staging, root apply helper), `hub` (service layer for the API and web interface), `httpapi` (OpenAPI implementation incl. the WSS endpoint in `ws.go`; tests validate against `protocol/openapi/framebeam.yaml`), `version`, `web`.

Web interface (`internal/web`, `html/template` + htmx via `embed`, no external resources): setup, login (cookie `fb_session`, admins only), Library, Saves, Systems & Cores, Clients, Users, Settings. htmx 2.0.4 (0BSD) lives unmodified at `internal/web/static/htmx.min.js`. ROM upload limit: `web.DefaultMaxUploadBytes` (4 GiB).

## Saves and Sessions

- Saves are stored as files under `<data dir>/saves/`, SQLite holds metadata only; a stale base creates a conflict and never overwrites ([ADR 0005](../docs/adr/0005-saves-phase3.md)).
- Sessions: session API plus WSS presence and signaling relay; the Hub never touches media (audio/video flow directly between Players) ([ADR 0006](../docs/adr/0006-sessions-phase4.md)).
- An owner device that disconnects has a 30 s grace period before its Session ends.

## Users, invites and uploads

- Users page (admins): create onboarding invites (code `FB-XXXX-XXXX`, single use, expiry 15 min / 1 h / 24 h, optionally authorizing the first device directly). The code is shown once; only a hash is stored. The Player redeems it with `POST /api/v1/invites/redeem` (rate limited), which creates a passwordless user and either a trusted device (200) or a pending request pre-assigned to that user (202; the admin allows it on the Clients page, "Assign user" defaults to the invite user).
- Admins can disable and enable users. A disabled user gets `401 user_disabled` on every bearer call and the token exchange, their WSS connections are closed and their Sessions end; saves and uploads stay. Web login remains admin-only.
- Settings: "Allow users to upload games" (default off) lets users call `POST /api/v1/games` (raw body, streamed to disk, max 4 GiB, same hashing and de-duplication as the web upload; `uploaded_by` is the caller). Appearance (Light / Dark / System) is a Hub setting for all web pages.

## Systems, cores and firmware

- Systems & Cores page: registry (seeded: `nds` -> `melonds_ds` 1.4.0), expected core version per system (empty = any), the last handshake report per device (core/version status; a missing or other core only warns, `compatible` stays true) and the firmware path.
- Firmware mode per system: `builtin` (core uses its own firmware, files optional) or `native` (files required). The admin provides files (size is validated; an optional admin-pinned SHA-256 must match) under `<data dir>/firmware/<system>/<file_id>` (mode 0600). Hub never ships or invents BIOS/firmware or hashes; contents are never logged and only leave the Hub through `GET /api/v1/systems/{system_id}/firmware/{file_id}` (ETag = SHA-256). The nav badge "N firmware" counts required files that are missing or mismatching.

## Updates (0.3)

The Hub checks a signed update feed and can install new versions of itself when it runs from the `.deb` package. The spec is `docs/roadmap.md` (0.3 "Automatic updates"); package and units are under `packaging/linux/`.

- Version info: `framebeam-hub version [--json]` prints one JSON object `{"product":"hub","version","channel","commit","protocol_version","min_protocol_version"}`. Build with `-ldflags "-X .../internal/version.Version=<v> -X .../internal/version.Channel=<stable|beta|dev> -X .../internal/version.Commit=<sha>"` (defaults `dev`, `dev`, empty). Versions are SemVer 2.0 and compared by precedence (build metadata ignored); a non-SemVer version such as `dev` disables the updater.
- Feed: the release tag `updates-index` holds `updates-index.json` and `updates-index.json.sig` (Ed25519 over the exact bytes, same format and trusted keys as the core index, [ADR 0010](../docs/adr/0010-cores-from-the-hub.md)). `-update-index-url`/`FRAMEBEAM_HUB_UPDATE_INDEX_URL` overrides the URL (https, or `file://` for tests; `file://` artifact URLs are accepted only when the index itself came from `file://`). Extra trusted keys: `-core-trust-key`/`FRAMEBEAM_HUB_CORE_TRUST_KEYS`, which now cover cores and updates. Unknown schema or duplicate (product, channel, version) rejects the index; invalid releases are skipped.
- Settings page, section "Updates" (admins): channel (`stable`, `beta`; `dev` builds default to off until a channel is selected; `beta` also sees stable releases, the highest SemVer wins; never a downgrade), automatic install (default on for beta, off for stable), "Check now", "Install update" (with confirmation). Checks run in the background at startup, hourly on beta and daily otherwise, and never block or fail startup. Settings live in the `settings` table (`update_channel`, `update_auto`, ...).
- Protocol: a candidate whose `min_protocol_version` is above the protocol version of any Player seen in the last 30 days (`device_reports`) is "breaking": the page warns, automatic install skips it, a manual install asks for a second confirmation.
- Install path: the Hub (service user) downloads the `.deb` for `linux-<GOARCH>`, verifies size and SHA-256 against the signed index, writes it with the exact index and signature to `<data dir>/updates/staged/` plus `staged.json`, and creates `<request dir>/update-request` (`-update-request-dir`/`FRAMEBEAM_HUB_UPDATE_REQUEST_DIR`, default `/run/framebeam`). Automatic install does not run during an active Session and does not retry a version whose last attempt failed. This needs a packaged install (executable `/usr/bin/framebeam-hub`, writable request dir); otherwise the page shows the version, the download link and the manual command.
- Root helper `framebeam-hub update apply-staged` (run by `framebeam-hub-update.service` as root; reads `FRAMEBEAM_DATA_DIR` and `FRAMEBEAM_HUB_CORE_TRUST_KEYS`): deletes the request file first, re-verifies the staged index signature with the compiled-in and env keys only, requires a version strictly newer than its own (no replay of old signed releases), requires the `linux-<arch>` `deb` artifact named in `staged.json`, never follows symlinks in the data directory, copies the package into a root-owned private temp directory, verifies size and SHA-256 of the copy and runs `dpkg -i` on it. The result goes to `<data dir>/updates/last-result.json` (owned like the data directory, shown on the Settings page).
- CLI: `framebeam-hub update check [-channel stable|test]` prints the selection as JSON (`current`, `channel`, `platform`, `up_to_date`, `available` with artifact, `skipped`); `framebeam-hub update stage [-channel ...]` additionally downloads, verifies and stages the update and creates the request file (run as the service user). Without `-channel` the Hub's stored setting applies (if the database exists), else the build channel; a `dev` build needs `-channel`. Both accept the usual config flags (`-data-dir`, `-update-index-url`, `-core-trust-key`, `-update-request-dir`).
- Database backup: before schema migrations are applied to an existing database, `VACUUM INTO <data dir>/backups/hub-<from>-to-<to>-<UTC yyyymmddThhmmssZ>.db` is written and the newest 5 are kept; a failed backup aborts startup with a clear error. Fresh databases are not backed up.
- Index maintenance (`framebeam-sign`): `release-add -index <updates-index.json> -release <release.json> [-keep 5]` adds or replaces one release (creates the index if missing), keeps the newest `-keep` per (product, channel) by SemVer, sets `generated_at` and writes sorted, deterministic JSON; `sign -index <file>` detects an updates index (object with `releases`) versus a core index (`packages`); `verify` stays byte-level.
- Rollback is manual: `sudo apt install --allow-downgrades ./framebeam-hub_<old>.deb`, and if the schema changed, restore the matching backup.
