# Architecture: saves

### Save sync, checkpoints and versioning [PoC]

```text
Start sync:      Hub → current checkpoint → Player → emulator
Auto checkpoint: changed save → Player → Hub → current checkpoint
Final sync:      pause / stop / clean exit → Player → Hub
History:         permanent version on relevant events
```

Saves are stored centrally in the file system; SQLite holds the assignment, checkpoint revisions and version metadata including originating device and timestamp. Save files are transferred only when their content has actually changed (hash/dirty detection). After a change, a short debounce follows, for example 10–15 seconds; periodic uploads happen at most roughly every 60 seconds. If the hash is unchanged, no upload takes place. Pause, stop and a clean app exit trigger an immediate final sync of changed saves, independent of the periodic interval.

Auto checkpoints update the **current checkpoint** without creating a permanent history version on every upload. Permanent history versions are created on relevant events such as Session end, device change, before conflict resolution or a manual snapshot. Retention, restore, snapshots, push and slots are specified in [ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md) D7 (0.4): per slot the newest 20 versions plus the newest of each day (30 days) and week (26 weeks) are kept; `manual_snapshot` versions and versions of unresolved conflicts are never thinned. History versions can be restored (the current checkpoint is secured first, reason `before_restore`; the restored content becomes a new revision), snapshots can be labeled, and the Hub pushes `save_updated` over WSS to the user's other devices. The Player lets the user pick the slot per game and Hub profile. Since 0.7.x (feature `saves_v3`, OpenAPI 1.6.0): `DELETE /api/v1/games/{game_id}/saves/{slot}/history/{version}` deletes a history version with reason `manual_snapshot` (204; 409 `save_not_snapshot` for other reasons, 404 when missing); the current checkpoint is never touched, the content file is removed once nothing references it, and no `save_updated` is pushed. The Hub web interface can create a slot from the current version of another slot (revision 1, checkpoint reason `restore`, device "Hub web interface", plus history v1 as manual snapshot labelled `From “<source slot>”`); there is no API for slot creation, Players receive the usual `save_updated` push.

The existing `base_version` conflict logic also applies to checkpoints: every change to the current checkpoint gets a new revision for the next reconciliation, even if no permanent history version is created. An upload against a stale base must not silently overwrite a competing change (section 13).

If the Hub is down or an upload fails, the save is safely buffered locally as **pending sync** with Hub, user and game/slot assignment and base version. Retries go exclusively to the original Hub; a save is never redirected to another Hub. Checkpoints limit possible progress loss in a crash, but do not guarantee a fixed maximum loss duration during outages or pending changes. Save states remain a separate mechanism and are outside the PoC; automatic merging of binary saves is not agreed.

**Saves view in the running game (Player).** "Manage saves" in the in-game side panel opens the saves view inside the game. Snapshot and delete work live. Restore and upload while the game runs: (1) upload the current local save to the Hub, (2) write a local backup, (3) write the save atomically, (4) restart the game: the core is stopped like on Quit (this flushes the old SRAM first), the new save file is written only afterwards, and the game starts again through the normal start path (the Session stays open, the in-game UI stays). The core is never reloaded inside the running process (unload and load in one `retro_init` crashes real cores with hardware rendering). They are refused up front when the core's SAVE_RAM size does not match the save. "Resolve conflict" in the saves view resolves inline ("Keep this device's save" or "Use the Hub save", with a backup of the device copy) and does not start the game.

## 13. Save conflicts: data model and UI

Save versioning is extended with an explicitly modeled conflict state. A new upload refers to the **base version** used during reconciliation. If the current Hub version has changed in the meantime, a diverging local change must not silently overwrite it. An offline change with a stale base can thus be detected just like changes on two devices. Timestamps are used for display, not as the sole conflict decision.

`base_version` also denotes, for auto checkpoints, the current-checkpoint revision read during reconciliation. Checkpoint revision and permanent history version are separate; conflict resolution first secures the competing contents in the history.

Planned metadata are save assignment (game/user or existing save slot), version ID, base version ID, content hash, originating device and timestamp. A conflict record references the competing versions or the secured local upload, its status and a later resolution decision. Both contents are preserved until a deliberate choice is made; save files continue to live in the file system.

The Hub shows conflicts on the **Saves page** next to the version history. The Player shows the sync/conflict state on the affected game and during start, checkpoint and final reconciliation. The detail view makes origin, time and versions comparable and provides actions such as "Use Hub version", "Adopt local save as new current version" and "Keep both versions / decide later". Even a resolution must not cause silent data loss and must be checked against intervening changes.

The concrete API, resolution logic and launch decision on an unresolved conflict remain to be specified. For the PoC, the data model and UI state are to be provided; automatic merging of binary saves is not agreed.
