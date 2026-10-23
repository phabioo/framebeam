# Uzume

Uzume (formerly FrameBeam) is a self-hosted retro gaming platform. The **Uzume server** keeps your ROM library, versioned saves, users and devices in one place. The **Uzume app** emulates games locally and shares running Sessions directly with other apps. The server never emulates, encodes or renders. Since 0.18 you administer the server in the app (SERVER group and Settings › Server); the server has no web pages.

> The server manages. The app emulates. Audio and video flow directly between apps wherever possible.

## Status

Version 0.18 makes Uzume one app that also administers the server: every user has an account with a password, the server has commands for first setup and emergencies (`setup-code`, `reset-admin`, `status`, `backup`, `restore`), one installer offers three options (including "On this PC", where the app runs its own server), runs work without a plugin, the app has the Uzume design, and the web admin is removed ([ADR 0030](docs/adr/0030-one-app-one-server-uzume.md)). Before that, 0.17 added operations and safety: a Docker image for the server, a Status page with optional `/metrics`, server backup and restore, plugin UI language, a connection check and "Report a problem" in the app, and rollback to an earlier release for app and server ([ADR 0029](docs/adr/0029-operations-and-safety.md)); 0.16 gives every beta real release notes from the `## Changelog` section of each PR, summarizes all betas in the stable release and in `CHANGELOG.md`, and carries a short `notes` text in the signed update index for a later "What's new" screen ([ADR 0028](docs/adr/0028-release-notes-and-changelogs.md)); 0.15 adds zero-copy display of hardware-rendered games on Windows (no CPU readback for the game view) and GPU-direct encoding for AMD (AMF) and Intel (QSV) next to NVIDIA ([ADR 0027](docs/adr/0027-gpu-paths.md)); 0.14 closes the Round 3 UI gaps in the app and in the then server web (Esc always pauses, F11 toggles fullscreen, Multiview Row/Grid, viewers see a paused game, per-system and per-core core auto-update, run admin actions; [ADR 0026](docs/adr/0026-ui-gaps-and-multiview.md)); 0.13 makes systems data-driven ([ADR 0024](docs/adr/0024-system-template-and-game-boy.md), [adding a system](docs/guides/adding-a-system.md)) and adds Game Boy, Game Boy Color and Game Boy Advance (mGBA, Gambatte); 0.12 local ROMs (library folders, resumable upload, [ADR 0025](docs/adr/0025-local-roms.md)); earlier 0.10.x (connections and small items before new testers: public default STUN, ICE logging and connection type per viewer, optional Cloudflare TURN or any external TURN server such as coturn on a VPS (editable in the app), gamepad hotkeys, save slot rename/delete, "Collect logs" bundle, background core updates; Nintendo 3DS with Azahar as second system, [ADR 0022](docs/adr/0022-nintendo-3ds-with-azahar.md); one Windows installer since 0.9, [ADR 0021](docs/adr/0021-one-windows-installer.md); cores from the libretro buildbot since 0.8, [ADR 0020](docs/adr/0020-cores-from-the-libretro-buildbot.md)). The proof of concept (Nintendo DS with melonDS DS) is complete; server UI and app UI have had their design passes (0.6, 0.7). Next on the [roadmap](docs/roadmap.md) to 1.0 (a SoulLink run with friends in Pokémon Omega Ruby/Alpha Sapphire): app comfort and library (0.19), emulation (0.20), together (0.21), plugins (0.22), Linux app and Vulkan (0.23). Metadata and artwork come after 1.0; macOS is dropped. Windows app and Linux server (Debian, Raspberry Pi) are the supported platforms (the server also runs as a Windows service since 0.9); real GPUs, drivers and networks are verified only locally.

## Highlights

- **One library, many devices:** the server serves ROMs to apps with a hash-verified cache; pairing needs an admin's approval or an invite code.
- **Saves that never get lost:** versioned checkpoints, conflicts are never overwritten silently, history, restore and snapshots ([how saves work](docs/guides/saves.md)).
- **Share a running game:** Sessions with "Private" / "Hub users" / "Invite only" visibility, multiview with up to 4 surfaces, direct WebRTC (public STUN by default) with an optional relay on the server for apps outside your LAN.
- **Cores from the server:** installers ship no emulator cores; since 0.8 the server downloads cores from the libretro buildbot on the admin's request and keeps them updated ([ADR 0020](docs/adr/0020-cores-from-the-libretro-buildbot.md)) and serves them to apps, which choose the core per system and game.
- **OpenGL rendering, gamepads, firmware handling** for DS (firmware files are yours; Uzume never ships them).
- **Self-updating:** signed releases, `stable` and `beta` channels, Debian package for the server, Windows installer for the app.

