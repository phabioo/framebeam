# Server/ – Uzume server (Go, SQLite)

Details only when needed, from `docs/architecture/` (index: `docs/architecture/README.md`).

## Responsibility and boundaries
- HTTPS API, file management, auth, registry, internal session metadata, presence, signaling, admin API (`/api/v1/admin/...`).
- Never emulates, encodes, decodes or renders (not even multiview); does not run cores, knows only the system/core registry. No video preview.
- No large framework. See `01-overview.md`.

## Storage locations
- ROMs, saves, firmware as files under `/var/lib/uzume/`; SQLite holds metadata only, no binary data.
- Firmware/BIOS is provided by the admin only (`05-emulation.md`).

## Save model (hub view)
- Every upload carries `base_version`; a stale base produces a conflict, never a silent overwrite. Timestamps never decide on their own.
- Current checkpoint (every change = new revision) is kept separate from durable history (session end, device switch, before conflict resolution, manual snapshot).
- Both conflicting contents are kept until a deliberate choice is made; back up history before resolving. No merging of binary saves. See `03-saves.md`.

## Security
- HTTPS/WSS required; HTTP/WS only in dev mode or on localhost. Own certificate, own cert/key or reverse proxy.
- Admin: username/password, Argon2id; admin API calls need an admin device token. Regular users may have a password too (admins must); created by the admin only (invites). User and device are separate.
- Password sign-in (`POST /api/v1/auth/sign-in`) replaces admin approval on a new device; generic `invalid_credentials`, rate limit per IP and lockout per user name. Dangerous admin steps call the hub helper `RequireConfirmed` ("Confirm it's you", 403 `confirmation_required`).
- Pairing: player request, admin allow/deny (mandatory). Revoking a device also revokes its access tokens.
- Store tokens and device credentials only as a verification representation; never log secrets. See `10-identity-pairing-tls.md`.

## No web interface
- The server serves only the REST API (`/api/v1`, admin under `/api/v1/admin/...`), WSS signaling and file transfer. No HTML pages, cookies, templates, htmx or static assets, no browser components (ADR 0030 D2). Administration happens in the app; first setup and emergencies use server commands.
- Every admin feature is an OpenAPI endpoint first (`protocol/openapi/uzume.yaml`).
