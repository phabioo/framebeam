# How saves work

The Hub keeps the central, versioned copy of every save; the Player syncs with it. The Hub stores a save as an opaque blob and never merges it. Decisions: [ADR 0005](../adr/0005-saves-phase3.md), [ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md) D7; model: [03-saves.md](../architecture/03-saves.md).

## Sync

- The core writes its battery save to the Player's local save directory (melonDS DS: `<rom sha256>.sav`).
- Before launch the Player compares the local save with the Hub's current checkpoint: it downloads a newer Hub save, uploads a changed local one, or starts empty.
- While playing, changes are uploaded as checkpoints (12 s after the last change, at most every 60 s); on pause, stop and exit a final upload follows. Every upload names the Hub revision it is based on.
- Without a connection the game starts with the local save and the upload stays pending for the same Hub and user. Saves are never sent to another Hub.
- Locations: Player `<data dir>/hubs/<hub_id>/users/<user_id>/saves/<game_id>/`; Hub `<data dir>/saves/` (files; SQLite holds metadata only).

## Conflicts

If the Hub moved on in the meantime, the Hub never overwrites: it keeps the upload in history and opens a conflict. Decide in the Player dialog or on the Hub page "Saves":

- use the Hub version, or adopt the local save ("Use Hub version" / "Adopt local save" on the Hub page), or
- "Keep both, decide later" (the default): the game starts with the local save and uploads for that game pause.

The Player shows a per-game badge Synced / Sync pending / Conflict; the Hub navigation has a badge for open conflicts.

## History, restore, snapshots, slots

- Each slot has a current checkpoint ("Rev N") and a durable history ("vN"). The history can be downloaded.
- Restore a history version and create manual snapshots in the Player (save history, slot picker per game) or on the Hub's Saves page. A "save changed on another device" notice appears live (WSS push `save_updated`).
- Retention thins the history per slot: `-save-keep-recent` (20), `-save-keep-daily` (30), `-save-keep-weekly` (26); manual snapshots and versions of unresolved conflicts are never thinned. See [hub-configuration.md](hub-configuration.md) (also editable in Settings → Network).
