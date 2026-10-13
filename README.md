# FrameBeam

FrameBeam is a self-hosted retro gaming platform. The **FrameBeam Hub** keeps your ROM library, versioned saves, users and devices in one place. The **FrameBeam Player** emulates games locally and shares running Sessions directly with other Players. The Hub never emulates, encodes or renders.

> The Hub manages. The Player emulates. Audio and video flow directly between Players wherever possible.

## Status

Version 0.15 adds zero-copy display of hardware-rendered games on Windows (no CPU readback for the game view) and GPU-direct encoding for AMD (AMF) and Intel (QSV) next to NVIDIA ([ADR 0027](docs/adr/0027-gpu-paths.md)); 0.14 closes the Round 3 UI gaps in the Hub web and the Player (Esc always pauses, F11 toggles fullscreen, Multiview Row/Grid, viewers see a paused game, per-system and per-core core auto-update, run admin actions; [ADR 0026](docs/adr/0026-ui-gaps-and-multiview.md)); 0.13 makes systems data-driven ([ADR 0024](docs/adr/0024-system-template-and-game-boy.md), [adding a system](docs/guides/adding-a-system.md)) and adds Game Boy, Game Boy Color and Game Boy Advance (mGBA, Gambatte); 0.12 local ROMs (library folders, resumable upload, [ADR 0025](docs/adr/0025-local-roms.md)); earlier 0.10.x (connections and small items before new testers: public default STUN, ICE logging and connection type per viewer, optional Cloudflare TURN or any external TURN server such as coturn on a VPS (editable in the Hub web interface), gamepad hotkeys, save slot rename/delete, "Collect logs" bundle, background core updates; Nintendo 3DS with Azahar as second system, [ADR 0022](docs/adr/0022-nintendo-3ds-with-azahar.md); one Windows installer since 0.9, [ADR 0021](docs/adr/0021-one-windows-installer.md); cores from the libretro buildbot since 0.8, [ADR 0020](docs/adr/0020-cores-from-the-libretro-buildbot.md)). The proof of concept (Nintendo DS with melonDS DS) is complete; Hub UI and Player UI have had their design passes (0.6, 0.7). Next on the [roadmap](docs/roadmap.md) to 1.0 (a SoulLink run with friends in Pokémon Omega Ruby/Alpha Sapphire): plugin runtime (0.11, Player part done), local ROMs (0.12, done in code), system template with GB/GBC/GBA (0.13, done in code), UI gaps and Multiview (0.14, done in code), GPU paths (0.15, done in code). Metadata and a Linux Player come after 1.0; macOS is dropped. Windows Player and Linux Hub (Debian, Raspberry Pi) are the supported platforms (the Hub also runs as a Windows service since 0.9); real GPUs, drivers and networks are verified only locally.

## Highlights

- **One library, many devices:** the Hub serves ROMs to Players with a hash-verified cache; pairing needs an admin's approval or an invite code.
- **Saves that never get lost:** versioned checkpoints, conflicts are never overwritten silently, history, restore and snapshots ([how saves work](docs/guides/saves.md)).
- **Share a running game:** Sessions with Private / Hub users / Invite only visibility, multiview with up to 4 surfaces, direct WebRTC (public STUN by default) with an optional relay on the Hub for Players outside your LAN.
- **Cores from the Hub:** installers ship no emulator cores; since 0.8 the Hub downloads cores from the libretro buildbot on the admin's request and keeps them updated ([ADR 0020](docs/adr/0020-cores-from-the-libretro-buildbot.md)) and serves them to Players, which choose the core per system and game.
- **OpenGL rendering, gamepads, firmware handling** for DS (firmware files are yours; FrameBeam never ships them).
- **Self-updating:** signed releases, `stable` and `beta` channels, Debian package for the Hub, Windows installer for the Player.

All features: [docs/features.md](docs/features.md).

## Plugins

Plugin runtime (0.11): the plugin runtime in the Player is implemented (sandboxed Lua, local folder install, developer mode only); UI slots, Hub plugins and runs (with the Player's Runs screen) and signed distribution are implemented. Sandboxed Lua plugins add game-specific features such as SoulLink/Nuzlocke runs without changing the generic Player. See [docs/guides/plugin-api.md](docs/guides/plugin-api.md), [docs/architecture/12-plugins.md](docs/architecture/12-plugins.md) and [ADR 0023](docs/adr/0023-plugins.md).

The official plugins live in their own repositories under github.com/phabioo: [`framebeam-plugin-pokemon-core`](https://github.com/phabioo/framebeam-plugin-pokemon-core), [`-soullink`](https://github.com/phabioo/framebeam-plugin-soullink), [`-pokemon-editor`](https://github.com/phabioo/framebeam-plugin-pokemon-editor), [`-cheats`](https://github.com/phabioo/framebeam-plugin-cheats) and [`-template`](https://github.com/phabioo/framebeam-plugin-template). Each carries its own docs and roadmap; no official plugin code exists yet.

## Quick start

**1. Install the Hub** (Debian / Raspberry Pi OS, amd64 or arm64). Download the `.deb` from the [releases](https://github.com/phabioo/framebeam/releases):

```sh
sudo apt install ./framebeam-hub_<version>_arm64.deb
```

Create the admin account and open `https://<hub-address>:8443/`. Other Linux systems, ports and options: [Hub installation](docs/guides/hub-install.md), [Hub configuration](docs/guides/hub-configuration.md).

**2. Install the Player** (Windows): run the MSI from the releases; "Set up a Hub on this PC" in the Player installs a local Hub. It ships no emulator core; the Player downloads it from the Hub on first use. See [Using the Player](docs/guides/player.md).

**3. Pair:** start the Player, enter the Hub address, compare the certificate fingerprint with the Hub's and confirm, then allow the new device on the Hub's Clients page (or redeem an invite code from the Users page).

Add homebrew ROMs in the Hub's Library, pick one in the Player and play. For friends outside your LAN see [Sessions over the internet](docs/guides/sessions-over-the-internet.md).

## Quick start for developers

```sh
scripts/bootstrap-vcpkg.sh   # once, Player build
make check                   # Hub and Player checks, quiet
```

Prerequisites, make targets, dependency scripts, CI and versioning: [docs/development.md](docs/development.md).

## Repository

- `server/`: FrameBeam Hub (Go, SQLite) ([README](server/README.md))
- `client/`: FrameBeam Player (C++20, Qt 6) ([README](client/README.md))
- `protocol/`: OpenAPI and WSS schemas shared by both ([README](protocol/README.md))
- `packaging/`: Hub installer and `.deb`, Windows installer ([README](packaging/README.md))
- `docs/`, `scripts/`: documentation; check and dependency scripts

## Documentation

Index: [docs/README.md](docs/README.md). Most used:

- [Using the Player](docs/guides/player.md), [Hub installation](docs/guides/hub-install.md), [Hub configuration](docs/guides/hub-configuration.md)
- [Updates and releases](docs/guides/updates.md), [Sessions](docs/guides/sessions-over-the-internet.md), [Saves](docs/guides/saves.md)
- [Roadmap](docs/roadmap.md), [Architecture](docs/architecture/README.md), [Decisions (ADRs)](docs/adr/), [Design](docs/design/README.md), [Working with Claude Code](docs/workflow.md)

## License

GPL-3.0-or-later. Copyright (C) 2026 Fabio and FrameBeam contributors. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md) (trademarks, disclaimer) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md) and [CLA.md](CLA.md).

## Legal

Not affiliated with Nintendo or any other console maker. No games, ROMs, BIOS or firmware are included or may be committed; use homebrew ROMs and only content you are legally entitled to.
