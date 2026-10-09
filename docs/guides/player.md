# Using the Player

The FrameBeam Player (`client/`) connects to a Hub, keeps your library and saves in sync, runs games locally and shares or watches Sessions. Feature overview: [features.md](../features.md). Internals: [player-internals.md](../reference/player-internals.md).

## Install

- **Windows:** run the installer `framebeam-player-<version>-windows-x64-setup.exe` from the [GitHub releases](https://github.com/phabioo/framebeam/releases) (CI artifact `framebeam-player-windows-x64-setup`); the portable zip works too. Per-user install, no admin rights; the installer is unsigned (SmartScreen warning). Neither ships an emulator core: the Player downloads the core for a system from the Hub on first use. Installer details: [packaging.md](packaging.md).
- **Linux:** builds and runs for development ([development.md](../development.md)). Credentials are kept in memory only, so pair again after a restart.

## Connect and pair

1. Start the Player and enter the Hub address (or pick a saved Hub profile; auto-connect is available).
2. On first contact the Player shows the Hub's certificate fingerprint. Compare it with the Hub's log or Settings page and confirm (trust on first use). On a mismatch the connection is blocked and no credential is sent.
3. Request pairing; an admin allows it on the Hub's Clients page. Alternatively choose "I have an invite code" and enter the code and your display name: the Hub creates a user for you without a password.
4. Credentials are stored in the Windows Credential Manager (Linux: memory only).

After a Hub certificate change (renewal) the Hub card shows the stored and the presented fingerprint. "Trust new certificate", then "Yes, trust this certificate" re-pins it and keeps the credential (CLI: `--accept-fingerprint <sha256>`). Settings → Hubs switches, edits (address, port), removes and sets auto-connect for Hubs.

## Play

- The Library lists the Hub's games with search and filter chips. ROMs are downloaded into a hash-verified, resumable cache. Start a game; the Player fetches the core on demand (shown as core loading / missing / incompatible).
- If the Hub allows user uploads, "Upload ROM" in the Library header adds your own (homebrew or own-dump) ROMs.
- Firmware (NDS): in native firmware mode the Player fetches the firmware files the admin provided and shows "Firmware required" / "Firmware missing" instead of launching without them.
- Keyboard: Arrows, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter=Start, Backspace=Select, Esc=Pause (F11 fullscreen, F3 diagnostics, Space speed-up; the Player keys are configurable in Controllers > Hotkeys). Mouse = DS touch. Gamepads and remapping: Controllers page.
- Speed-up: Space, configurable in Controllers > Hotkeys (or the header button "Speed-up") runs the game faster, with "Speed-up ×N" shown while on. "Speed-up speed" in the Session panel picks 1.5×, 2×, 3×, 4×, 6× or 8× for the running game (narrow windows show the button as "»"). Emulation page settings (global, system or game): "Speed-up speed" (default 2×), "Speed-up on start" (default off; the game starts sped up and stays so until toggled) and "Audio during speed-up" (default on: audio is resampled, so the pitch rises; off mutes it). The button is hidden if the core does not allow it. It also works while your Session is shared; viewers see the sped-up game at the normal frame rate (audio follows the same setting). Saves sync as usual.
- Core choice ([ADR 0020](../adr/0020-cores-from-the-libretro-buildbot.md)): the Hub admin installs the cores; the Emulation page offers a "Core" select per system with the cores the Hub serves. The effective core is game > system > Hub default; a stored choice the Hub no longer serves is ignored with a notice. A game-level choice can be stored but has no UI yet. Core options stay per core.
- Experimental cores: a core without a FrameBeam profile (badge "experimental") runs without option defaults or locks, shows the framebuffer as one screen, has no touch input and only gets firmware placed in its system directory.
- Core change and saves: when a game starts with another core or another build than the one that last wrote its save, the Player first creates a manual snapshot on the Hub ("Before core change: ..." in the save history) and shows a notice. If the snapshot cannot be created (Hub unreachable) the game does not start and the save stays untouched; without Hub snapshot support a local backup copy is made. Switch back to the previous core to start offline.
- Emulation page: core options per global / system level (options FrameBeam controls are hidden). melonDS DS offers an OpenGL renderer and internal resolution (applied on next start; default Software). `FRAMEBEAM_DISABLE_HW_RENDER=1` forces the software path.
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