All features: [docs/features.md](docs/features.md).

## Plugins

Plugin runtime (0.11): the plugin runtime in the app is implemented (sandboxed Lua, local folder install, developer mode only); UI slots, server plugins and runs (with the app's Runs screen) and signed distribution are implemented. Sandboxed Lua plugins add game-specific features such as SoulLink/Nuzlocke runs without changing the generic app. See [docs/guides/plugin-api.md](docs/guides/plugin-api.md), [docs/architecture/12-plugins.md](docs/architecture/12-plugins.md) and [ADR 0023](docs/adr/0023-plugins.md).

The official plugins live in their own repositories under github.com/phabioo: [`uzume-plugin-pokemon-core`](https://github.com/phabioo/uzume-plugin-pokemon-core), [`-soullink`](https://github.com/phabioo/uzume-plugin-soullink), [`-pokemon-editor`](https://github.com/phabioo/uzume-plugin-pokemon-editor), [`-cheats`](https://github.com/phabioo/uzume-plugin-cheats) and [`-template`](https://github.com/phabioo/uzume-plugin-template). Each carries its own docs and roadmap; no official plugin code exists yet.

## Quick start

**1. Install the server** (Debian / Raspberry Pi OS, amd64 or arm64). Download the `.deb` from the [releases](https://github.com/phabioo/uzume/releases):

```sh
sudo apt install ./uzume-server_<version>_arm64.deb
```

The server prints a setup banner with a setup code (also `uzume-server setup-code`); you enter it in the app to create the admin account. Other Linux systems, ports and options: [Server installation](docs/guides/hub-install.md), [Server configuration](docs/guides/hub-configuration.md).

**2. Install the app** (Windows): run the MSI from the releases; the installer asks how you will use Uzume (only on this PC, share with others, or server only). It ships no emulator core; the app downloads it from the server on first use. See [Using the app](docs/guides/player.md).

**3. Pair:** start the app, enter the server address (or open an invite link), compare the security code with the server's and confirm, then sign in with your user name and password; an admin creates users and invites under SERVER › Users.

Add homebrew ROMs in the library (admins: Rescan or Upload), pick one in the app and play. For friends outside your LAN see [Sessions over the internet](docs/guides/sessions-over-the-internet.md).

## Quick start for developers

```sh
scripts/bootstrap-vcpkg.sh   # once, app build
make check                   # server and app checks, quiet
```

Prerequisites, make targets, dependency scripts, CI and versioning: [docs/development.md](docs/development.md).

## Repository

- `server/`: Uzume server (Go, SQLite) ([README](server/README.md))
- `client/`: Uzume app (C++20, Qt 6) ([README](client/README.md))
- `protocol/`: OpenAPI and WSS schemas shared by both ([README](protocol/README.md))
- `packaging/`: server installer and `.deb`, Windows installer ([README](packaging/README.md))
- `docs/`, `scripts/`: documentation; check and dependency scripts

## Documentation

Index: [docs/README.md](docs/README.md). Most used:

- [Using the app](docs/guides/player.md), [Server installation](docs/guides/hub-install.md), [Server configuration](docs/guides/hub-configuration.md)
- [Updates and releases](docs/guides/updates.md), [Sessions](docs/guides/sessions-over-the-internet.md), [Saves](docs/guides/saves.md)
- [Roadmap](docs/roadmap.md), [Architecture](docs/architecture/README.md), [Decisions (ADRs)](docs/adr/), [Design](docs/design/README.md), [Working with Claude Code](docs/workflow.md)

## License

GPL-3.0-or-later. Copyright (C) 2026 Fabio and Uzume contributors. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md) (trademarks, disclaimer) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md) and [CLA.md](CLA.md).

## Legal

Not affiliated with Nintendo or any other console maker. No games, ROMs, BIOS or firmware are included or may be committed; use homebrew ROMs and only content you are legally entitled to.
