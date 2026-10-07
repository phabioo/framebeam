# protocol

Shared protocol definition for Hub and Player (`openapi/`, `schemas/`). Rules: `CLAUDE.md`.

- Current `protocol_version`: **1** (integer, separate from product versions). Hub and Player each report `protocol_version` and `min_protocol_version`.
- Compatibility: `Player.protocol_version < Hub.min_protocol_version` -> `player_too_old`; `Hub.protocol_version < Player.min_protocol_version` -> `hub_too_old`.
- Error format: `{"error": {"code": <enum>, "message": string}}`. Auth: Bearer (`fba_` access token, 15 min, `fbd_` device credential, `fbp_` poll token).
- Current OpenAPI spec version: 1.3.0.
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
| POST | `/api/v1/games/{game_id}/saves/{slot}/history/{version}/restore` | Bearer | restoreSaveHistoryVersion (`{expected_revision}`, 200 SaveSlot, 409 `save_conflict_stale`; `saves_v2`) |
| POST | `/api/v1/games/{game_id}/saves/{slot}/snapshots` | Bearer | createSaveSnapshot (optional `{label}`, 201 SaveHistoryVersion, 404 without checkpoint; `saves_v2`) |
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
| POST | `/api/v1/sessions/{session_id}/join` | Bearer | joinSession (`{viewer_id, permissions, ice_servers, turn_servers?}`; 409 `session_full` / `capability_missing` without H.264 decode) |
| DELETE | `/api/v1/sessions/{session_id}/viewers/{viewer_id}` | Bearer (owner device or that viewer) | removeSessionViewer |
| POST | `/api/v1/invites/redeem` | none | redeemInvite |
| POST | `/api/v1/games` | Bearer | uploadGame (raw body, 403 `uploads_disabled`) |
| GET | `/api/v1/systems` | Bearer | listSystems |
| GET | `/api/v1/systems/{system_id}/firmware/{file_id}` | Bearer | getFirmwareFile (ETag) |
| GET | `/api/v1/cores/index` | Bearer | getCoresIndex (raw signed bytes, 404 `core_package_not_found` without verified index; `cores_index_v1`) |
| GET | `/api/v1/cores/index.sig` | Bearer | getCoresIndexSignature (text/plain `ed25519 <key_id> <base64>`; `cores_index_v1`) |
| GET | `/api/v1/cores/{core_id}/packages/{version}/{platform}` | Bearer | getCorePackage |
| GET | `/api/v1/cores/{core_id}/packages/{version}/{platform}/files/{name}` | Bearer | getCorePackageFile (ETag, 304) |
| GET | `/api/v1/ws` | Bearer | connectWebSocket (WSS upgrade, documentation only) |

Phase 3 (OpenAPI 1.1.0, handshake feature `saves_v1`, `protocol_version` stays 1). Error codes added: `save_conflict` (409), `save_conflict_stale` (409), `payload_too_large` (413). Rules: ADR 0005.

Phase 4 (OpenAPI 1.2.0, handshake feature `sessions_v1`, `protocol_version` stays 1). Error codes added: `session_not_found` (404), `session_forbidden` (403), `session_full` (409), `session_ended` (410); `capability_missing` is returned as 409. Rules: ADR 0006.

Phase 5 (OpenAPI 1.3.0, handshake features `users_v1`, `uploads_v1`, `firmware_v1`, `protocol_version` stays 1). Added: `redeemInvite` (`POST /api/v1/invites/redeem`, no auth), `uploadGame` (`POST /api/v1/games`, raw body, 4 GiB limit), `listSystems` (`GET /api/v1/systems`), `getFirmwareFile` (`GET /api/v1/systems/{system_id}/firmware/{file_id}`). Error codes added: `invite_invalid` (404), `display_name_taken` (409), `user_disabled` (401), `uploads_disabled` (403); duplicate uploads reuse `conflict` (409, with `existing_game_id`). `core_missing` / `core_version_mismatch` are handshake warnings (`compatible` stays true). A disabled owner's Sessions end with the existing `session_ended` reason `owner_disconnected` (no schema change).

### WSS messages (`/api/v1/ws`)

Envelope `{type, id?, payload}` (JSON text frames); schema `schemas/ws-<type>.schema.json`, example `schemas/examples/ws-<type>.example.json`. The first client message must be `hello`. The Hub pings every 20 s.

