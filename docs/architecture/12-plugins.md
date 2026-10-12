# Architecture: plugins (runtime, Player UI, Hub plugins and runs implemented; Player runs in progress)

## 16. Plugin interface [0.11: runtime, Player UI, Hub plugins and runs implemented; Player runs in progress]

Status: package 1 (plugin runtime in the Player), package 2 (plugin UI in the Player: building blocks, map view, in-game tab, HUD, Multiview tile strip, sidebar screen, toasts, game detail section, shell context, hotkeys) and package 3 on the Hub side (plugin store and delivery, `.fbplugin` files with `SIGNATURE.json`, signed plugin index, runs with REST and WSS, Hub web pages Plugins and Runs with the JS renderer) plus the Player side of plugin delivery (install from the Hub and from files) are implemented. In progress: the Player side of runs (`fb.run` Lua API, Runs screen). Still design only: `memory.write`, `tool.exec`, settings schema, UI language. What the runtime implements today is described in [guides/plugin-api.md](../guides/plugin-api.md). Decisions: [ADR 0023](../adr/0023-plugins.md) (proposed). Screens and building-block sheet: [design/plugins.md](../design/plugins.md). Where this file names code, it names what is built or today's extension points. Open items are marked **open**.

Decided defaults of the runtime (package 1): plugins run only with developer mode on (Settings → Plugins); the only install paths are local folders (copy or linked dev folder with live reload), all unsigned; Lua 5.4.7 is vendored in `client/plugins/third_party/`; plugins resolve (`requires`/`provides`) once at game start; the plugin tick is limited to 30 Hz by default (`tickHz`); budgets per entry call are 20M instructions / 500 ms and 64 MiB per state.

Principle: FrameBeam stays a generic emulator Player. Everything game-specific (for example Pokémon SoulLink/Nuzlocke runs) lives in plugins. The Hub manages (plugins, run state), the Player emulates (reads memory, shows UI).

Plugin-specific documentation (rules, per-game adapters, plugin roadmaps) lives in the plugin repositories, not in this repository.

### 16.1 Goals and non-goals

Goals:

- Plugins feel like part of FrameBeam: same components, tokens, light/dark, controller operation, hotkeys and languages.
- Plugins read emulator memory in the device's address space, identical across cores for the same system.
- Several plugins run at once, isolated from each other and from the game.
- Official plugins come from their own repositories; Hub upload and local install work without GitHub.
- Run state is shared through the Hub and stays on the user's own Hub.

Non-goals:

- Plugin-provided QML, native code in plugins, free network access, sending controller input.
- Pokémon (or any game) knowledge in the core code.
- Game data, sprites, maps or any ROM-derived content in any repository.
- Switch / Gen 8 (no libretro core).
- Randomizer plugin: deferred, not dropped (see 16.11).
- `memory.write` and `tool.exec` implementation in the first release: the API describes them, they are built later.

### 16.2 Extension points in today's code

