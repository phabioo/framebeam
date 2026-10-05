# ADR 0005: Save sync and conflicts in phase 3

- Status: accepted
- Date: 2026-10-05
- Decided by: Fabio (proposal by the orchestrator, accepted on 2026-10-05)

## Context

Phase 3 adds versioned saves: the FrameBeam Player syncs the core's battery save with the FrameBeam Hub, and diverging saves are resolved explicitly by the user. The architecture (`docs/architecture/03-saves.md`, "Save sync" and "13. Save conflicts") fixes the principles; this ADR fixes the data model, protocol and behavior for the PoC. The architecture documents remain unchanged. Design: `docs/design/player.md#3d-player-save-conflict`, `docs/design/hub.md#3k-hub-saves`.

## Decisions

### D1 Save slot

- A slot is identified by (hub, user, game_id, slot); the user is the owner of the authenticated device (`devices.user_id`). In the PoC the slot is always `default`; the API carries it anyway. Reason: avoids an API break when slots arrive.
- A slot holds exactly one opaque blob (the core's battery save), max 64 MiB. The Hub never interprets or merges it. Reason: binary saves cannot be merged safely.

### D2 Revisions and history (two counters)

- Current checkpoint: every accepted upload creates `revision` = previous + 1 (starts at 1), shown as "Rev N". Only the newest checkpoint content is kept as current. Reason: cheap, frequent auto-checkpoints during play without flooding history.
- History: permanent versions with their own per-slot counter `version`, shown as "vN". Each stores the captured checkpoint revision, sha256, device, time and `reason` (`session_end`, `device_change`, `before_conflict_resolution`, `conflict_upload`, `manual_snapshot`). Reason: history holds only meaningful states.
  - `session_end`: upload with sync reason `final_session_end` creates a checkpoint and a history version of it.
  - `device_change`: upload from a device other than the current checkpoint's device first copies the previous checkpoint into history (if not yet captured).
  - `conflict_upload`: the secured upload of a conflict (D3).
  - `before_conflict_resolution`: on resolve, the then-current checkpoint is copied first (if not yet captured).
  - `manual_snapshot`: Hub web "Create snapshot" on the current checkpoint (admin, optional; no API required).
- Storage: files under `<data_dir>/saves/<user_id>/<game_id>/<slot>/`, named by sha256 (identical content stored once per slot); SQLite holds metadata only. Writes: temp file, fsync, atomic rename. Reason: crash-safe, never a half-written save.
- Retention: none in the PoC (keep all).

### D3 Upload protocol (`base_revision`)

Endpoints (all under `/api/v1`, scoped to the caller's user; foreign slots give 403/404):

- `GET /saves`: caller's slots (game_id, slot, current checkpoint meta, open conflict count).
- `GET /games/{game_id}/saves/{slot}`: SaveSlot (checkpoint meta, open conflicts); 404 if none.
- `PUT /games/{game_id}/saves/{slot}`: raw body `application/octet-stream`; upload.
- `GET /games/{game_id}/saves/{slot}/content`: checkpoint bytes; headers `ETag` (sha256), `X-FrameBeam-Save-Revision`.
- `GET /games/{game_id}/saves/{slot}/history`: history versions.
- `GET /games/{game_id}/saves/{slot}/history/{version}/content`: bytes of a version.
- `POST /games/{game_id}/saves/{slot}/conflicts/{conflict_id}/resolve`: body `{"resolution": "use_hub"|"use_local", "expected_revision": N}`.

Upload headers: `X-FrameBeam-Base-Revision` (0 = "no Hub save"), `X-FrameBeam-Content-SHA256` (verified by the Hub), `X-FrameBeam-Sync-Reason` (`checkpoint` | `final` | `final_session_end`). Rules, in order, in one DB transaction per slot:

1. sha256 equals the current checkpoint's: 200, no new revision (idempotent retry).
2. `base_revision` equals the current revision (or 0 with no slot): accept, new revision, 200 with SaveSlot.
3. Otherwise 409 `save_conflict`: the upload is secured as history version (`conflict_upload`) and a conflict is created, or the open conflict of this slot+device is updated to the newest secured upload (at most one open conflict per slot and device). The current checkpoint is not changed. Reason: the decision is made by a person, never by the Hub.

Resolve: `expected_revision` must equal the current revision, else 409 `save_conflict_stale` (nothing changed, caller re-reads). The current checkpoint is copied to history first (`before_conflict_resolution`). `use_hub`: conflict becomes `resolved_hub`, the secured upload stays in history. `use_local`: the secured upload becomes revision+1, conflict becomes `resolved_local`. "Keep both, decide later" is no API call; the conflict stays `open`.

Conflict record: id, game_id, slot, status (`open`|`resolved_hub`|`resolved_local`), hub side (revision, sha256, device id+name, time), secured upload (history version, sha256, base_revision, device id+name, time), created_at, resolved_at, resolved_by.

Errors: new codes `save_conflict`, `save_conflict_stale`, `payload_too_large` (HTTP 413, limit 64 MiB); hash mismatch is `bad_request`.

Upload with a non-zero base when the Hub has no slot yet: 400 `bad_request`.

Handshake: `HandshakeResponse` gets optional `features: [string]`; the Hub advertises `saves_v1`. `protocol_version` stays 1 (additive). A Player without `saves_v1` disables sync and shows "Hub does not support save sync".

### D4 Player sync behavior

- Layout per Hub and Hub user, never mixed: `<baseDir>/hubs/<hub_id>/users/<user_id>/saves/<game_id>/` (the core's save dir) plus `sync.json` there: `{slot, base_revision, last_synced_sha256, pending, conflict_id, last_error, updated_at}`; no secrets. Synced file: the single save the core writes (melonDS DS: `<rom basename>.sav` or as observed); ignore `sync.json` and temp files; several candidates: sync the newest and log a warning.
- Legacy migration: files in the old `hubs/<hub_id>/saves/` matching the ROM basename are copied (never moved or deleted) into the new dir if it has no save; they count as pending local changes (base 0).
- Start sync, before the core loads (local hash L, last synced S, base B, Hub revision H):
  - no Hub slot, no local file: start empty; no Hub slot, local file: upload (base 0), then start.
  - Hub slot, no local file: download, base=H.
  - L == S and H > B: download Hub content (nothing is lost), keep a `.bak` of the replaced file, base=H.
  - L != S and H == B: upload (pending), then start.
  - L != S and H != B: upload, Hub answers 409, conflict dialog (3d) before starting. An existing open conflict also shows the dialog first.
  - Hub unreachable: start with the local file, mark pending, retry only against the same `hub_id`.
- Conflict dialog: "Use Hub version" (resolve `use_hub`, download, local file kept as `<file>.local-<timestamp>.bak`); "Adopt local save as new current version" (`use_local`); "Keep both, decide later" (default): the game starts with the local save, uploads for the slot pause while the conflict is open, changes stay pending, the library shows "Conflict". On `save_conflict_stale`, re-run start sync and show the dialog again.
- Auto checkpoint: poll mtime/size about every 2 s, hash on change; upload after 12 s without further change, at most every 60 s; skip if hash equals `last_synced`; reason `checkpoint`.
- Final sync: on pause, stop and clean exit, upload a changed save immediately (`final`; on stop/exit `final_session_end`). On stop the core is unloaded first to flush its save. App exit waits about 10 s for the upload, then leaves it pending.
- Upload failure (network, 5xx): `pending=true`, retry with backoff against the same Hub and on next connect to it; never to another Hub (hard rule).
- Library status per game: "Synced" / "Sync pending" / "Conflict" / nothing.

### D5 Hub web "Saves" page (3k)

- Admin-only (only admins use the web UI in the PoC). Slot list with user filter; games with an open conflict first ("Conflict, unresolved"), else "Checkpoint, {time}". Empty state: "No saves yet."
- Detail: checkpoint bar (Rev N, device, time, reason) with Download; conflict card with both sides (device, time, base revision, short hash) and actions "Use Hub version", "Adopt local save as new current version" (confirmation, CSRF, `expected_revision` guard), "Keep both, decide later"; history timeline "vN" with Download only.
- No "Restore" in phase 3: the architecture does not back it. This resolves the first point of design deviation 7 and deviation 8 for phase 3 ("Rev N" = checkpoint, "vN" = history).
- Sidebar badge with the number of open conflicts.

### D6 Battery saves in the Player

- melonDS DS exposes the cartridge save only as `RETRO_MEMORY_SAVE_RAM`. The Player's LibretroBackend persists it as `<rom file basename>.sav` (the ROM cache name, i.e. the ROM sha256) in the game's save directory, writes atomically about every 3 s when changed, on pause and before unload, and never overwrites an existing file it could not load (backup on size mismatch).

## Open

- Retention and thinning of history.
- "Restore" from history (and its semantics).
- Multiple slots in the UI.
- WS push of save changes (the Player polls at start; another device's change appears on the next start).
- Manual snapshot in the Player.
- Credential store for Linux and macOS (unchanged from ADR 0003).

## Rejected

- **Timestamp-based conflict decisions:** clocks differ between devices; a newer mtime does not mean a better save, and silent loss breaks the hard rule.
- **Auto-merge of binary saves:** the format is core-specific and opaque; a merge can corrupt the save.
- **A single revision counter for checkpoints and history:** checkpoints are frequent and mostly discarded, so history numbers would have gaps; deviation 7 showed the confusion.
- **Blocking the launch on an unresolved conflict:** the player must be able to play offline or undecided. Instead "decide later" starts with the local save and pauses uploads.
- **Uploading the conflicting save over the current checkpoint:** destroys the Hub's state silently; the upload is secured in history and the checkpoint stays untouched until a person decides.

## Consequences

- Hub (`server/`), protocol (`protocol/`) and Player (`client/`) follow these decisions once accepted; changes via a new ADR.
- The OpenAPI contract gets the endpoints, `SaveSlot`/conflict schemas, error codes and `features`; Go code is regenerated via `make generate`.
- History grows without limit until retention is decided.
- A conflict never loses data: every upload of a stale device is kept in history.