| Type | Direction | Purpose |
|---|---|---|
| `hello` | Player -> Hub | `{protocol_version, device_id}`, first message |
| `hello_ack` | Hub -> Player | `{protocol_version, hub_version, features, ice_servers, turn_servers?}` (`turn_servers` only while TURN is on, feature `turn_v1`) |
| `presence_update` | both | Player: `{state: online\|in_game, game_id?}`; Hub: plus `user_id`, `device_id`, state may be `offline` |
| `session_update` | Hub -> Player | `{session}` created/changed, personalised, to every device that may see the Session |
| `save_updated` | Hub -> Player | `{game_id, slot, revision, sha256, device_id, device_name, reason}` a slot's checkpoint changed; to the user's other connected devices (feature `saves_v2`); `reason` is `checkpoint`, `final`, `final_session_end`, `restore` or `conflict_resolution`; `device_id` is the nil UUID for changes made in the Hub web interface |
| `session_ended` | Hub -> Player | `{session_id, reason}` |
| `session_invite` | Hub -> Player | `{session}` for the invited user's devices |
| `viewer_joined` | Hub -> owner device | `{session_id, viewer_id, display_name, device_name, turn_servers?}` (`turn_servers`: fresh relay credentials for the owner while TURN is on) |
| `viewer_left` | Hub -> owner and viewer device | `{session_id, viewer_id, reason: left\|removed\|revoked\|disconnected}` |
| `signal` | both | `{session_id, viewer_id, kind: offer\|answer\|candidate, sdp?, candidate?, mid?}` relayed unchanged between owner device and the authorized viewer's device |
| `error` | Hub -> Player | `{code, message}`, `id` echoes the request |

0.2 "Cores from the Hub" (OpenAPI 1.4.0, handshake feature `cores_v1`, `protocol_version` stays 1). Added: `getCorePackage`, `getCorePackageFile` (ETag = SHA-256, `If-None-Match` 304), optional nullable `SystemInfo.core_package_version`. Error codes added: `core_package_not_found` (404), `core_file_not_available` (404).

## Update index (0.3)

Release feed of the updater ([ADR 0011](../docs/adr/0011-automatic-updates.md)). No change to `protocol_version` (stays 1) or OpenAPI. Release `updates-index` holds `updates-index.json` and `updates-index.json.sig` (default URL `https://github.com/phabioo/framebeam/releases/download/updates-index/updates-index.json`; signature URL = index URL + `.sig`). Signature as for the core index: Ed25519 over the exact bytes, one line `ed25519 <key_id> <base64 sig>`, same release key.

Schema 1:

```json
{
  "schema": 1,
  "generated_at": "2026-10-07T10:00:00Z",
  "releases": [
    {
      "product": "hub",
      "channel": "beta",
      "version": "0.3.0-beta.57",
      "commit": "<40 hex>",
      "published_at": "2026-10-07T10:00:00Z",
      "notes_url": "https://github.com/phabioo/framebeam/releases/tag/v0.3.0-beta.57",
      "protocol_version": 1,
      "min_protocol_version": 1,
      "artifacts": [
        {"platform": "linux-arm64", "kind": "deb", "name": "framebeam-hub_0.3.0~beta.57_arm64.deb",
         "size": 123, "sha256": "<64 lowercase hex>", "url": "https://github.com/.../framebeam-hub_0.3.0~beta.57_arm64.deb"}
      ]
    }
  ]
}
```

- `product`: `hub` or `player`. `channel`: `stable` or `beta`. `notes_url` is optional.
- Platforms and kinds: hub `linux-amd64`, `linux-arm64` with `deb` and `binary`; player `windows-x64` with `installer` and `zip`.
- Artifact URLs must be https (`file://` only when the index itself was loaded from `file://`, for tests). Integrity comes from size and SHA-256 in the signed index.
- Unknown fields are ignored, an unknown schema is an error, invalid releases are skipped and reported, a duplicate (product, channel, version) rejects the whole index.
- Versions are SemVer 2.0; a consumer picks the highest release of its product, channel, platform and kind that is strictly newer than the running version and protocol-compatible. Maintained with `framebeam-sign release-add` (newest 5 per product and channel).

0.4 "Internet sessions and save comfort" (OpenAPI 1.5.0, ADR 0012, `protocol_version` stays 1). Added: handshake features `turn_v1`, `saves_v2`, `cores_index_v1`; optional `turn_servers` (`TurnServer`) in `hello_ack` and `SessionJoinResponse`; `restoreSaveHistoryVersion`, `createSaveSnapshot`, optional nullable `SaveHistoryVersion.label`, `SaveSyncReason` `restore`, `SaveHistoryReason` `before_restore`; `getCoresIndex`, `getCoresIndexSignature`; WSS message `save_updated`.
