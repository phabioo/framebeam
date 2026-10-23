# Architecture – index

At a glance: the design basis of the PoC (draft 0.1, phases 0-5, implemented). Read only the file you need. Where the implementation refines or deviates, the ADRs in the table below apply; what the product does today is in [features.md](../features.md), how to use it in [guides/](../guides/). Planned work: [roadmap.md](../roadmap.md). UI screens: [design/README.md](../design/README.md) (valid from 0.18: [design/uzume/README.md](../design/uzume/README.md)).

- `01-overview.md` – goal, basic principle, responsibilities, tech stack – sections 1, 2
- `02-protocols-and-rom-cache.md` – protocols, data flows, handshake, game launch, ROM cache – section 3 (without save sync)
- `03-saves.md` – save sync, checkpoints, history, conflicts, base version – section 3 (save sync), 13
- `04-sessions-and-multiview.md` – Session sharing, WebRTC/P2P, visibility, multiview – sections 4, 5
- `05-emulation.md` – emulator extensibility, firmware, systems/cores, settings hierarchy – sections 6, 10, 11
- `06-controllers.md` – controllers and local input profiles – section 12
- `07-poc-scope.md` – PoC scope, exclusions, proof – section 7
- `08-repo-and-open-points.md` – repository structure, open implementation decisions – section 8
- `09-ui-and-navigation.md` – product terms, navigation, presentation – section 9
- `10-identity-pairing-tls.md` – roles, Hub profiles, pairing, TLS, switching Hubs – section 14
- `11-metadata-future.md` – central game metadata and artwork (future) – section 15
- `12-plugins.md` – plugin interface: Lua runtime, permissions, UI building blocks, distribution, runs

## Naming and web admin

These files were written for FrameBeam and keep the old product terms: since 0.18 the product is Uzume, the Player is the app and the Hub is the Uzume server ([ADR 0030](../adr/0030-one-app-one-server-uzume.md)). The Hub web interface that several files mention is removed: the server serves only the REST API, WSS signaling and file transfer, and administration is in the app (SERVER group). Read "Hub web", "Clients page" and similar that way. Current names of binaries, variables, files and images are in [guides/](../guides/) and [reference/](../reference/).

## File to ADRs

| File | Refined by (ADR) | Note |
|---|---|---|
| `01-overview.md` | 0001, 0003, 0006, 0007, 0009, 0012, 0013, 0019 | C++20, not C++23 (0001); audio via Qt Multimedia, SDL3 only for gamepads (0007). |
| `02-protocols-and-rom-cache.md` | 0002, 0005, 0006, 0007, 0010, 0012, 0022, 0025 | Endpoints and messages are specified in `protocol/`, not here. `core_missing` / `core_version_mismatch` only warn (0007). ROM cache limit: 0022 D5; several ROM files per game and local files: 0025. |
| `03-saves.md` | 0005, 0012, 0020, 0022, 0025 | Retention, restore, snapshots and slots: 0012 D7; core-format conversion: 0020 D7; 3DS saves as one tar: 0022 D3. |
| `04-sessions-and-multiview.md` | 0006, 0009, 0012, 0019, 0027 | Private = devices of the owner user; Invite only is fully implemented; multiview up to 4 surfaces with one audible surface (0012); GPU-direct encoding: 0019, 0027. |
| `05-emulation.md` | 0003, 0007, 0010, 0013, 0017, 0020, 0022, 0024 | The Hub ships no firmware hashes (0007 D4). Cores come from the libretro buildbot and update automatically (0020); the "no automatic update" text in section 11 is outdated. Systems are data (0024). |
| `06-controllers.md` | 0007, 0014, 0018 | As specified; built-in profiles are read-only; hotkeys tab (0014, 0018). |
| `07-poc-scope.md` | 0003-0007, 0010, 0011, 0012, 0021 | Several items listed as outside the PoC are delivered since: core distribution (0010), installers and updater (0011, 0021), TURN fallback (0012). |
| `08-repo-and-open-points.md` | 0002, 0005, 0006, 0007, 0009, 0010, 0011, 0012, 0022 | Most open points are decided in the listed ADRs; the tree's `client/` additionally has `input/` and `testutil/`. |
| `09-ui-and-navigation.md` | 0006, 0007, 0009, 0014, 0015, 0030 | Hub web UI is removed (0030); navigation is AppNav PLAY / SERVER / THIS PC. |
| `10-identity-pairing-tls.md` | 0002, 0003, 0007, 0009, 0030 | Credentials: Windows Credential Manager, Linux in memory only. Passwords, sign-in on a new device, user switching and "Confirm it's you": 0030 D4, D5. Invite expiry as built: 1 h / 24 h / 7 days. |
| `11-metadata-future.md` | none | Future, unchanged; planned after 1.0. |
| `12-plugins.md` | 0023, 0030 | Plugin runtime, server side and runs are built; no sidebar entries, plugin screens are run tabs, runs exist without a plugin (0030 D7). Design specs: [design/legacy/plugins.md](../design/legacy/plugins.md). |
