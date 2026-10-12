# ADR 0024: System template and Game Boy, Game Boy Color, Game Boy Advance (0.13)

- Status: proposed (acceptance follows Fabio's local test with real mGBA and Gambatte runs and real games)
- Date: 2026-10-12
- Deciders: Fabio (no core preselection and the two shipped profiles, 2026-10-12; milestone and goal "a new system costs minimal effort", 2026-10-11); implementation proposal by the orchestrator
- Builds on: [ADR 0020](0020-cores-from-the-libretro-buildbot.md) (systems and buildbot cores), [ADR 0022](0022-nintendo-3ds-with-azahar.md) (second system, input profile `3ds`)

## Context

Until 0.12 every system was spread over code: a Hub SQL migration per system (`nds` in the first migrations, `3ds` in migration 0010), a Player system manifest, and input tables compiled into the Player. 0.13 makes a system mostly data and proves it with Game Boy (`gb`), Game Boy Color (`gbc`) and Game Boy Advance (`gba`), which the Pokémon Gen 1-3 plugins also need. The step-by-step procedure is in [Adding a system](../guides/adding-a-system.md).

## Decisions

### D1 Hub: one embedded JSON per system, reconciled into SQLite

- `server/internal/hub/systems/<id>.json` (fields `id`, `display_name`, `extensions`, `libretro_ids`, `platforms`, `provisioning`, `input_profile`, `display_profile`, `firmware[]`, `profiled_cores`) is embedded in the Hub and validated at load (`systemdefs.go`); an invalid file makes `hub.Open` fail (`reconcileSystems` returns the error), so the Hub does not start; the Go tests load every embedded file and catch it first.
- At Hub start the definitions are reconciled into SQLite: new systems are inserted (firmware mode `builtin`, no default core), definition columns and firmware definitions are updated. Admin state (default core, firmware mode, installed cores, uploaded firmware, pins) is never touched. Firmware "required" comes from the admin's firmware mode, not from the file. The `nds` and `3ds` rows from older migrations stay and are reconciled like the others. No SQL migration per system any more.
- Rejected: one SQL migration per system. Every system would again need a schema version, a Hub release and a hand-written seed that can drift from the Player manifest; definition fixes could not reach existing databases without yet another migration.

### D2 Player: system manifest carries input, identity and display

- `client/emulation/manifests/systems/<id>.json` additionally holds `input` (`map` from FrameBeam input id to libretro target, optional `labels`), `identity` (`nds_header`, `3ds_ncsd`, `gb_header`, `gba_header`) and `display.layout` (`single` for one screen). Input tables are registered from the manifests; systems that share an `input_profile` must have identical maps (checked at load). Manifests are globbed by CMake.
- A Go test cross-checks the Hub definitions against the Player manifests (names, extensions, firmware ids and files, profiled cores), so the two copies cannot drift.
- Rejected: generating one file from the other. The Hub and the Player are built and shipped separately; a check keeps both sides self-contained.

### D3 Two Game Boy systems

- `gb` (`.gb`, 160x144, boot ROM `gb_bios.bin` optional) and `gbc` (`.gbc`, `gbc_bios.bin` optional) are separate systems; `gba` (`.gba`, 240x160, `gba_bios.bin` optional) is the third. Saves use `SAVE_RAM` (`.sav`).
- Rejected: one combined GB/GBC system. The extensions, boot ROMs and plugin identity differ, and the Systems & Cores page, the library and per-system core settings are keyed by system. Splitting costs two small JSON files.

### D4 Core discovery by `systemid` and extensions

- A buildbot core is offered for a system when its info `systemid` is in the system's `libretro_ids` and its `supported_extensions` are empty or share an extension with the system. mGBA (`systemid` `game_boy_advance`, extensions `gb|gbc|gba`) is therefore offered for `gb`, `gbc` and `gba`; gpSP only for `gba`; Gambatte and SameBoy only for `gb` and `gbc`. Libretro ids: `gb` and `gbc` list `game_boy`, `game_boy_color`, `game_boy_advance`; `gba` lists `game_boy_advance`.
- A core shared by several systems is one installed build: installing it into a second system attaches the installed version, Update (manual and automatic) moves all systems of that core, and a package version is deleted only when no system references it. `import-cores` installs into every matching system.
- Rejected: discovery by `systemid` alone. It would offer gpSP and mGBA for the GB systems by accident and, in the other direction, hide mGBA from GB and GBC because its `systemid` says GBA.

### D5 No default core on the Hub, profiles on the Player

- The Hub never preselects a core for a system (Fabio, 2026-10-12); the admin installs what the buildbot offers. The Player ships core profiles only (option defaults and locks, firmware switch). A core without a profile runs as experimental ([ADR 0020](0020-cores-from-the-libretro-buildbot.md) D6). Profiles shipped: `mgba` (`gb`, `gbc`, `gba`) and `gambatte` (`gb`, `gbc`), chosen by Fabio.
- mGBA: `mgba_sgb_borders` locked OFF (borders would change the frame to 256x224), BIOS switch `mgba_use_bios`. Gambatte: the link cable network options are locked (cores get no network access), BIOS switch `gambatte_gb_bootloader`.
- Rejected: a Hub-side default core per system. It hides a decision that depends on the admin's preference and on what the buildbot offers, and the Hub would carry a second list that must follow the buildbot.

### D6 Game identity for plugins

- `gba_header` yields kind `gba-gamecode` (4-character game code at `0xAC`); `gb_header` yields kind `gb-title` (header title at `0x134`, only when the header checksum at `0x14D` matches). Offsets as implemented in `game_identity.cpp`: GBA game code `0xAC`, maker `0xB0`, version `0xBC`, title `0xA0` (12 bytes); GB/GBC title `0x134` (11 bytes when the CGB flag at `0x143` is set, else 16), checksum over `0x134..0x14C`. The header layouts follow the public cartridge header documentation (GBATEK for GBA, Pan Docs for GB/GBC), cited in the code comments. A new header format needs a reader in `client/emulation/game_identity.cpp`, the only code step when adding a system.

## Consequences

- Adding a system is two JSON files plus, optionally, a core profile; no migration, no CMake edit.
- Open: acceptance with real cores and real games (local only, not in CI); the Pokémon Gen 1-3 plugins are separate work in the plugin repositories.
