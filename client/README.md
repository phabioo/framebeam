# client: FrameBeam Player

C++20/Qt 6 application: Hub profiles and pairing, library and ROM cache, local emulation with libretro cores (melonDS DS), save sync, Sessions and multiview, Emulation and Controllers pages. Rules for agents: `CLAUDE.md`.

## Layout

`core/` (profiles, cache, settings, updater), `network/` (Hub connection, sync, CLI `framebeam_player_cli`), `emulation/` (libretro backend, manifests), `input/` (SDL3 gamepads), `media/` (H.264/Opus, WebRTC), `ui/` (QML), `app/` (executable `framebeam_player`), `launcher/` (Windows), `testutil/`. Details: [docs/reference/player-internals.md](../docs/reference/player-internals.md).

## Build, run, test

```sh
scripts/bootstrap-vcpkg.sh                 # once
make check-client                          # builds, fetches pinned deps, runs tests (CLIENT_PRESET, default linux-debug)
make fetch-core                            # melonDS DS core; without it, core tests are skipped
client/build/linux-debug/app/framebeam_player [--data-dir <path>] [--dev-allow-http]
```

Qt >= 6.4 comes from apt (Linux) or install-qt-action (Windows), not vcpkg. Prerequisites and dependency scripts: [docs/development.md](../docs/development.md).

## Documentation

- Using the Player (pairing, data directory, CLI, environment): [docs/guides/player.md](../docs/guides/player.md)
- Saves: [docs/guides/saves.md](../docs/guides/saves.md); Sessions: [docs/guides/sessions-over-the-internet.md](../docs/guides/sessions-over-the-internet.md); updates: [docs/guides/updates.md](../docs/guides/updates.md)
- Dependencies, features per area, firmware, controllers, updater internals: [docs/reference/player-internals.md](../docs/reference/player-internals.md)
- Design: [docs/design/README.md](../docs/design/README.md); decisions: [ADR 0003](../docs/adr/0003-player-phase2.md), [0004](../docs/adr/0004-player-portable-data.md), [0006](../docs/adr/0006-sessions-phase4.md), [0007](../docs/adr/0007-phase5.md), [0013](../docs/adr/0013-opengl-hardware-rendering.md), [0014](../docs/adr/0014-player-ui-pass.md)
