# ADR 0025: Local ROMs (0.12)

- Status: accepted (implemented in 0.12)
- Date: 2026-10-12
- Deciders: Fabio (2026-10-10: local ROMs with optional upload, owner-only visibility, no P2P; 2026-10-12: merge by game identity, not only by hash). Details marked "default" were chosen in the implementation thread and are not separately decided.
- Builds on: [ADR 0022](0022-nintendo-3ds-with-azahar.md) (ROM cache), [ADR 0023](0023-plugins.md) (game codes from ROM headers). The 0.12 goals: play ROMs the user keeps on their own disk, with the usual save sync, and upload them to the Hub only when wanted.

## Context

Until 0.11 a ROM exists only through the Hub: upload, then download into the Player's ROM cache. Users want to play files they already have in a folder without copying them to the Hub, but still share saves, Sessions and runs for the same game. Different dumps of one game (trimmed/untrimmed, `.3ds` and `.cci`, a re-dump) must not split the saves. 4 GiB ROMs over a proxy or a flaky uplink need a resumable upload.

## Decisions

### D1 Game files and identity merge

- A game has one or more ROM files (`game_roms`: SHA-256, size, file name, `stored` 0/1). `games.rom_*` stays the primary file. The migration (0015) copies every existing game as one stored file.
- Identity = (system, `game_code`, `game_version`) with a non-empty code (default for the version source: NDS header byte 0x1E, 3DS NCCH version). A file whose identity matches an existing game becomes a further file of that game; saves stay on the one `game_id`. Other versions or regions stay separate games.
- Files whose header cannot be read (for example `.zcci`, unknown systems) have no identity and match by SHA-256 only. Without a match they become a new game.
- Match order: SHA-256 of a known file, then identity, then a new game. `Game.rom` is a stored file if there is one, else the primary file.

### D2 Visibility

- A local-only game (no file stored on the Hub) is visible to the users who registered one of its files (`rom_owners`). As soon as one of its files is stored on the Hub, it is visible to everyone. Admins see all games in the web library, local-only ones with a "Local only" badge.
- `GET /api/v1/games` and `GET /api/v1/games/{id}` filter accordingly. `GET /api/v1/roms/{sha256}` serves stored files only (404 otherwise). The Hub never stores a local-only file.
- `DELETE /api/v1/games/{id}/registration` removes the caller's registrations; the game and its saves stay.

### D3 Register API

- `POST /api/v1/games/register` takes SHA-256, size, file name and optionally system, title, game code and version (feature `local_roms_v1`, OpenAPI 1.17.0). 201 for a new game, 200 for an existing one; the caller becomes an owner of the file.
- The Hub trusts the identity (game code, version) sent by the Player. This is acceptable on a self-hosted Hub: the users are known, and a wrong claim only merges or splits their own library entries. When the Hub stores a file itself (web or API upload, resumable upload, import) it reads the identity from the file header (`ReadGameIdentityVersion`) and ignores client claims.

### D4 Resumable uploads

- `POST /api/v1/uploads` creates or resumes a session (same user and same SHA-256, or same file name and size); `GET` returns the offset; `PUT` takes a raw chunk with `Content-Range`, the start must equal the offset (else 409), chunks are at most 64 MiB; `DELETE` aborts. A stored SHA-256 answers 409 with `existing_game_id`. Same permission (`uploads_disabled`) and the same 4 GiB limit as before; at most 16 open sessions per user.
- The Hub persists the SHA-256 state with the offset, so the last chunk needs no re-read. The last chunk checks the hash and stores the file: it marks the registered file as stored (game unchanged), else adds it to the identity match, else creates a game. Data lives under `<data>/tmp/uploads/<id>`; sessions untouched for 7 days are removed by the janitor.
- The web interface uploads with JavaScript in 8 MiB chunks (`/library/uploads/...`, progress and resume). Without JavaScript the multipart form still works.
- The old one-shot `POST /api/v1/games` stays for older Players. Chunk reads have their own read deadline (15 minutes per chunk, 2 hours for the old streaming routes).

### D5 Player library folders

- Settings → Library folders: the user adds folders; the Player scans them for the extensions of the Hub's systems, hashes new files, reads the game code (NDS header, 3DS NCSD/NCCH) and registers the files with the Hub. Hashes are cached by path + size + mtime, so a 4 GB file is hashed once. Scanning runs off the UI thread.
- Launch order: a local file of any ROM of the game, started straight from the folder (no copy into the ROM cache); else the ROM cache; else download when the game is on the Hub; else "File missing on this device".
- The Library shows "Local" for games not on the Hub and "Missing" when no file is on this device. "Upload to Hub" in the game detail starts a resumable upload that survives a restart (upload id kept per SHA-256). "Upload ROM" also uses the resumable API when the Hub offers `local_roms_v1`.

### D6 No P2P

ROMs are never sent between Players. Another user plays a local-only game only after one of its files is stored on the Hub, or with their own file of the same identity or hash.

## Consequences

- A Hub with `local_roms_v1` shows local-only games to their owners; older Players keep working with stored games and `POST /api/v1/games`.
- A Player trusts nothing it cannot hash: launch from a folder checks size and mtime against the cache entry and re-hashes a changed file.
- Unregistering the last owner of a local-only game leaves the game row with its saves; it is then visible only to admins ("Local only"), who can delete it. No automatic cleanup is built (open). Deleting a game removes all its files, registrations and the game row. `protocol_version` stays 1.

## Amendment 2026-10-19: deleting and re-adding a game

Deleting a game that has saves records its identity and file hashes (`removed_games`, migration 0018). A game created later for the same SHA-256, else for the same identity (system + game code + kind + ROM version, rule 2 of D3), re-uses the removed game's id, so the saves belong to it again without any copy; saves of a game that never comes back stay listed as "(removed game)". Details: [saves](../architecture/03-saves.md#deleted-games-and-saves).
