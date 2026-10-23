# ADR 0031: Game variants and the randomizer (0.21)

- Status: Proposed. Fabio decided on 2026-10-23: a randomizer for every Pokémon generation from GB to 3DS, started from the game page and from New run (no sidebar row), before 1.0, each app builds its own variant, the engine is managed by Uzume and operated from a plugin. On the same day he accepted the design (canvas sheet 5 "Randomizer", [spec](../design/uzume/randomizer.md)) and the milestone order (option A: Randomizer is 0.22, Plugins move to 0.23, Platforms to 0.24; see the amendment under Consequences). The details below follow the assessment of 2026-10-23; the spike decides D3 and D4.
- Date: 2026-10-23
- Deciders: Fabio

## Context

Friends want a SoulLink or Nuzlocke run where each person plays their own randomized game. Up to 0.18 Uzume cannot do that:

- A randomized ROM keeps the header of the original, so the library merges it into the original game by identity ([ADR 0025](0025-local-roms.md) D1), and `chooseRomSource` (`client/network/romsource.cpp`) starts the primary file first. Whoever drops a randomized ROM into a library folder may play the original. This is why everyone plays the same game today.
- Run participants carry only a `game_id`, no variant.
- Plugins cannot start a program: the manifest rejects `type: "tool"` and the permission `tool.exec` ([12-plugins.md](../architecture/12-plugins.md) 16.4, 16.5).
- Plugins have no sidebar entries since [ADR 0030](0030-one-app-one-server-uzume.md) D7.
- `fb.rom.romfs` reads the RomFS of the game file, so a 3DS LayeredFS mod is invisible to plugins.
- Uzume does not install or check 3DS updates.

Randomizing itself is not rewritten. The Universal Pokémon Randomizer FVX (successor of UPR ZX, GPL-3.0, Java with its own runtime) supports the main games of Gen 1 to 7 on GB, GBC, GBA, NDS and 3DS and has a command line with a settings string and a fixed seed (`cli -i <in> -o <out> -S <settings> -z <seed> [-d] [-u <update>] [-l]`). A Lua port would not fit the sandbox limits and is out of proportion. The Azahar libretro core already loads LayeredFS mods from `<user dir>/load/mods/<title id>/`, and Uzume gives Azahar one user dir per game and slot ([ADR 0022](0022-nintendo-3ds-with-azahar.md)).

## Decisions

### D1 Game variants

- A **variant** is a library game plus a **recipe**: tool and tool version, preset or settings string, seed, the SHA-256 of the base dump (GB to NDS) or the 3DS update version, and the owner. The server stores variants and recipes; it never stores ROM data, patches or spoiler logs of a variant.
- Every variant plays in its **own save slot** named like the variant; a run variant plays in the run slot "Run: <Name>". Main is never touched. Deleting a variant asks and keeps the saves by default (10a-2); the recipe stays with kept saves so the variant can be built again.
- Starting a variant starts exactly that variant, never another dump of the base game. The library shows one row per game with a variant count (10g-1); the slot picker lists Main, the variants and the runs (10g-2).
- Variants are useful without the randomizer too (externally patched ROMs, fan patches); that use is not built in 0.21.

### D2 Output per generation

- **Gen 1 to 5 (GB, GBC, GBA, NDS):** the output is a whole ROM. The app keeps it in a local variant cache with a size limit next to the ROM cache (Settings › This PC › Storage, 10g-3) and may drop it at any time, because the recipe rebuilds it. The result depends on the exact base dump (revision, trimmed or not), so the recipe pins its SHA-256; a different dump gets a clear state ("Download the run's file", 10d-1d, 10f-5a) instead of a different world.
- **Gen 6 and 7 (3DS):** the output is a LayeredFS folder written into the Azahar user dir of the variant's slot (`load/mods/<title id>/`). The result depends on the title and the update version, not on the dump. Save sync keeps packing only `sdmc`, so the mod never travels to the server.
- `fb.rom.*` reads the file that was started (Gen 1 to 5) and lays the mod files over the RomFS (3DS), so plugins such as SoulLink Core see the randomized data.

