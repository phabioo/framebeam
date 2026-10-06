# server – FrameBeam Hub

Go hub with SQLite: library, saves, users, devices, signaling, web interface. Rules for agents: `CLAUDE.md`.

## Start and setup

```sh
framebeam-hub setup-admin -username <name>   # password as a single line from stdin; refused if an admin already exists
framebeam-hub [flags]                        # server (HTTPS, self-signed certificate under <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP
```

Flags and environment (flag wins): `-data-dir`/`FRAMEBEAM_DATA_DIR` (default `/var/lib/framebeam`), `-listen`/`FRAMEBEAM_LISTEN` (`:8443`), `-name`/`FRAMEBEAM_NAME` (first start only), `-tls-cert`+`-tls-key` (`FRAMEBEAM_TLS_CERT`/`_KEY`, own certificate), `-dev`/`FRAMEBEAM_DEV` (HTTP instead of HTTPS). The certificate's SHA-256 fingerprint is printed to the log at startup. Without an admin the hub still starts; the web interface then redirects to `/setup` (form accepted from loopback clients only).

Layout of `internal/`: `config`, `store` (SQLite, migrations), `auth` (Argon2id, tokens), `tlsutil`, `hub` (service layer for the API and web interface), `httpapi` (OpenAPI implementation; tests validate against `protocol/openapi/framebeam.yaml`).

Web interface (`internal/web`, `html/template` + htmx via `embed`, no external resources): setup, login (cookie `fb_session`, admins only), library, clients, settings. htmx 2.0.4 (0BSD) lives unmodified at `internal/web/static/htmx.min.js`. ROM upload limit: `web.DefaultMaxUploadBytes` (4 GiB).

## Users, invites and uploads

- Users page (admins): create onboarding invites (code `FB-XXXX-XXXX`, single use, expiry 15 min / 1 h / 24 h, optionally authorizing the first device directly). The code is shown once; only a hash is stored. The Player redeems it with `POST /api/v1/invites/redeem` (rate limited), which creates a passwordless user and either a trusted device (200) or a pending request pre-assigned to that user (202; the admin allows it on the Clients page, "Assign user" defaults to the invite user).
- Admins can disable and enable users. A disabled user gets `401 user_disabled` on every bearer call and the token exchange, their WSS connections are closed and their Sessions end; saves and uploads stay. Web login remains admin-only.
- Settings: "Allow users to upload games" (default off) lets users call `POST /api/v1/games` (raw body, streamed to disk, max 4 GiB, same hashing and de-duplication as the web upload; `uploaded_by` is the caller). Appearance (Light / Dark / System) is a Hub setting for all web pages.

## Systems, cores and firmware

- Systems & Cores page: registry (seeded: `nds` -> `melonds_ds` 1.4.0), expected core version per system (empty = any), the last handshake report per device (core/version status; a missing or other core only warns, `compatible` stays true) and the firmware path.
- Firmware mode per system: `builtin` (core uses its own firmware, files optional) or `native` (files required). The admin provides files (size is validated; an optional admin-pinned SHA-256 must match) under `<data dir>/firmware/<system>/<file_id>` (mode 0600). Hub never ships or invents BIOS/firmware or hashes; contents are never logged and only leave the Hub through `GET /api/v1/systems/{system_id}/firmware/{file_id}` (ETag = SHA-256). The nav badge "N firmware" counts required files that are missing or mismatching.
