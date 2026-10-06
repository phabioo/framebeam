# server/ – FrameBeam Hub (Go, SQLite)

Details only when needed, from `docs/architecture/` (index: `docs/architecture/README.md`).

## Responsibility and boundaries
- HTTPS API, file management, auth, registry, internal session metadata, presence, signaling, web interface.
- Never emulates, encodes, decodes or renders (not even multiview); does not run cores, knows only the system/core registry. No sessions management page, no video preview.
- No large framework. See `01-overview.md`.

## Storage locations
- ROMs, saves, firmware as files under `/var/lib/framebeam/`; SQLite holds metadata only, no binary data.
- Firmware/BIOS is provided by the admin only (`05-emulation.md`).

## Save model (hub view)
- Every upload carries `base_version`; a stale base produces a conflict, never a silent overwrite. Timestamps never decide on their own.
- Current checkpoint (every change = new revision) is kept separate from durable history (session end, device switch, before conflict resolution, manual snapshot).
- Both conflicting contents are kept until a deliberate choice is made; back up history before resolving. No merging of binary saves. See `03-saves.md`.

## Security
- HTTPS/WSS required; HTTP/WS only in dev mode or on localhost. Own certificate, own cert/key or reverse proxy.
- Admin: username/password, Argon2id. Regular users are passwordless and created by the admin only (invites). User and device are separate.
- Pairing: player request, admin allow/deny (mandatory). Revoking a device also revokes its access tokens.
- Store tokens and device credentials only as a verification representation; never log secrets. See `10-identity-pairing-tls.md`.

## Web interface
- Go `html/template` + htmx via `embed`, no Node build (ADR 0001). Pages/terms: `09-ui-and-navigation.md`.
- Web interface design: `docs/design/hub.md`, `docs/design/tokens.md` (index: `docs/design/README.md`).
