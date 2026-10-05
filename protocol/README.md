# protocol

Gemeinsame Protokolldefinition für Hub und Player (`openapi/`, `schemas/`). Regeln: `CLAUDE.md`.

- Aktuelle `protocol_version`: **1** (Ganzzahl, getrennt von Produktversionen). Hub und Player melden je `protocol_version` und `min_protocol_version`.
- Kompatibilität: `Player.protocol_version < Hub.min_protocol_version` -> `player_too_old`; `Hub.protocol_version < Player.min_protocol_version` -> `hub_too_old`.
- Fehlerformat: `{"error": {"code": <enum>, "message": string}}`. Auth: Bearer (`fba_` Access Token 15 min, `fbd_` Device Credential, `fbp_` Poll Token).
- Quelle: `openapi/framebeam.yaml` (OpenAPI 3.0.3); WSS-Schemas (draft 2020-12) und Beispiele in `schemas/`.

| Methode | Pfad | Auth | operationId |
|---|---|---|---|
| GET | `/.well-known/framebeam` | keine | getHubInfo |
| POST | `/api/v1/pairing/requests` | keine | createPairingRequest |
| GET | `/api/v1/pairing/requests/{request_id}` | Poll Token | getPairingRequest |
| POST | `/api/v1/auth/token` | keine | createAccessToken |
| POST | `/api/v1/auth/revoke` | Bearer | revokeSelf |
| POST | `/api/v1/handshake` | Bearer | postHandshake |
| GET | `/api/v1/games` | Bearer | listGames |
| GET | `/api/v1/games/{game_id}` | Bearer | getGame |
| GET | `/api/v1/roms/{sha256}` | Bearer | downloadRom (Range, ETag) |
| GET | `/api/v1/ws` | Bearer | connectWebSocket (nur Doku) |
