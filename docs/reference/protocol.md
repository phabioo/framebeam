# Protocol reference

Shared protocol definition for Hub and Player: `protocol/openapi/framebeam.yaml` (OpenAPI 3.0.3) and `protocol/schemas/` (WSS message schemas, draft 2020-12, with examples). Summary and navigation: [protocol/README.md](../../protocol/README.md). Rules for agents: `protocol/CLAUDE.md`.

- Current `protocol_version`: **1** (integer, separate from product versions). Hub and Player each report `protocol_version` and `min_protocol_version`.
- Compatibility: `Player.protocol_version < Hub.min_protocol_version` -> `player_too_old`; `Hub.protocol_version < Player.min_protocol_version` -> `hub_too_old`.
- Error format: `{"error": {"code": <enum>, "message": string}}`. Auth: Bearer (`fba_` access token, 15 min, `fbd_` device credential, `fbp_` poll token).
- Current OpenAPI spec version: 1.15.0 (history per milestone below). Handshake features: `saves_v1`, `sessions_v1`, `users_v1`, `uploads_v1` (only advertised when the caller may upload), `firmware_v1`, `cores_v1`, `turn_v1` (only while a relay provider is on: embedded TURN, Cloudflare TURN and/or an external TURN server), `saves_v2`, `saves_v3`, `saves_v4`, `saves_v5`, `cores_v2`, `local_setup_v1`, `ice_update_v1`, `plugins_v1`, `runs_v1`; `cores_index_v1` existed in 0.4 to 0.7 and was removed in 0.8.

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
| DELETE | `/api/v1/games/{game_id}/saves/{slot}/history/{version}` | Bearer | deleteSaveSnapshot (204; only `manual_snapshot`, else 409 `save_not_snapshot`; 404 when missing; `saves_v3`) |
| POST | `/api/v1/games/{game_id}/saves/{slot}/upload` | Bearer | uploadSaveFile (409 `session_running` while a Session of the caller runs the game since `saves_v5`; raw `application/octet-stream`, headers `X-FrameBeam-Content-SHA256` and `X-FrameBeam-Expected-Revision`, 0 = slot must not exist; 200 SaveSlot, 400 `bad_request`, 404, 409 `save_conflict_stale`, 413 `payload_too_large`; `saves_v4`) |
| POST | `/api/v1/games/{game_id}/saves/{slot}/rename` | Bearer | renameSaveSlot (`{name}`, 200 SaveSlot under the new name; 400 invalid or same name incl. Windows reserved names, 404 unknown slot, 409 `save_slot_exists`, 409 `save_slot_protected` for `default`, 409 `session_running`; `saves_v5`) |
| DELETE | `/api/v1/games/{game_id}/saves/{slot}` | Bearer | deleteSaveSlot (required query `confirm_revision` = the checkpoint revision the caller saw; 204, hard delete of checkpoint, history, conflicts and content; 400 missing or invalid `confirm_revision`, 404 unknown slot, 409 `save_conflict_stale` on a different revision, 409 `save_slot_protected` for `default`, 409 `session_running`, 409 `conflict` while the slot has an open conflict; `saves_v5`) |
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
| GET | `/api/v1/local/status` | none, loopback only | getLocalStatus (`local_setup_v1`) |
| POST | `/api/v1/local/setup` | none, loopback only | localSetup (first admin plus pairing; 409 when an admin exists) |
| POST | `/api/v1/local/pair` | none, loopback only | localPair (enabled admin credentials, else 401) |
| PUT | `/api/v1/local/settings` | Bearer (admin), loopback only | updateLocalSettings |
| POST | `/api/v1/games` | Bearer | uploadGame (raw body, 403 `uploads_disabled`) |
| GET | `/api/v1/systems` | Bearer | listSystems |
| GET | `/api/v1/systems/{system_id}/firmware/{file_id}` | Bearer | getFirmwareFile (ETag) |
| GET | `/api/v1/cores/{core_id}/packages/{version}/{platform}` | Bearer | getCorePackage |
| GET | `/api/v1/cores/{core_id}/packages/{version}/{platform}/files/{name}` | Bearer | getCorePackageFile (ETag, 304) |
| GET | `/api/v1/plugins` | Bearer | listPlugins (`{plugins:[PluginInfo]}`, every offered version; `plugins_v1`) |
| GET | `/api/v1/plugins/{plugin_id}/{version}/file` | Bearer | getPluginFile (`application/zip`, ETag = SHA-256, 404 unknown; `plugins_v1`) |
| GET | `/api/v1/runs` | Bearer | listRuns (runs the caller participates in, not archived; `runs_v1`) |
| POST | `/api/v1/runs` | Bearer | createRun (`{name, plugins, game_id, invite_user_ids, settings}`, 201 Run, creator joined, status `starting`; 413 `too_large`) |
| GET | `/api/v1/runs/{run_id}` | Bearer | getRun (with every participant's state; 404 for non-participants) |
| POST | `/api/v1/runs/{run_id}/join` | Bearer | joinRun (`{game_id}`) |
| POST | `/api/v1/runs/{run_id}/invite` | Bearer | inviteRun (`{user_ids}`, any joined participant; 409 above 8 participants) |
| POST | `/api/v1/runs/{run_id}/decline` | Bearer | declineRun |
| POST | `/api/v1/runs/{run_id}/leave` | Bearer | leaveRun |
| POST | `/api/v1/runs/{run_id}/status` | Bearer | setRunStatus (`{status}`; 403 wrong user, 409 `conflict` for other transitions) |
| PUT | `/api/v1/runs/{run_id}/state` | Bearer | putRunState (`{base_rev, state}`, own state, 409 `state_conflict` with the current `state_rev` and `state`, 409 `run_not_active`, 413 `too_large`, 429 `rate_limited`) |
| POST | `/api/v1/runs/{run_id}/events` | Bearer | appendRunEvent (`{client_id, plugin_id, source, type, text, payload}`, 201 `{seq}`, 200 for a repeated `client_id`) |
| GET | `/api/v1/runs/{run_id}/events` | Bearer | listRunEvents (`after`, `limit` 1 to 500, default 200; `{events, last_seq}`) |
| PUT | `/api/v1/runs/{run_id}/boards/{plugin_id}` | Bearer | putRunBoard (`{tree}`, at most 64 KiB, `{rev}`) |
| GET | `/api/v1/ws` | Bearer | connectWebSocket (WSS upgrade, documentation only) |

Phase 3 (OpenAPI 1.1.0, handshake feature `saves_v1`, `protocol_version` stays 1). Error codes added: `save_conflict` (409), `save_conflict_stale` (409), `payload_too_large` (413). Rules: ADR 0005.

Phase 4 (OpenAPI 1.2.0, handshake feature `sessions_v1`, `protocol_version` stays 1). Error codes added: `session_not_found` (404), `session_forbidden` (403), `session_full` (409), `session_ended` (410); `capability_missing` is returned as 409. Rules: ADR 0006.

Phase 5 (OpenAPI 1.3.0, handshake features `users_v1`, `uploads_v1`, `firmware_v1`, `protocol_version` stays 1). Added: `redeemInvite` (`POST /api/v1/invites/redeem`, no auth), `uploadGame` (`POST /api/v1/games`, raw body, 4 GiB limit), `listSystems` (`GET /api/v1/systems`), `getFirmwareFile` (`GET /api/v1/systems/{system_id}/firmware/{file_id}`). Error codes added: `invite_invalid` (404), `display_name_taken` (409), `user_disabled` (401), `uploads_disabled` (403); duplicate uploads reuse `conflict` (409, with `existing_game_id`). `core_missing` / `core_version_mismatch` are handshake warnings (`compatible` stays true). A disabled owner's Sessions end with the existing `session_ended` reason `owner_disconnected` (no schema change).

### WSS messages (`/api/v1/ws`)

Envelope `{type, id?, payload}` (JSON text frames); schema `schemas/ws-<type>.schema.json`, example `schemas/examples/ws-<type>.example.json`. The first client message must be `hello`. The Hub pings every 20 s.

| Type | Direction | Purpose |
|---|---|---|
| `hello` | Player -> Hub | `{protocol_version, device_id, runs?}`, first message; optional `runs: [{run_id, last_seq}]` (feature `runs_v1`) is the last event seq the Player saw per run |
| `hello_ack` | Hub -> Player | `{protocol_version, hub_version, features, ice_servers, turn_servers?}` (`turn_servers` only while a relay provider is on, feature `turn_v1`; `ice_servers` defaults to `stun:stun.cloudflare.com:3478`, empty with `-ice-servers none`) |
| `ice_update` | Hub -> Player | `{ice_servers, turn_servers?}` same shape as the ICE part of `hello_ack`; sent to every connected Player when the Hub's effective ICE configuration changes (STUN list, embedded TURN on/off or public host, refreshed Cloudflare TURN credentials; feature `ice_update_v1`); replaces the old lists for PeerConnections created afterwards, running connections are not touched |
| `presence_update` | both | Player: `{state: online\|in_game, game_id?}`; Hub: plus `user_id`, `device_id`, state may be `offline` |
| `session_update` | Hub -> Player | `{session}` created/changed, personalised, to every device that may see the Session |
| `save_updated` | Hub -> Player | `{game_id, slot, revision, sha256, device_id, device_name, reason}` a slot's checkpoint changed; to the user's other connected devices (feature `saves_v2`); `reason` is `checkpoint`, `final`, `final_session_end`, `restore`, `upload` or `conflict_resolution`; `device_id` is the nil UUID for changes made in the Hub web interface; since `saves_v5` also `renamed` (slot renamed: `slot` is the new name, extra field `previous_slot` the old one, checkpoint unchanged) and `deleted` (slot deleted: the other fields are those of the deleted checkpoint) |
| `session_ended` | Hub -> Player | `{session_id, reason}` |
| `session_invite` | Hub -> Player | `{session}` for the invited user's devices |
| `viewer_joined` | Hub -> owner device | `{session_id, viewer_id, display_name, device_name, turn_servers?}` (`turn_servers`: fresh relay credentials for the owner while TURN is on) |
| `viewer_left` | Hub -> owner and viewer device | `{session_id, viewer_id, reason: left\|removed\|revoked\|disconnected}` |
| `signal` | both | `{session_id, viewer_id, kind: offer\|answer\|candidate, sdp?, candidate?, mid?}` relayed unchanged between owner device and the authorized viewer's device |
| `run_updated` | Hub -> Player | `{run}` full run incl. participant states, to every participant (all statuses) after create, invite, join, decline, leave, status change and in the catch-up (feature `runs_v1`) |
| `run_state` | Hub -> Player | `{run_id, user_id, state_rev, state}` to the other joined participants and the writer's other devices |
| `run_event` | Hub -> Player | `{run_id, event}` to every joined participant's connections; the author's own device dedups by `client_id` |
| `error` | Hub -> Player | `{code, message}`, `id` echoes the request |

0.2 "Cores from the Hub" (OpenAPI 1.4.0, handshake feature `cores_v1`, `protocol_version` stays 1). Added: `getCorePackage`, `getCorePackageFile` (ETag = SHA-256, `If-None-Match` 304), optional nullable `SystemInfo.core_package_version`. Error codes added: `core_package_not_found` (404), `core_file_not_available` (404).

0.4 "Internet sessions and save comfort" (OpenAPI 1.5.0, ADR 0012, `protocol_version` stays 1). Added: handshake features `turn_v1`, `saves_v2`, `cores_index_v1` (retired in 0.8); optional `turn_servers` (`TurnServer`) in `hello_ack` and `SessionJoinResponse`; `restoreSaveHistoryVersion`, `createSaveSnapshot`, optional nullable `SaveHistoryVersion.label`, `SaveSyncReason` `restore`, `SaveHistoryReason` `before_restore`; `getCoresIndex`, `getCoresIndexSignature`; WSS message `save_updated`.

0.7.x "Hub saves: new slot and deleting a snapshot" (OpenAPI 1.6.0, `protocol_version` stays 1). Added: handshake feature `saves_v3`; `deleteSaveSnapshot` (`DELETE .../history/{version}`, 204; only `manual_snapshot`, else 409 `save_not_snapshot`; 404 when missing; no `save_updated`). Slot creation from the Hub web interface has no API.

0.7.x "Handshake user" (OpenAPI 1.7.0, `protocol_version` stays 1). Added: optional `user` (`HandshakeUser`: `id`, `display_name`, `role` admin|user) in `HandshakeResponse`, the Hub user the authenticated device belongs to.

The release feed of the updaters is specified in [update-index.md](update-index.md).

0.7.x "Upload a save file" (OpenAPI 1.8.0, `protocol_version` stays 1). Added: handshake feature `saves_v4`; `uploadSaveFile` (`POST .../saves/{slot}/upload`): deliberate replacement of a slot's checkpoint with a file from outside the sync flow (for example a `.sav` from another emulator); it never creates a conflict, a stale `X-FrameBeam-Expected-Revision` returns 409 `save_conflict_stale`, an existing checkpoint is secured in the history first; `SaveSyncReason` `upload`, `SaveHistoryReason` `before_upload`; `save_updated` `reason` `upload`. The Hub web interface offers the same upload for admins.

0.8 "Cores from the libretro buildbot" (OpenAPI 1.9.0, ADR 0020, `protocol_version` stays 1). Added: handshake feature `cores_v2`; optional `default_core_id` and `cores` (`SystemCore`: `core_id`, `display_name`, `version`, `license`, `experimental`, `required_hw_api`, `origin`, optional `build_date`) in `SystemInfo`; optional `origin` in `CorePackage`; core ids relaxed to `^[a-z0-9][a-z0-9_-]{0,63}$`. `preferred_core_id`, `expected_core_version` and `core_package_version` keep describing the default core for Players without `cores_v2`. Removed: `cores_index_v1`, `getCoresIndex`, `getCoresIndexSignature`. Packages from the buildbot carry no license file.

0.9 "One installer" (OpenAPI 1.10.0, ADR 0021, `protocol_version` stays 1). Added: handshake feature `local_setup_v1`; loopback-only endpoints (other callers get 403, rate-limited like pairing): `GET /api/v1/local/status` (no auth: `admin_exists`, `network_sharing`, `import_dir`, `hub_version`), `POST /api/v1/local/setup` (no auth; creates the first admin and pairs the calling Player in one step, 409 when an admin exists), `POST /api/v1/local/pair` (no auth; pairs as an enabled admin with username and password, 401 otherwise), `PUT /api/v1/local/settings` (device token of an admin; `network_sharing`, `import_dir`; 400 `import_dir_unreadable`). Setup and pair answer with the credential payload of the normal pairing poll.

0.10.x "Connections" (OpenAPI 1.11.0, `protocol_version` stays 1). Added: handshake feature `ice_update_v1` and the WSS message `ice_update`; `turn_servers` can come from the embedded TURN server and/or an optional Cloudflare TURN provider (`turn_v1` is announced while any relay provider is on); the default STUN list is `stun:stun.cloudflare.com:3478` (`-ice-servers none` switches it off).

0.10.x "Rename and delete save slots" (OpenAPI 1.12.0, `protocol_version` stays 1). Added: handshake feature `saves_v5`; `renameSaveSlot` (`POST .../saves/{slot}/rename`, body `{name}`, 200 SaveSlot) and `deleteSaveSlot` (`DELETE .../saves/{slot}?confirm_revision=N`, 204); error codes `save_slot_exists` (now in the `ErrorCode` enum, was already sent by the Hub), `save_slot_protected` and `session_running`; `save_updated` reasons `renamed` and `deleted` plus the optional payload field `previous_slot`; `uploadSaveFile` returns 409 `session_running` while an active Session of the calling user runs the game. The `default` slot can be neither renamed nor deleted. Rename and delete are also refused with `session_running` during a Session of the game. Deleting is a hard delete (no undo) that needs the matching `confirm_revision`, so a stale client cannot remove newer progress; the Hub logs it.

0.10.x "External TURN servers" (OpenAPI 1.13.0, `protocol_version` stays 1). Changed: `TurnServer.expires_at` is optional. The Hub sends it for every short-lived credential (embedded TURN, Cloudflare, external TURN in shared-secret mode) and omits it for the static username and password of an external TURN server. `turn_servers` (hello_ack, join response, `viewer_joined`, `ice_update`) may now hold entries of several providers at once. A Player that ignores `expires_at` (the FrameBeam Player does) needs no change.

0.11 "Hub plugins" (OpenAPI 1.14.0, ADR 0023, `protocol_version` stays 1). Added: handshake feature `plugins_v1`; `listPlugins` (`GET /api/v1/plugins`, `PluginInfo`) and `getPluginFile` (`GET /api/v1/plugins/{plugin_id}/{version}/file`, ETag = SHA-256). `PluginInfo.origin` is `official` or `unverified`.

0.11 "Hub runs" (OpenAPI 1.15.0, ADR 0023, `protocol_version` stays 1). Added: handshake feature `runs_v1`; the run endpoints above (tag `runs`); optional `runs` in the WSS `hello`; WSS messages `run_updated`, `run_state`, `run_event` (schemas `ws-run-updated`, `ws-run-state`, `ws-run-event`, shared `ws-run`, `ws-run-event-entry`). Error codes added: `run_not_active` (409), `state_conflict` (409), `too_large` (413), `rate_limited` (429). Run catch-up after `hello`: `run_updated` for every non-archived run of the user, then `run_event`s with `seq > last_seq` for the listed runs, at most 500 per run (more missing: none sent, the Player pages with `GET .../events`). Limits: [12-plugins.md](../architecture/12-plugins.md#1611-hub-runs).
