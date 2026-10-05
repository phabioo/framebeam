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
| GET | `/api/v1/saves` | Bearer | listSaves |
| GET | `/api/v1/games/{game_id}/saves/{slot}` | Bearer | getSaveSlot |
| PUT | `/api/v1/games/{game_id}/saves/{slot}` | Bearer | putSave (raw body, `X-FrameBeam-Base-Revision`/`-Content-SHA256`/`-Sync-Reason`, 409 `save_conflict`) |
| GET | `/api/v1/games/{game_id}/saves/{slot}/content` | Bearer | downloadSaveContent (ETag, `X-FrameBeam-Save-Revision`) |
| GET | `/api/v1/games/{game_id}/saves/{slot}/history` | Bearer | listSaveHistory |
| GET | `/api/v1/games/{game_id}/saves/{slot}/history/{version}/content` | Bearer | downloadSaveHistoryContent |
| POST | `/api/v1/games/{game_id}/saves/{slot}/conflicts/{conflict_id}/resolve` | Bearer | resolveSaveConflict (409 `save_conflict_stale`) |
| GET | `/api/v1/users` | Bearer | listUsers (`{id, display_name, online}` for the invite field) |
| GET | `/api/v1/sessions` | Bearer | listSessions (Sessions the caller may join or owns, plus inviting) |
| POST | `/api/v1/sessions` | Bearer | publishSession (ends the device's previous Session; 409 `capability_missing` without H.264 encode) |
| GET | `/api/v1/sessions/{session_id}` | Bearer | getSession (403 `session_forbidden`, 404 `session_not_found`, 410 `session_ended`) |
| PATCH | `/api/v1/sessions/{session_id}` | Bearer (owner device) | updateSession (visibility; revokes viewers no longer allowed) |
| DELETE | `/api/v1/sessions/{session_id}` | Bearer (owner device) | endSession |
| PUT | `/api/v1/sessions/{session_id}/invites/{user_id}` | Bearer (owner device) | inviteSessionUser |
| DELETE | `/api/v1/sessions/{session_id}/invites/{user_id}` | Bearer (owner device) | withdrawSessionInvite (removes that user's viewers) |
| POST | `/api/v1/sessions/{session_id}/decline` | Bearer (invited user) | declineSession |
| POST | `/api/v1/sessions/{session_id}/join` | Bearer | joinSession (`{viewer_id, permissions, ice_servers}`; 409 `session_full` / `capability_missing` without H.264 decode) |
| DELETE | `/api/v1/sessions/{session_id}/viewers/{viewer_id}` | Bearer (owner device or that viewer) | removeSessionViewer |
| GET | `/api/v1/ws` | Bearer | connectWebSocket (WSS upgrade, documentation only) |

Phase 4 (OpenAPI 1.2.0, handshake feature `sessions_v1`, `protocol_version` stays 1). Error codes added: `session_not_found` (404), `session_forbidden` (403), `session_full` (409), `session_ended` (410); `capability_missing` is returned as 409. Rules: ADR 0006.

### WSS messages (`/api/v1/ws`)

Envelope `{type, id?, payload}` (JSON text frames); schema `schemas/ws-<type>.schema.json`, example `schemas/examples/ws-<type>.example.json`. The first client message must be `hello`. The Hub pings every 20 s.

| Type | Direction | Purpose |
|---|---|---|
| `hello` | Player -> Hub | `{protocol_version, device_id}`, first message |
| `hello_ack` | Hub -> Player | `{protocol_version, hub_version, features, ice_servers}` |
| `presence_update` | both | Player: `{state: online\|in_game, game_id?}`; Hub: plus `user_id`, `device_id`, state may be `offline` |
| `session_update` | Hub -> Player | `{session}` created/changed, personalised, to every device that may see the Session |
| `session_ended` | Hub -> Player | `{session_id, reason}` |
| `session_invite` | Hub -> Player | `{session}` for the invited user's devices |
| `viewer_joined` | Hub -> owner device | `{session_id, viewer_id, display_name, device_name}` |
| `viewer_left` | Hub -> owner and viewer device | `{session_id, viewer_id, reason: left\|removed\|revoked\|disconnected}` |
| `signal` | both | `{session_id, viewer_id, kind: offer\|answer\|candidate, sdp?, candidate?, mid?}` relayed unchanged between owner device and the authorized viewer's device |
| `error` | Hub -> Player | `{code, message}`, `id` echoes the request |