| Area | Where | Today | Needed |
|---|---|---|---|
| Memory | `client/emulation/libretro_backend.cpp`, `memory_map.h` | Implemented: accepts `RETRO_ENVIRONMENT_SET_MEMORY_MAPS`; descriptors with `select` 0 match by address range; fallback when a core gives no map: `RETRO_MEMORY_SYSTEM_RAM` mapped at address 0. `RETRO_MEMORY_SAVE_RAM` is used as before | Open: whether the buildbot Azahar build exposes the map and covers ORAS data (local check with the Plugin console) |
| Backend interface | `client/emulation/emulator_backend.h` (`EmulatorBackend`) | Implemented: memory map access and watch set (`MemoryWatchSet`, `MemorySnapshot` in `memory_snapshot.h`) | - |
| Threading | `EmulationRunner` | Implemented: merged watched ranges are copied after `runFrame()` every N frames (default every frame; limits 4 MiB per range, 16 MiB per tick); the plugin thread works on the copy | - |
| Game start | `GameStarter::launch` in `client/ui/gamestarter.cpp` | Implemented: `game_identity.h` provides the NDS game code and the 3DS title ID | Open: GB/GBA header code (needs those systems) |
| Saves | keyed by `romSha256` (`gamestarter.cpp`) | A changed ROM (for example externally randomized) gets its own saves | Unchanged; "never overwrite saves silently" stays |
| Hub channel | WSS envelope, `protocol/schemas/ws-envelope.schema.json` (`type` + `payload`) | Implemented (package 3): `run_updated`, `run_state`, `run_event`, optional `runs` in `hello` (feature `runs_v1`) | - |
| Peer channel | DataChannel `fb-diag` | Exists only while someone watches the Session | Not suitable for run state |
| Distribution | Core distribution with SHA-256 pin ([ADR 0020](../adr/0020-cores-from-the-libretro-buildbot.md)); signed `updates-index`, `server/cmd/framebeam-sign` | Cores, updates, plugins | Implemented (package 3): `.fbplugin` signature and plugin index reuse the updates-index key and `framebeam-sign` (16.10) |
| UI | `client/ui/*.qml`, `Fb*` components, `Theme.qml`; Hub web: Go templates + htmx (`server/internal/web`) | Implemented in the Player (package 2): `fb.ui`, validation in `client/plugins/plugin_ui.*`, `PluginsController`, `PluginBlock.qml`, `PluginMapView.qml`, `PluginImageProvider`; slots panel, HUD, tile strip, screen, game detail, toasts. [API guide](../guides/plugin-api.md#ui) | Implemented in the Hub web UI (package 3): `server/internal/web/static/plugin-blocks.js` renders the same tree read-only |

Core-side facts (3DS): see [ADR 0023](../adr/0023-plugins.md) context (Azahar exposes memory maps with virtual addresses; `retro_get_memory_data` returns NULL; `retro_cheat_set` is empty).

### 16.3 Runtime

- **Lua 5.4**, one Lua state per plugin, on a dedicated plugin thread (never the emulation or UI thread).
- Sandbox: no `io`, `os`, `package`, `load` or `debug`; an instruction budget per call; a memory limit per state through a custom allocator. File and network access only through the API.
- **Memory snapshots:** a plugin registers address ranges. The emulation thread copies all registered ranges after `runFrame()` every N frames into a buffer; the plugin reads the copy. The emulation can never be slowed by a plugin. If two plugins register the same range, it is copied once per tick.
- **Virtual addresses:** plugins address the device's address space, resolved through the core's memory maps (RetroAchievements-style), so cores of the same system look the same.
- **Pointer following:** data may live on a heap. A plugin reads a pointer from a snapshot, then registers the target range; it is included from the next tick.
- **Native helper functions** called from Lua, generic and testable: decompression (LZ77/LZ11 and similar), containers (NARC, GARC), image decoding (2bpp/4bpp tiles, NCGR/NCLR, BCLIM/ETC1) to PNG, and format helpers such as Pokémon data decryption. Knowledge about individual games stays in the plugin.
- A crash or an exceeded budget pauses only that plugin, with a visible notice; the game and other plugins continue.

### 16.4 Plugin types and trust levels

| Type | Example | May | Source |
|---|---|---|---|
| Script plugin | SoulLink/Nuzlocke, tracker | Read memory, read ROM (for assets), UI, run state; runs in the Lua sandbox | Any source, including Hub upload |
| Tool plugin | Randomizer (deferred) | Additionally start an external program (`tool.exec`); not sandboxable | Signed official plugins only |

### 16.5 Permissions

| Permission | Meaning | Status |
|---|---|---|
| `memory.read` | Read registered ranges | first release |
| `rom.read` | Read the user's own game files at runtime (assets) | first release |
| `ui` | Provide building-block trees for slots, including notices (toasts) outside the game; no separate notice permission | first release |
| `run.state` | Read/write per-player run state, append run events | first release |
| `memory.write` | Write memory through a write queue | later (cheats, editor) |
| `tool.exec` | Start an external program | later, signed official only |

`memory.write`: all plugins' writes go through one queue applied on the emulation thread between two frames in a fixed order. If two plugins persistently set the same address, the Player shows the conflict instead of silently letting one win. Before the first write in a play session the Player takes a save snapshot (the Hub already versions saves). The consent ("Change game memory") is asked once per game. In a run, every change is written to the run log that all participants see ("cheat used" event), and a run rule can forbid memory writes; the plugin then stays off in that run.

Not planned: sending input, free network access.

The Player shows name, origin (Official / Unverified / Local) and permissions before a plugin is switched on. In-game surfaces (tab, HUD widget, tile strip, toast) show a small "Unverified" or "Local" marker on hover or focus for non-official plugins; Official shows none.

### 16.6 Manifest (sketch)

```json
{
  "id": "net.framebeam.example",
  "name": "Example",
  "version": "1.0.0",
  "api": 1,
  "type": "script",
  "requires": { "net.framebeam.example-core": "^1.0" },
  "provides": ["example.team"],
  "games": [{ "system": "nds", "codes": ["ABC*"] }],
  "permissions": ["memory.read", "rom.read", "ui", "run.state"],
  "entry": "main.lua",
  "assets": ["icons/", "i18n/"],
  "settings": { }
}
```

`api` versions the interface; a plugin with an unknown API version is rejected with a clear message instead of being half-loaded. `settings` holds a schema from which the settings page is generated. **Open:** the exact schema, the Lua API and the building-block catalog are fixed in the design handoff and the runtime work package.

### 16.7 Several plugins at once

- Isolation: own state, memory and compute budget per plugin.
- Load order: `requires` first; cycles and missing or incompatible versions are reported on activation.
- Cooperation: `provides` exposes an interface for other plugins; calls between plugins are messages through the Player, not shared Lua memory. Events can be subscribed to by permitted plugins.
- UI: several plugins in one slot get tabs (side panel), are stacked (HUD, tile overlay) or listed separately (sidebar, settings, hotkeys). Hotkey conflicts are shown in the Hotkeys tab: FrameBeam keys win, then the earlier plugin; a losing key is inactive.
- Management: Settings → Plugins lists version, origin, permissions, dependencies and a switch per plugin, per game and per run.

### 16.8 UI

Plugins ship **no QML** (QML can run JavaScript and load native modules, which would void the sandbox and let the look drift). A plugin describes its UI as a tree of declarative building blocks; the Player renders it with its own `Fb*` components, and a small JS renderer in the Hub web UI draws the same tree. On state changes the plugin sends only the changed values.

Building blocks v1 (implemented in the Player; the exact catalog with props and limits is in the [API guide](../guides/plugin-api.md#ui); the list below is the design outline):

- Layout: column, row, grid, tabs, scroll area, card
- Content: text, label, chip/badge, icon, image/sprite, progress/HP bar, table/list
- Interaction: button (calls a plugin action), toggle, select, text field, confirmation dialog
- Map view
- Settings generated from the manifest schema

A new building block needs a Player update; the `api` version keeps that clean. A second tier with signed plugin QML is conceivable only if the building blocks provably do not suffice.

Slots (implemented in the Player; the Hub web run page is built, see 16.11):

| Slot | Where |
|---|---|
| In-game side panel (own tab) | `GameScreen`, next to Saves/Diagnostics |
| In-game overlay (HUD) | over the game picture, can be switched off |
| Multiview tile overlay | on a friend's tile |
| Own sidebar screen | like Library/Settings |
| Section in game detail | `DetailPane` |
| Settings page | Settings → Plugins |
| Hotkeys | Controllers → Hotkeys |
| Notifications | toasts |
| Hub web: run page, game detail | Hub web UI (also usable as a browser source) |

HUD corners: widgets stack in the bottom left (default), bottom right and top left; the top right is reserved for diagnostics. F4 hides all HUD widgets, Ctrl+H shows or hides one plugin's widget (the keys come later in package 2; the hide actions exist in the Player) (expanding is a click or controller A). Remote Multiview tiles show one plugin strip; the user picks it when several plugins offer one. In-game views stay dark in both themes.

Map view: a background image, markers at coordinates (player symbols in the player's colour, sprites, state), zoom and pan with mouse and controller, selection details and filters. Positions are at location or route level (map ID), not exact overworld position.

### 16.9 Assets

Game artwork (sprites, icons, region maps) belongs to the rights holders. It is **never** in any repository. A plugin extracts it at runtime from the user's own game files with `rom.read` and the native helpers and stores it in a local plugin cache. Repositories hold only facts the project records itself (for example location tables and map coordinates). Falling back to user-supplied asset packs is possible but not the default.

### 16.9a Languages and i18n

Decided 2026-10-11 (Fabio): the group plays German/EU releases first.

- Plugins support all language versions of a game where feasible, at least German and English.
- Names of species, moves, items and locations are read from the game at runtime, so they follow the game's language; plugins do not ship name tables.
- Plugin UI strings come from i18n files (`i18n/`): English is the source, German the first translation.
- 3DS: EU 3DS cartridges contain several languages, and the game language follows the emulated console's system language. Azahar offers the core option "3DS System Language" (Citra language setting). FrameBeam must let the user set it (per-game override or a 3DS default); **open:** the exact setting and where it lives. Game plugins such as `pokemon-core` must detect the active language from the game instead of assuming it.

### 16.10 Distribution

Built in package 3 (Hub) and in the Player's delivery part.

**`.fbplugin` file.** A zip (deflate or store) with `manifest.json` at the root (the Player manifest format, [API guide](../guides/plugin-api.md)), the entry file, assets and i18n as in a plugin folder, and an optional `SIGNATURE.json`. Hub (`server/internal/pluginpkg`) and Player (`client/plugins/fbplugin.*`) validate identically:

- compressed file at most 32 MiB, sum of uncompressed sizes at most 64 MiB, at most 2000 entries;
- every name is a relative path with `/` separators: no leading `/`, no `\`, no drive letter, no `.` or `..` segment, no empty segment, at most 255 bytes, no duplicate names (case-insensitive), no symlink entries; directory entries are ignored;
- `manifest.json` parses, `api` is 1, `id`, `version` and `entry` are valid and the entry file exists.

**`SIGNATURE.json`:** `{ "format": 1, "files": { "<path>": "<sha256 hex>" }, "signature": "<base64 Ed25519>" }`. `files` lists exactly every non-directory entry except `SIGNATURE.json` with the lowercase hex SHA-256 of its content. The signed message is the UTF-8 text `framebeam-plugin-v1\n` followed by one line `<sha256>  <path>\n` (two spaces) per file, sorted by path in byte order. The key is checked against the same trusted keys as the updates index (Hub: release key plus `-core-trust-key`; Player: the compiled-in release key). A missing signature leaves the plugin unsigned; an invalid one is reported as invalid and never counts as official.

**Origins.** `official`: valid FrameBeam signature, wherever it was installed from (index, Hub upload, file). `unverified`: unsigned, offered by the Hub. `local`: unsigned file or folder on this device; only `local` plugins need developer mode in the Player.

**Plugin index.** `plugin-index.json` (`format` 1, `generated_at`, `plugins[]` with `id`, `name`, `version`, `api`, `description`, `permissions`, `url`, `sha256`, `size`) plus a detached `plugin-index.json.sig` in the scheme of `updates-index`. `framebeam-sign plugin-sign` adds the signature to a `.fbplugin`, `plugin-add` puts a signed file into the index (it refuses unsigned files), `sign -index` writes the `.sig` ([publishing guide](../guides/plugin-publishing.md)). Hub flag `-plugin-index-url` / `FRAMEBEAM_PLUGIN_INDEX_URL` (https or `file://`); the default is empty, so a Hub without it has no catalog. The index is read at start, every 24 h and with "Check now"; a failure is shown on the Plugins page and never stops the Hub. Install and Update on the Plugins page download the file, check size and SHA-256 against the signed index and the embedded signature, and store it as `official` (source `index`). Official plugins come from their own repositories whose release workflow builds and signs the `.fbplugin`.

**Hub plugin store.** Files live under `<data-dir>/plugins/<id>/<version>.fbplugin`, metadata in the table `plugins` (migration `0012_plugins`). Every stored version is offered to all Players of the Hub:

- `GET /api/v1/plugins` (device token, feature `plugins_v1`): `PluginInfo` per version: `plugin_id`, `version`, `name`, `description`, `api`, `origin`, `sha256`, `size`, `permissions`, `requires`, `provides`, `games`, `created_at`.
- `GET /api/v1/plugins/{plugin_id}/{version}/file`: the zip bytes, `Content-Type: application/zip`, `ETag` = SHA-256; 404 when unknown.
- Admin upload in the Hub web UI (admin only; **open:** whether non-admins may upload): the Hub validates the file and shows name, version, origin and one tick per requested permission; "Offer to Players" works only with every tick set (checked on the server). The same id and version with a different SHA-256 is rejected; remove the old version first.
- Offline: `framebeam-hub import-plugins <dir>` offers every `*.fbplugin` of a folder (source `import`), like `import-cores`; a Hub without internet works completely with uploads and imports.
- Remove: deletes file and row, refused with 409 while a run that is not `finished` or `abandoned` uses that version. Copies Players already installed stay.

**Player.** "Browse plugins on your Hub" lists the offered plugins; Install downloads the file with the list's SHA-256 as pin (a mismatch installs nothing), validates it as above and stores a Hub copy under `<plugins-root>/hub/<id>/<version>/` with `origin.json`. "Install plugin from file": signed files become `official` without developer mode, unsigned ones `local` (developer mode). The developer mode also reloads a plugin from a folder on change and shows a log console.

**Runs** use exactly the plugin versions the run pins (id, version, SHA-256); a locally installed copy is not enough and never replaces the Hub version a run uses.

Official plugin repositories (created 2026-10-11, no code yet, each with its own docs and roadmap): `framebeam-plugin-pokemon-core`, `framebeam-plugin-soullink`, `framebeam-plugin-pokemon-editor`, `framebeam-plugin-cheats`, `framebeam-plugin-template`.

### 16.11 Hub: runs

Hub side built in package 3 (`server/internal/hub/runs.go`, `server/internal/httpapi/runs.go`, migration `0013_runs`; feature `runs_v1`, OpenAPI 1.15.0). Player side (`fb.run` Lua API, Runs screen): **in progress**.

**Data model.**

- **Run:** `run_id` (uuid), `name` (1 to 64 characters), `status` (`starting`, `active`, `paused`, `finished`, `abandoned`), `archived` (web only), `created_by`, `created_at`, `updated_at`, `status_by`, `settings` (JSON object up to 16 KiB, opaque to the Hub: the plugin's run rules), `plugins` (1 to 8 of `{plugin_id, version, sha256}`, each an offered plugin version at creation), `last_seq`, `participants`.
- **Participant:** `user_id`, `user_name`, `status` (`invited`, `joined`, `declined`, `left`), `game_id` (a Hub library game, set on join; the creator joins on create), `color` (0 to 7, in join order), `state` (JSON object up to 64 KiB, default `{}`), `state_rev` (starts at 0), `state_updated_at`. At most 8 participants.
- **Event** (append-only): `seq` (per run from 1, gapless), `created_at`, `user_id`, `user_name`, `device_id`, `plugin_id` (the last three null for Hub events), `source` (`automatic`, `manual`, `hub`), `type` (`^[a-z0-9_.-]{1,64}$`), `text` (up to 280 characters), `payload` (JSON object up to 4 KiB), `client_id` (uuid, unique per run, makes appending idempotent). The Hub appends `hub` events itself: `run.created`, `run.invited`, `run.joined`, `run.declined`, `run.left`, `run.status`. At most 50 000 events per run.
- **Board:** per run and plugin a building-block tree (JSON object up to 64 KiB) that a joined participant's Player publishes for the Hub web run page; `rev`, `updated_by`, `updated_at`.

The plugin computes the rules in the Player from the shared state; the Hub validates only shape and size and executes no plugin code. Run data stays on the user's Hub and never goes to another Hub (as for saves).

**Limits.** Per participant token buckets: events 2/s burst 30, state writes 4/s burst 20, board writes 1/s burst 5; exceeded: 429 `rate_limited`. `starting` and `active` runs accept events, state and board writes; `paused`, `finished` and `abandoned` refuse them with 409 `run_not_active`. Too large: 413 `too_large`; wrong shape: 400.

**REST** (device token; only participants of any status see a run, others get 404): `GET /runs` (not archived), `POST /runs` (name, plugins, `game_id`, `invite_user_ids`, `settings`; 201, creator joined, status `starting`), `GET /runs/{run_id}`, `POST .../join` (`game_id`), `.../invite` (any joined participant while the run is not over; more than 8 participants: 409), `.../decline`, `.../leave`, `.../status`, `PUT .../state` (`base_rev`, `state`; own state only; a different `base_rev` returns 409 `state_conflict` with the current `state_rev` and `state`), `POST .../events` (201 `{seq}`, the same `client_id` again 200 with the original `seq`), `GET .../events?after=<seq>&limit=<1..500, default 200>`, `PUT .../boards/{plugin_id}` (joined participant, plugin must be in the run). Status transitions: `active` from `starting` by the creator and from `paused` by any joined participant; `paused` from `active` by any joined participant; `finished` or `abandoned` by the creator; other transitions 409 `conflict`, the right transition by the wrong user 403. Users to invite come from `GET /users`. Details: [protocol reference](../reference/protocol.md).

**WSS.** `run_updated` `{run}` to every participant (all statuses) after create, invite, join, decline, leave and status change; `run_state` `{run_id, user_id, state_rev, state}` to the other joined participants and the writer's other devices; `run_event` `{run_id, event}` to every joined participant's connections. Catch-up: `hello` may carry `runs: [{run_id, last_seq}]`. After `hello_ack` the Hub sends `run_updated` (full, with states) for every non-archived run of the user, then for the listed runs the missed `run_event`s with `seq > last_seq`, at most 500 per run; when more are missing it sends none and the Player pages with `GET .../events`. Runs not listed get only `run_updated`.

**Hub web.** The pages Plugins and Runs are admin-only. Runs: list, detail with status actions (Pause/Resume, Archive, Delete), participants, the board of each plugin drawn by the JS renderer `static/plugin-blocks.js` (no framework, no external fetch, plugin strings only as text, images only `data:image/png;base64`; interactive blocks disabled, unknown block types shown as "Unsupported block"; a "Paused" banner) and the event log with a JSON export. The Hub board is read-only.

- **Deferred:** randomizer plugin (tool plugin; would run in the run creator's Player and upload only a patch as a variant of the base game). Until then, randomized ROMs are made externally; they get their own hash and therefore their own saves.

### 16.12 Security and trust model

- The Hub admin is already trusted for cores; plugins add no new kind of trust for official and admin-uploaded plugins.
- The sandbox protects against plugin bugs and third-party plugins: no file system, process or network access, bounded CPU and memory.
- Tool plugins can only be official (signed).
- Plugins never touch saves directly; `memory.write` is preceded by a save snapshot.
- Secrets never enter plugin manifests, state or logs.

### 16.13 Platforms

Lua, the building blocks and the helper functions are platform-neutral and run with the Player on Windows now and on Linux with the Linux Player after 1.0 (roadmap; macOS dropped).

### 16.14 Testing

- Synthetic data only: the in-repo test core `framebeam_test_core` gets a synthetic memory map with self-generated data. No ROMs, no RAM dumps of real games in the repository or in cloud CI.
- Hub, protocol and renderer run fully in CI.
- Real games are tested only locally by the maintainer; recordings stay local.
