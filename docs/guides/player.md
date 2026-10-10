# Using the Player

The FrameBeam Player (`client/`) connects to a Hub, keeps your library and saves in sync, runs games locally and shares or watches Sessions. Feature overview: [features.md](../features.md). Internals: [player-internals.md](../reference/player-internals.md).

## Install

- **Windows:** run the installer from the [GitHub releases](https://github.com/phabioo/framebeam/releases): the MSI `framebeam.msi` (CI artifact `framebeam-windows-x64-msi`) or the `framebeam-player-<version>-windows-x64-setup.exe` that installs it; the portable zip works too. The default is a per-user Player install, no admin rights; an existing Inno install is replaced and its data kept. The installer is unsigned (SmartScreen warning). Neither ships an emulator core: the Player downloads the core for a system from the Hub on first use. Installer details: [packaging.md](packaging.md).
- **Linux:** builds and runs for development ([development.md](../development.md)). Credentials are kept in memory only, so pair again after a restart.

## Set up a Hub on this PC

On the connection screen, "Set up a Hub on this PC" runs a Hub on the same computer without a pairing code ([ADR 0021](../adr/0021-one-windows-installer.md)):

1. If no local Hub is installed, the Player downloads the MSI of its own version, checks size and SHA-256 and runs it elevated (UAC prompt) with the Hub feature. The Player then moves to Program Files; its data is copied once from the old portable folder and never overwritten.
2. You choose an admin name and password; the Player creates the first admin and pairs over localhost. The certificate is trusted automatically only for `127.0.0.1` on the registered port. With an existing local Hub, "Connect" asks for an admin's credentials instead.
3. Standalone mode: network sharing starts off, so the Hub listens only on this PC. Pick a ROM folder (the Hub's import folder; the Player asks for admin rights once to give the Hub service read access) and "Rescan folder" in the Library. Switch "Network sharing" on under Settings > Hubs ("This PC's Hub") or in the Hub web settings to let other Players connect. Cores come from the libretro buildbot through this Hub.

## Connect and pair

1. Start the Player and enter the Hub address (or pick a saved Hub profile; auto-connect is available).
2. On first contact the Player shows the Hub's certificate fingerprint. Compare it with the Hub's log or Settings page and confirm (trust on first use). On a mismatch the connection is blocked and no credential is sent.
3. Request pairing; an admin allows it on the Hub's Clients page. Alternatively choose "I have an invite code" and enter the code and your display name: the Hub creates a user for you without a password.
4. Credentials are stored in the Windows Credential Manager (Linux: memory only).

After a Hub certificate change (renewal) the Hub card shows the stored and the presented fingerprint. "Trust new certificate", then "Yes, trust this certificate" re-pins it and keeps the credential (CLI: `--accept-fingerprint <sha256>`). Settings → Hubs switches, edits (address, port), removes and sets auto-connect for Hubs.

## Play

- The Library lists the Hub's games with search and filter chips. ROMs are downloaded into a hash-verified, resumable cache. Start a game; the Player fetches the core on demand (shown as core loading / missing / incompatible).
- If the Hub allows user uploads, "Upload ROM" in the Library header adds your own (homebrew or own-dump) ROMs. An uploaded ROM is kept in the local ROM cache, so it starts without downloading.
- Firmware (NDS): in native firmware mode the Player fetches the firmware files the admin provided and shows "Firmware required" / "Firmware missing" instead of launching without them.
- Keyboard: Arrows, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter=Start, Backspace=Select, Esc=Pause (F11 fullscreen, F3 diagnostics, Space speed-up; the Player keys are configurable in Controllers > Hotkeys). Mouse = DS touch. Gamepads and remapping: Controllers page.
- Speed-up: Space, configurable in Controllers > Hotkeys (or the header button "Speed-up") runs the game faster, with "Speed-up ×N" shown while on. "Speed-up speed" in the Session panel picks 1.5×, 2×, 3×, 4×, 6× or 8× for the running game (narrow windows show the button as "»"). Emulation page settings (global, system or game): "Speed-up speed" (default 2×), "Speed-up on start" (default off; the game starts sped up and stays so until toggled) and "Audio during speed-up" (default on: audio is resampled, so the pitch rises; off mutes it). The button is hidden if the core does not allow it. It also works while your Session is shared; viewers see the sped-up game at the normal frame rate (audio follows the same setting). Saves sync as usual.
- Core choice ([ADR 0020](../adr/0020-cores-from-the-libretro-buildbot.md)): the Hub admin installs the cores; the Emulation page offers a "Core" select per system with the cores the Hub serves. The effective core is game > system > Hub default; a stored choice the Hub no longer serves is ignored with a notice. Per game the select offers "Use system default" or a core of the system (0.10). Core options stay per core.
- Experimental cores: a core without a FrameBeam profile (badge "experimental") runs without option defaults or locks, shows the framebuffer as one screen, has no touch input and only gets firmware placed in its system directory.
- Core change and saves: when a game starts with another core or another build than the one that last wrote its save, the Player first creates a manual snapshot on the Hub ("Before core change: ..." in the save history) and shows a notice. If the snapshot cannot be created (Hub unreachable) the game does not start and the save stays untouched; without Hub snapshot support a local backup copy is made. Switch back to the previous core to start offline.
- Save files per core: the Hub always holds the raw cartridge save. melonDS DS stores it as `<game>.sav`; DeSmuME keeps its own `<game>.dsv` (raw save plus a DeSmuME footer), which the Player converts before the start and after play. Only the file of the running core is synced, so switching cores never flips the Hub save between formats. A `.dsv` that cannot be read is never uploaded and never overwritten; the game reports it and your Hub save stays unchanged.
- Emulation page: core options per global / system level (options FrameBeam controls are hidden). melonDS DS offers an OpenGL renderer and internal resolution (applied on next start; default Software). `FRAMEBEAM_DISABLE_HW_RENDER=1` forces the software path.
- Game overrides (0.10): the Emulation page edits core, speed-up and core options at Game level (game > system > global); "Reset" removes the game's overrides.
- Nintendo 3DS ([ADR 0022](../adr/0022-nintendo-3ds-with-azahar.md)): needs the `azahar` core installed by the Hub admin and an OpenGL Core >= 3.3 context; without it the game does not start and the Player shows an error (no software fallback; `FRAMEBEAM_DISABLE_HW_RENDER=1` therefore also blocks 3DS games). Use decrypted dumps (`.3ds .cci .cxi .3dsx` and the compressed `.zcci .zcxi .z3dsx`); FrameBeam never decrypts and ships no firmware. Layout: top 400x240, bottom 320x240 with touch by mouse. Keyboard: E/R = ZL/ZR, T/G/F/H = circle pad, I/K/J/L = C-stick. Gamepad: triggers = ZL/ZR, left stick = circle pad (the DS folds them back into L/R and the D-pad). Earlier user profiles keep their bindings; set the new inputs manually in Controllers.
- 3DS saves are local-only: they stay in the game's save directory on this device, are never deleted or overwritten, and are not synced to the Hub yet (the UI says so).
- ROM cache: Settings → "ROM cache limit" (default 20 GB, 0 = unlimited). The least recently used ROMs are removed first; the running or starting game, active downloads and `.part` files are never removed. Trimming runs at startup, when a ROM is ready, when a game ends and when the limit changes. "Clear ROM cache" asks for confirmation and shows the freed size.
- Settings: Appearance (Dark / Light / System), Updates (channel, automatic install), Hubs.

Saves sync automatically; see [saves.md](saves.md). Sharing and watching: [sessions-over-the-internet.md](sessions-over-the-internet.md).

## Data directory

The ROM cache, `profiles.json`, `device.json`, `hubs/<id>/users/<user_id>/saves/<game_id>/`, `system/` and settings are stored portable by default in `<directory of the executable>/data`, provided a real write test succeeds there. Otherwise the Player falls back to AppData (`QStandardPaths::AppDataLocation`) and logs a note, for example when installed under Program Files. On the first portable start existing AppData data is copied once (nothing is moved, deleted or overwritten; `device_id` is preserved; the ROM cache is re-downloaded). Credentials stay in the OS credential store. `--data-dir <path>` or `FRAMEBEAM_DATA_DIR` take precedence ([ADR 0004](../adr/0004-player-portable-data.md)).

## Command line and environment

```sh
framebeam_player [--data-dir <path>] [--dev-allow-http] [--version | --version-json] [--help]
framebeam_player_cli <command> ...            # run without arguments for the usage text
```

`--dev-allow-http` allows HTTP Hubs outside localhost (development only). The CLI covers `identify`, `pair`, `redeem-invite <address> --code FB-XXXX-XXXX --name <name>`, `games`, `fetch-rom`, `game upload <file> [--title T]`, `systems`, `fetch-core <system-id>`, `saves list`, `save push|pull|resolve`, `session-share --synthetic`, `session-watch` (both accept `--force-relay`) and `update-check`; the CLI has no hardware rendering. `scripts/e2e-player-hub.sh` checks the CLI against a locally built Hub, including a save round trip.

| Variable | Meaning |
|---|---|
| `FRAMEBEAM_DATA_DIR` | Data directory (same as `--data-dir`) |
| `FRAMEBEAM_FORCE_RELAY=1` | Force the TURN relay for Sessions (testing; the CLI also has `--force-relay`) |
| `FRAMEBEAM_DISABLE_HW_RENDER=1` | Force the software rendering path of cores |
| `FRAMEBEAM_DISABLE_GPU_ENCODE=1` | Kill switch for GPU-direct encoding of shared Sessions ([ADR 0019](../adr/0019-gpu-direct-nvenc.md)); the Session encoder gets read-back frames |
| `FRAMEBEAM_H264_ENCODER=<name>` | Force one H.264 encoder (for example `h264_nvenc` or `libx264`); keeps the readback path, also for `h264_nvenc`, so it serves as the baseline when comparing against GPU-direct |
| `FRAMEBEAM_SYNC_READBACK=1` | Force synchronous readback of hardware-rendered frames instead of double-buffered PBOs ([ADR 0013](../adr/0013-opengl-hardware-rendering.md) D3) |
| `FRAMEBEAM_PLAYER_UPDATE_INDEX_URL`, `FRAMEBEAM_PLAYER_TRUST_KEYS` | Update index URL and extra trusted keys for the update index only (tests); cores are not signed |
| `FRAMEBEAM_MELONDS_DS_CORE`, `FRAMEBEAM_DESMUME_CORE` | Core path for tests (build time / tests only) |
| `FRAMEBEAM_SCREENSHOT_DIR` | Output directory of the screenshot tests |
