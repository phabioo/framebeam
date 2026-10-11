# ADR 0022: Nintendo 3DS with Azahar (0.10)

- Status: proposed (decision pending Fabio's acceptance with the merge of the 0.10 PR)
- Date: 2026-10-10
- Deciders: Fabio (second system: 3DS with Azahar, 2026-10-07); implementation proposal by the orchestrator
- Builds on: [ADR 0013](0013-opengl-hardware-rendering.md) (OpenGL hardware rendering), [ADR 0020](0020-cores-from-the-libretro-buildbot.md) (systems and buildbot cores)

## Context

0.10 adds the Nintendo 3DS as the second system, using the `azahar_libretro` core from the libretro buildbot. The 3DS differs from the DS in four ways that the DS-shaped code did not cover: the two screens have different widths (400x240 top, 320x240 bottom), the core only runs with an OpenGL Core >= 3.3 context, its save data is a whole SD/NAND directory tree instead of one cartridge save file, and the input has more buttons (ZL/ZR, circle pad, C-stick). The milestone also carries two items from the plan: the game override UI and a ROM cache limit.

## Decisions

### D1 System, core and files

- Hub migration 0010 seeds system `3ds` ("Nintendo 3DS") with the extensions `.3ds .cci .cxi .3dsx .zcci .zcxi .z3dsx`. `.elf`, `.axf` and `.app` are deliberately not accepted.
- No default core: the admin installs `azahar` from the libretro buildbot on Systems & Cores ([ADR 0020](0020-cores-from-the-libretro-buildbot.md)). `azahar` is a profiled core. The Systems page opens on the first system that has an installed default core.
- No firmware or system files are required or shipped (firmware mode builtin, no firmware definitions). Decrypted dumps only; FrameBeam never decrypts anything. Mii and other system files are open (not wired yet).
- Player manifests: `systems/3ds.json` and `cores/azahar.json`. The core profile forces `citra_graphics_api=OpenGL`, layout default, swap Top, touch on / mouse off, `c_stick` and save path "LibRetro Default"; it locks layout, touch and save-path options and shows `citra_resolution_factor`. A manifest field `order` keeps DS first in lists.

### D2 Unequal-width screen composition via `align`

- The system manifest describes each screen's size and position; a generic per-screen `align` (for example centred) places a narrower screen inside the frame. The 3DS frame is 400x480 with the 320x240 bottom screen centred below the top screen. No 3DS-specific code path.
- Rejected: padding to the wider screen in the core options or scaling the bottom screen up to 400 px. Both distort the touch mapping or change pixel sizes.

### D3 Local-only 3DS saves via save source `none` (superseded by the D3 amendment of 2026-10-11)

- A new save source `none` means local-only. Azahar's save data is the SD/NAND tree, not a single file the Hub's opaque save model fits. With source `none` the Player keeps the data in the per-game save directory on the device, never deletes or overwrites it, and does not sync it to the Hub. The UI says so.
- Open at the time: syncing the SD/NAND tree with the Hub (a later item), now decided in the amendment below.
- Rejected: zipping the tree into one blob now. It would put an unverified format on the Hub and risk the "never overwrite saves silently" rule for a core we have not run on real hardware.

### D4 Hardware rendering is required, no software fallback

- The profile sets `requires_hw_render`. Without an OpenGL Core >= 3.3 context (including `FRAMEBEAM_DISABLE_HW_RENDER=1`) the game does not start and the Player shows a clear error. There is no software path.
- Rejected: a software renderer fallback. Azahar's software path is not usable for play and would hide the real cause.

### D5 ROM cache limit

- Setting `rom_cache_limit_bytes` in Settings: default 20 GB, 0 = unlimited. Eviction is LRU by last use, stored in the `.ok` sidecar of each cached ROM.
- Never evicted: the running or starting game, ROMs with an active download, and `.part` files.
- Trimming runs at startup, after a ROM becomes ready, when a game ends and when the limit changes. "Clear ROM cache" asks for confirmation and reports the freed size.
- 3DS dumps can be several GB, which is why the limit lands with this milestone.

### D6 Game override UI

- The Emulation page offers, per game: core choice ("Use system default" or a core of the system), speed-up settings and core options at game level (precedence game > system > global), and a reset action. This replaces the earlier "stored but no UI" state for the game-level core choice.

### D7 Input profile and gamepad default change

- New input profile `3ds`: ZL/ZR, circle pad and C-stick are appended after the 12 existing inputs, so existing indices stay stable.
- The built-in gamepad profile now binds the triggers to ZL/ZR and the left stick to the circle pad. The DS folds them back into L/R and the D-pad, so DS play is unchanged. Keyboard defaults: E/R = ZL/ZR, T/G/F/H = circle pad, I/K/J/L = C-stick.
- User profiles saved earlier keep their bindings; the new inputs must be set manually.

### D3 amendment (2026-10-11, decided by Fabio): Hub sync of the SD tree

- Save source `core_tree` (format `azahar_sdmc`) replaces `none` for Azahar. Azahar keeps its virtual SD/NAND tree in the per-Hub/user/game/slot short dir `<data>/c/<16 hex>/Azahar/`.
- The Player packs only the game's save data into one deterministic, uncompressed ustar archive, with paths relative to the `Azahar` folder: `sdmc/Nintendo 3DS/<id0>/<id1>/title/<high>/<low>/data/**` (the save archive including `.metadata`) and `sdmc/Nintendo 3DS/<id0>/<id1>/extdata/**`. The short dir is per game and slot, so everything there belongs to that game; no ExHeader parsing. Never synced: `nand/` (system data such as config and eventlog), `sysdata/`, `shaders/`, `cheats/`, installed content (`title/.../content`).
- The archive is the slot's save on the Hub (opaque, same 64 MiB limit) with the same revisions, history, snapshots, conflicts and restore as DS saves. It is converted around the core like DeSmuME `.dsv` (ADR 0020 D7): imported from the tree at start, during play (polling) and at the final sync; exported into the tree before the core starts.
- Before the tree is replaced by a different Hub version, the current tree is written as `<slot dir>/<rom>.sav.tree-<timestamp>.bak`. Unmanaged files are never touched. An invalid archive blocks the start and leaves the tree untouched. Existing local-only 3DS saves are adopted and uploaded on the first start with the new Player.
- Restore, upload and conflict resolution while a 3DS game runs restart the game with the new save (as for NDS): the archive is validated first (invalid: refused, nothing changes), then the game is stopped, the current tree is imported into the raw save, the new raw save is written, the old tree is backed up (`.tree-*.bak`) and replaced, and the game starts again. An uploaded file ("Upload save file...") for a 3DS game must be such a tar.
- Rejected (unchanged from D3): a Hub-side format. The archive stays opaque to the Hub.

## Consequences

- Verified by tests and CI only. Only Fabio can verify: a real Azahar run on his RTX 4080 (GL 3.3 core context), frame time including Session encoding, the resolution factor, the option values against the real core, and Mii/system files.
- 3DS saves sync with the Hub since the D3 amendment (2026-10-11); only the game's save data and extdata, not system data (`nand/`, `sysdata/`) or Mii/system files.
- The 3DS needs a considerably faster CPU/GPU than the DS; whether play plus encoding fits is open until measured.
