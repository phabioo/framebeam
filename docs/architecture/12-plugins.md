# Architecture: plugins (planned)

## 16. Plugin interface [Planned for 0.11 – not implemented]

Status: design only. Decisions: [ADR 0023](../adr/0023-plugins.md) (proposed). Nothing here exists in code yet; where this file names code, it names today's extension points. Open items are marked **open**.

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
| Memory | `client/emulation/libretro_backend.cpp` | Resolves `retro_get_memory_data/size`, uses only `RETRO_MEMORY_SAVE_RAM`; rejects `RETRO_ENVIRONMENT_SET_MEMORY_MAPS` (~line 825) | Accept memory maps, expose system RAM and mapped regions through a new method on the backend interface |
| Backend interface | `client/emulation/emulator_backend.h` (`EmulatorBackend`) | No memory access | Read access by virtual address |
| Threading | `EmulationRunner` | Memory is valid only on the emulation thread | Snapshot copy after `runFrame()` every N frames; the plugin thread works on the copy |
| Game start | `GameStarter::launch` in `client/ui/gamestarter.cpp` | Knows game, ROM path, SHA-256 | Also provide the header game code (GB/GBA header, NDS game code, 3DS title ID) |
| Saves | keyed by `romSha256` (`gamestarter.cpp`) | A changed ROM (for example externally randomized) gets its own saves | Unchanged; "never overwrite saves silently" stays |
| Hub channel | WSS envelope, `protocol/schemas/ws-envelope.schema.json` (`type` + `payload`) | No plugin messages | New message types for plugin/run state and events |
| Peer channel | DataChannel `fb-diag` | Exists only while someone watches the Session | Not suitable for run state |
| Distribution | Core distribution with SHA-256 pin ([ADR 0020](../adr/0020-cores-from-the-libretro-buildbot.md)); signed `updates-index`, `server/cmd/framebeam-sign` | Cores and updates | Reuse the pin and the signing for plugins |
| UI | `client/ui/*.qml`, `Fb*` components, `Theme.qml`; Hub web: Go templates + htmx (`server/internal/web`) | No plugin slots | Slots, building-block renderer in Qt and in JS |

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
| `ui` | Provide building-block trees for slots | first release |
| `run.state` | Read/write per-player run state, append run events | first release |
| `memory.write` | Write memory through a write queue | later (cheats, editor) |
| `tool.exec` | Start an external program | later, signed official only |

`memory.write`: all plugins' writes go through one queue applied on the emulation thread between two frames in a fixed order. If two plugins persistently set the same address, the Player shows the conflict instead of silently letting one win. Before the first write in a play session the Player takes a save snapshot (the Hub already versions saves). In a run, an active cheat writes a "cheat used" event to the run log, visible to all participants; the run decides whether cheats are allowed.

Not planned: sending input, free network access.

The Player shows name, origin (Official / Hub upload / local) and permissions before a plugin is switched on.

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
- UI: several plugins in one slot get tabs (side panel), are stacked (HUD, tile overlay) or listed separately (sidebar, settings, hotkeys). Hotkey conflicts are reported by the Hotkeys tab as today.
- Management: Settings → Plugins lists version, origin, permissions, dependencies and a switch per plugin, per game and per run.

### 16.8 UI

Plugins ship **no QML** (QML can run JavaScript and load native modules, which would void the sandbox and let the look drift). A plugin describes its UI as a tree of declarative building blocks; the Player renders it with its own `Fb*` components, and a small JS renderer in the Hub web UI draws the same tree. On state changes the plugin sends only the changed values.

Building blocks v1 (catalog **open**, driven by the official plugins):

- Layout: column, row, grid, tabs, scroll area, card
- Content: text, label, chip/badge, icon, image/sprite, progress/HP bar, table/list
- Interaction: button (calls a plugin action), toggle, select, text field, confirmation dialog
- Map view
- Settings generated from the manifest schema

A new building block needs a Player update; the `api` version keeps that clean. A second tier with signed plugin QML is conceivable only if the building blocks provably do not suffice.

Slots:

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

Map view: a background image, markers at coordinates (player symbols in the player's colour, sprites, state), zoom and pan with mouse and controller, selection details and filters. Positions are at location or route level (map ID), not exact overworld position.

### 16.9 Assets

Game artwork (sprites, icons, region maps) belongs to the rights holders. It is **never** in any repository. A plugin extracts it at runtime from the user's own game files with `rom.read` and the native helpers and stores it in a local plugin cache. Repositories hold only facts the project records itself (for example location tables and map coordinates). Falling back to user-supplied asset packs is possible but not the default.

### 16.10 Distribution

- Official plugins: one repository each, with a release workflow that builds a `.fbplugin` (zip with manifest, Lua, own assets) and signs it with the FrameBeam release key. A signed **plugin index** (like `updates-index`) lists them with versions and SHA-256; the Hub reads it and the admin installs or updates with one click. Signed plugins carry an "Official" badge.
- Hub upload: an admin uploads a plugin; it is available to all users of that Hub and marked "Unverified" unless signed; the admin confirms the permissions on upload. A Hub without internet works completely with uploads (like `import-cores`).
- Local install: "Install plugin from file" in the Player for development; a developer mode reloads a plugin from a folder on change and shows a log console.
- Runs: the Hub delivers the same plugin version to all participants; a locally installed copy is not enough.

Planned official repositories (not created yet, each with its own docs and roadmap): `framebeam-plugin-pokemon-core`, `framebeam-plugin-soullink`, `framebeam-plugin-pokemon-editor`, `framebeam-plugin-cheats`, `framebeam-plugin-template`.

### 16.11 Hub: runs

- **Run** object: name, plugins with versions, participants (Hub users, invited as for Sessions), game or variant per participant, status.
- Per-player **state** (JSON) and an **append-only event log**, with size and rate limits. The plugin computes rules in the Player from the shared state; the Hub validates only shape and size and executes no plugin code.
- Fan-out over WSS to all connected participants; on `hello` a participant who was offline receives the current state and missing events.
- Data stays on the user's Hub and never goes to another Hub (as for saves).
- **Deferred:** randomizer plugin (tool plugin; would run in the run creator's Player and upload only a patch as a variant of the base game). Until then, randomized ROMs are made externally; they get their own hash and therefore their own saves.

### 16.12 Security and trust model

- The Hub admin is already trusted for cores; plugins add no new kind of trust for official and admin-uploaded plugins.
- The sandbox protects against plugin bugs and third-party plugins: no file system, process or network access, bounded CPU and memory.
- Tool plugins can only be official (signed).
- Plugins never touch saves directly; `memory.write` is preceded by a save snapshot.
- Secrets never enter plugin manifests, state or logs.

### 16.13 Platforms

Lua, the building blocks and the helper functions are platform-neutral and run with the Player on Windows now and Linux/macOS later (roadmap 0.13).

### 16.14 Testing

- Synthetic data only: the in-repo test core `framebeam_test_core` gets a synthetic memory map with self-generated data. No ROMs, no RAM dumps of real games in the repository or in cloud CI.
- Hub, protocol and renderer run fully in CI.
- Real games are tested only locally by the maintainer; recordings stay local.
