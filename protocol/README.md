# protocol

Shared protocol definition for Hub and Player (`openapi/`, `schemas/`). Rules: `CLAUDE.md`.

- Current `protocol_version`: **1** (integer, separate from product versions). Hub and Player each report `protocol_version` and `min_protocol_version`.
- Compatibility: `Player.protocol_version < Hub.min_protocol_version` -> `player_too_old`; `Hub.protocol_version < Player.min_protocol_version` -> `hub_too_old`.
- Error format: `{"error": {"code": <enum>, "message": string}}`. Auth: Bearer (`fba_` access token, 15 min, `fbd_` device credential, `fbp_` poll token).
- Source: `openapi/framebeam.yaml` (OpenAPI 3.0.3); WSS schemas (draft 2020-12) and examples in `schemas/`.

| Method | Path | Auth | operationId |
|---|---|---|---|
| GET | `/.well-known/framebeam` | none | getHubInfo |
| POST | `/api/v1/pairing/requests` | none | createPairingRequest |
| GET | `/api/v1/pairing/requests/{request_id}` | poll token | getPairingRequest |
| POST | `/api/v1/auth/token` | none | createAccessToken |
| POST | `/api/v1/auth/revoke` | Bearer | revokeSelf |
| POST | `/api/v1/handshake` | Bearer | postHandshake |
| GET | `/api/v1/games` | Bearer | listGames |
| GET | `/api/v1/games/{game_id}` | Bearer | getGame |
| GET | `/api/v1/roms/{sha256}` | Bearer | downloadRom (Range, ETag) |
| GET | `/api/v1/ws` | Bearer | connectWebSocket (documentation only) |
