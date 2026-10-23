# Documentation

Start with the [root README](../README.md) for what Uzume is and a quick start.

## Guides (running Uzume)

- [Using the app](guides/player.md): install, pairing, playing, data directory, CLI and environment
- [Server installation](guides/hub-install.md): Debian package and systemd script on Linux / Raspberry Pi
- [Server in Docker](guides/hub-docker.md): official image (amd64, arm64), volumes, compose, upgrade by pulling
- [Server backup and restore](guides/hub-backup.md): one archive, schedule, retention, `uzume-server backup` / `restore`, admin API, restore on a new machine
- [Server configuration](guides/hub-configuration.md): commands, all flags and environment variables, certificates
- [Sessions: LAN and internet](guides/sessions-over-the-internet.md): testing on a LAN, port forwards, TURN relay
- [coturn on a VPS](guides/coturn-on-a-vps.md): own external TURN relay for the server
- [Saves](guides/saves.md): sync, conflicts, history, restore, retention
- [Updates and releases](guides/updates.md): channels, automatic updates, promoting a beta to a release, rollback
- [Packaging](guides/packaging.md): Windows installer, server package
- [Walkthrough](guides/walkthrough.md): try the main features
- [Adding a system](guides/adding-a-system.md): server JSON, app manifest, core profile, input map, identity
- [Local ROMs](guides/local-roms.md): library folders, upload to the server, what others see
- [Plugin API](guides/plugin-api.md): Lua API for app plugins (0.11 runtime), developer mode
- [Publishing an official plugin](guides/plugin-publishing.md): sign a `.fbplugin`, add it to the plugin index

## Reference

- [Features as built](features.md)
- [Protocol](reference/protocol.md) (endpoints, errors, WSS messages) and [update index](reference/update-index.md)
- [Server internals](reference/hub-internals.md), [App internals](reference/player-internals.md)

## Project

- [Development](development.md): build, test, dependency scripts, CI, versioning
- [Roadmap](roadmap.md): status and plan by version
- [Working with Claude Code](workflow.md): roles, briefs, phase plan
- Legal: [LICENSE](../LICENSE), [NOTICE.md](../NOTICE.md) (trademarks, disclaimer), [third-party notices](../THIRD-PARTY-NOTICES.md), [CONTRIBUTING.md](../CONTRIBUTING.md), [CLA](../CLA.md)
- [Architecture](architecture/README.md): design basis of the PoC (index; read only the file you need)
- [Design](design/README.md): screens, tokens, logo; [decisions](design/decisions.md); [Uzume design](design/uzume/README.md) (valid from 0.18 on)
- Decisions (ADRs, historical records):
  [0001 stack additions](adr/0001-stack-additions.md),
  [0002 protocol and server phase 1](adr/0002-protocol-and-hub-phase1.md),
  [0003 app phase 2](adr/0003-player-phase2.md),
  [0004 portable data directory](adr/0004-player-portable-data.md),
  [0005 saves](adr/0005-saves-phase3.md),
  [0006 Sessions](adr/0006-sessions-phase4.md),
  [0007 phase 5](adr/0007-phase5.md),
  [0008 Windows CI cache](adr/0008-windows-ci-cache.md),
  [0009 finish the PoC](adr/0009-finish-poc.md),
  [0010 cores from the server](adr/0010-cores-from-the-hub.md),
  [0011 automatic updates](adr/0011-automatic-updates.md),
  [0012 internet sessions and save comfort](adr/0012-internet-sessions-and-save-comfort.md),
  [0013 OpenGL hardware rendering](adr/0013-opengl-hardware-rendering.md),
  [0014 app UI pass](adr/0014-player-ui-pass.md),
  [0015 server UI pass](adr/0015-hub-ui-pass.md),
  [0016 release numbering and promotion](adr/0016-release-numbering-and-promotion.md),
  [0017 app tech debt](adr/0017-player-tech-debt.md),
  [0018 app speed-up](adr/0018-player-speed-up.md),
  [0019 GPU-direct NVENC encoding](adr/0019-gpu-direct-nvenc.md),
  [0020 cores from the libretro buildbot](adr/0020-cores-from-the-libretro-buildbot.md),
  [0021 one Windows installer](adr/0021-one-windows-installer.md),
  [0022 Nintendo 3DS with Azahar](adr/0022-nintendo-3ds-with-azahar.md),
  [0023 plugins](adr/0023-plugins.md),
  [0024 system template and Game Boy](adr/0024-system-template-and-game-boy.md),
  [0025 local ROMs](adr/0025-local-roms.md),
  [0026 UI gaps and Multiview](adr/0026-ui-gaps-and-multiview.md),
  [0027 GPU paths](adr/0027-gpu-paths.md),
  [0028 release notes and changelogs](adr/0028-release-notes-and-changelogs.md),
  [0029 operations and safety](adr/0029-operations-and-safety.md),
  [0030 one app, one server, new name: Uzume](adr/0030-one-app-one-server-uzume.md),
  [0031 game variants and the randomizer](adr/0031-game-variants-and-randomizer.md)

## Conventions

English only. Terms: "Uzume server", "Uzume", "Session" (not "Stream", except in diagnostics). Architecture files are not rewritten; deviations are recorded as ADRs. No ROMs, BIOS or firmware, no secrets, no private hostnames or addresses (use placeholders such as `hub.example.com`).
