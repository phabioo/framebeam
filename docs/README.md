# Documentation

Start with the [root README](../README.md) for what FrameBeam is and a quick start.

## Guides (running FrameBeam)

- [Using the Player](guides/player.md): install, pairing, playing, data directory, CLI and environment
- [Hub installation](guides/hub-install.md): Debian package and systemd script on Linux / Raspberry Pi
- [Hub configuration](guides/hub-configuration.md): commands, all flags and environment variables, certificates
- [Sessions: LAN and internet](guides/sessions-over-the-internet.md): testing on a LAN, port forwards, TURN relay
- [Saves](guides/saves.md): sync, conflicts, history, restore, retention
- [Updates and releases](guides/updates.md): channels, automatic updates, promoting a beta to a release, rollback
- [Packaging](guides/packaging.md): Windows installer, Hub package
- [Walkthrough](guides/walkthrough.md): try the main features

## Reference

- [Features as built](features.md)
- [Protocol](reference/protocol.md) (endpoints, errors, WSS messages) and [update index](reference/update-index.md)
- [Hub internals](reference/hub-internals.md), [Player internals](reference/player-internals.md)

## Project

- [Development](development.md): build, test, dependency scripts, CI, versioning
- [Roadmap](roadmap.md): status and plan by version
- [Working with Claude Code](workflow.md): roles, briefs, phase plan
- Legal: [LICENSE](../LICENSE), [NOTICE.md](../NOTICE.md) (trademarks, disclaimer), [third-party notices](../THIRD-PARTY-NOTICES.md), [CONTRIBUTING.md](../CONTRIBUTING.md), [CLA](../CLA.md)
- [Architecture](architecture/README.md): design basis of the PoC (index; read only the file you need)
- [Design](design/README.md): screens, tokens, logo; [decisions](design/decisions.md)
- Decisions (ADRs, historical records):
  [0001 stack additions](adr/0001-stack-additions.md),
  [0002 protocol and Hub phase 1](adr/0002-protocol-and-hub-phase1.md),
  [0003 Player phase 2](adr/0003-player-phase2.md),
  [0004 portable data directory](adr/0004-player-portable-data.md),
  [0005 saves](adr/0005-saves-phase3.md),
  [0006 Sessions](adr/0006-sessions-phase4.md),
  [0007 phase 5](adr/0007-phase5.md),
  [0008 Windows CI cache](adr/0008-windows-ci-cache.md),
  [0009 finish the PoC](adr/0009-finish-poc.md),
  [0010 cores from the Hub](adr/0010-cores-from-the-hub.md),
  [0011 automatic updates](adr/0011-automatic-updates.md),
  [0012 internet sessions and save comfort](adr/0012-internet-sessions-and-save-comfort.md),
  [0013 OpenGL hardware rendering](adr/0013-opengl-hardware-rendering.md),
  [0014 Player UI pass](adr/0014-player-ui-pass.md),
  [0015 Hub UI pass](adr/0015-hub-ui-pass.md),
  [0016 release numbering and promotion](adr/0016-release-numbering-and-promotion.md),
  [0017 Player tech debt](adr/0017-player-tech-debt.md)

## Conventions

English only. Terms: "FrameBeam Hub", "FrameBeam Player", "Session" (not "Stream", except in diagnostics). Architecture files are not rewritten; deviations are recorded as ADRs. No ROMs, BIOS or firmware, no secrets, no private hostnames or addresses (use placeholders such as `hub.example.com`).
