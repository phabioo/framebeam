# ADR 0002: Protocol and FrameBeam Hub in phase 1

- Status: accepted
- Date: 2026-10-05
- Decided by: Fabio (proposal by the orchestrator, confirmed on 2026-10-05)

## Context

`docs/architecture/08-repo-and-open-points.md` leaves API endpoints, message formats, compatibility rules, token format and lifetimes, revocation details and TLS certificate management open. Phase 1 needs defaults for these. Fabio confirmed them on 2026-10-05.

## Decisions

- **Protocol version:** `protocol_version` is an integer starting at 1, independent of product versions. FrameBeam Hub and FrameBeam Player each report `protocol_version` and `min_protocol_version`. Player below the Hub's minimum: `player_too_old`. Hub below the Player's minimum: `hub_too_old`.
- **Info endpoint:** `GET /.well-known/framebeam` returns `hub_id`, `name`, `hub_version`, `protocol_version`, `min_protocol_version`, `api_base`. No auth, grants no rights. All other endpoints live under `/api/v1`.
- **Error format:** `{"error":{"code","message"}}` with fixed codes, including `player_too_old`, `hub_too_old`, `core_missing`, `core_version_mismatch`, `capability_missing`, `device_revoked`, `pairing_*`.
- **Tokens:** opaque (random bytes, base64url) with prefix `fba_` (access token), `fbd_` (device credential), `fbp_` (poll token). The Hub stores only the SHA-256 hash; because of the high entropy, Argon2 is not needed. Access token: 15 min. Device credential: valid until revoked. Revoking a device immediately invalidates all of its access tokens. The admin password is hashed with Argon2id.
- **Pairing:** A request via POST without auth returns `request_id` and `poll_token`. The admin allows or denies it in the web interface and assigns the request to an existing user (phase 1: only the admin exists). The Player polls; the credential is delivered exactly once. Open requests expire after 10 min. The number of open requests is limited (HTTP 429).
- **Handshake:** `POST /api/v1/handshake` after auth. Phase 1 checks only the protocol versions. Core and codec checks follow with the core registry (phase 5) and Sessions (phase 4), respectively.
- **ROMs:** Upload in phase 1 only via the admin web interface. Download via API with Range and ETag. Files are stored in the data directory by SHA-256; SQLite holds only metadata.
- **WSS:** Only the message schemas are created in `protocol/schemas` (envelope, `hello`, `hello-ack`, `error`, `presence-update`). Implementation follows from phase 4.
- **TLS:** On first start the Hub generates a self-signed certificate (ECDSA P-256) in the data directory. Alternatively your own cert/key via configuration, or operation behind a reverse proxy. HTTP only with an explicit dev flag or on localhost.
- **SQLite:** Pure Go driver `modernc.org/sqlite`; the Hub builds with `CGO_ENABLED=0`.

## Open

- ~~Certificate renewal and confirmed pin change.~~ Update 2026-10-06: resolved in 0.1.1 ([ADR 0009](0009-finish-poc.md) D1, D2).
- Other points from section 8 (save retention, cache limits, ICE/STUN/TURN, media parameters, core/firmware manifests) are not affected by this ADR.

## Rejected

- **Refresh tokens in phase 1:** The device credential takes over this role; a second token type is unnecessary.
- **Argon2 for tokens:** Gives no benefit for high-entropy random bytes.
- **SQLite via cgo (`mattn/go-sqlite3`):** complicates cross-builds (`CGO_ENABLED=0`, amd64/arm64).

## Consequences

- The architecture documents remain unchanged; deviations apply via this ADR.
- OpenAPI and schemas in `protocol/` as well as the Hub in `server/` follow these decisions; changes only via a new ADR.