### D3 Tools catalog

- The randomizer engine is a **tool** that the server offers to apps the way it offers cores ([ADR 0020](0020-cores-from-the-libretro-buildbot.md)): the server downloads a release per platform from its official source, pins it by SHA-256 at the first download, keeps versions while a variant or run uses them, and imports offline. Apps download tools only from their server. The admin page is Server › Tools (10h).
- FVX is never checked into an Uzume repository. Its license and source link appear in the tool entry and in the notices of whatever ships it.
- The app starts a tool with a fixed argument list and no shell, with progress, cancel, a time and a memory limit, in the background (10c). 3DS needs up to 4 GB of Java heap and a few minutes; a Raspberry Pi server never runs the tool.
- Open: the exact catalog format and whether tools share the plugin index signing; decided in the package "Tools".

### D4 Each app builds its own variant

- No ROM or patch travels between apps or over the server. A run stores the recipe, the pinned engine version, the base dump pin or 3DS update, and a **run seed**.
- **Each person gets their own:** each participant's seed is derived from the run seed and their user ID. **Same world for everyone:** all use the run seed. Nobody sees the seed of a run (10e).
- Before "Play run" the app builds the participant's variant once (10f-3). Own variants may join a run only if the run allows them and they use the run's engine version (10f-2, 10f-5d). The run pins the 3DS update like the engine: a run variant is never rebuilt for a newer update while the run lasts (10d-4g).
- Determinism (same base, settings, seed and engine version give the same bytes) is the assumption this rests on; the spike proves it per system before anything is built. The exact seed derivation is decided in the package "Runs with variants".

### D5 Plugin and UI

- The official plugin **Randomizer** (repository `uzume-plugin-randomizer`) holds the presets, the Randomize dialog, the run setting and the variant list. It has no sidebar row: Randomize… sits next to Play on the game page (10a-1) and the Randomizer section in New run (10e-1).
- No generic `tool.exec`: the plugin asks Uzume to build a variant from a recipe; Uzume runs the tool. Only signed official plugins get this permission. Open: the permission name and the Lua call; decided in the package "Tools".
- Spoilers stay hidden: the app never shows what changed; the spoiler log is off by default, opens only from a variant's menu after a confirm, and for run variants only after the run ends and only the own one. Error details carry no spoiler. "FVX" appears only in 10 px Mono lines; copy says "the engine".

### D6 3DS updates

The 3DS update a variant is built for must match the one the app plays. Uzume gets a way to keep 3DS updates on the server and in the app (10b-3d "Add update 1.4 … on Home server"); until then, randomizing and playing without an update works.

## Rejected

- **A sidebar row "Randomizer":** against ADR 0030 D7; every randomization starts from a game anyway.
- **The run creator builds all variants and uploads patches:** patches are ROM derivatives on the server, BPS patches fit only the same dump, and the creator would need every game locally. May come later as a fallback for someone who cannot run the tool.
- **The server builds variants:** the server does not have local ROMs, a Pi cannot build 3DS variants, and whole CXI outputs are 1.7 to 3.5 GB.
- **A randomizer in Lua:** sandbox limits (64 MiB, 500 ms per call) and size.
- **UPR ZX or pk3DS:** ZX has no seed or settings string on the command line; pk3DS is 3DS only and GUI only.

## Consequences

- New milestone 0.22 "Randomizer" ([roadmap](../roadmap.md)); Plugins move to 0.23, Platforms to 0.24.
- Amendment (2026-10-23): renumbered to 0.21 by Fabio; Plugins are 0.22, Together 0.23.
- New server data (variants, recipes, run seed, tools catalog), new OpenAPI version and a migration; a new app storage area (variant cache).
- [12-plugins.md](../architecture/12-plugins.md) 16.1, 16.4, 16.5 and 16.11 point here; the tool plugin type is replaced by the narrow build permission of D5.
- SoulLink needs the variant in the run view and reads encounter data from the variant; Gen 1 to 5 SoulLink runs also need SoulLink Core adapters for those games (plugin repositories).
- Tests use synthetic files and the test core only; no ROMs, patches or spoiler logs in any repository.
