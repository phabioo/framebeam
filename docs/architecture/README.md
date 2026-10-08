# Architecture – index

Draft 0.1, the design basis of the PoC. The PoC (phases 0-5) is implemented; where the implementation refines or deviates, the ADRs apply (see below). Read only the file you need.

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

Design specification of the UI screens: `../design/README.md`.

## Status after the PoC

Planned work after the PoC: `../roadmap.md`.

| File | Refined by | Deviations and notes as built |
|---|---|---|
| `01-overview.md` | ADR 0001, 0003, 0006, 0007, 0009, 0013 | C++20 baseline, not C++23 (ADR 0001). Audio output via Qt Multimedia; SDL3 only for gamepads (ADR 0007). Software H.264 via libopenh264 (Windows) / libx264 (Linux); NVENC/QSV/AMF are probed at runtime (ADR 0006 D5) and, since 0.1.1, enabled in the Windows FFmpeg (`client/vcpkg.json`, ADR 0009 D7); opening them needs a GPU and is verified only locally. Hardware-rendered cores use an offscreen OpenGL context and a CPU readback; the frame path stays unchanged (ADR 0013). |
| `02-protocols-and-rom-cache.md` | ADR 0002, 0005, 0006, 0007 | Endpoints and messages are specified in `protocol/` (OpenAPI 1.5.0, `protocol_version` 1); info endpoint `/.well-known/framebeam`; compatibility rules `player_too_old`/`hub_too_old` (ADR 0002). `core_missing`/`core_version_mismatch` are warnings, launch allowed (ADR 0007). Still open: ROM cache limit and cleanup. |
| `03-saves.md` | ADR 0005 | Two counters: checkpoint "Rev N", history "vN"; 64 MiB per slot; "Keep both, decide later" starts with the local save and pauses uploads. Since 0.4 (ADR 0012 D7): retention/thinning, Restore from history, manual snapshot, slots in the Player, WSS push `save_updated`. |
| `04-sessions-and-multiview.md` | ADR 0006, 0009 | Private = devices of the owner user. Invite only is fully implemented (beyond the data-model minimum). Max 4 viewers. Since 0.4 (ADR 0012): optional embedded STUN/TURN relay on the Hub, connection type and AIMD bitrate adaptation, multiview up to 4 surfaces with one audible surface (audio focus). `-ice-servers` stays `stun:` only. Encoding runs on a worker thread with a bounded queue and RTT comes from the `fb-diag` DataChannel (ADR 0009 D3, D4). The multiview picker lists all Sessions (ADR 0009 D5). |
| `05-emulation.md` | ADR 0003, 0007, 0010, 0013 | Firmware: the Hub ships no expected hashes; it validates size and an optional admin-pinned SHA-256 (ADR 0007 D4). Core version mismatch only warns (accepted deviation, ADR 0007). Game Override level is shown but disabled. Core package cache and Player core cache exist since 0.2 (ADR 0010). Libretro hardware rendering (OpenGL) exists since 0.5 (ADR 0013). |
| `06-controllers.md` | ADR 0007 | As specified; first gamepad is P1; profiles local in `controllers.json`. |
| `07-poc-scope.md` | ADR 0003-0007 | All PoC rows delivered. Linux Player builds and runs for development, but keeps credentials in memory only (not a PoC target). |
| `08-repo-and-open-points.md` | ADR 0002, 0005, 0006, 0007, 0009, 0010 | Decided since: token format and lifetimes (ADR 0002), endpoints/messages (`protocol/`), ICE/STUN (ADR 0006 D4), media parameters (ADR 0006 D5), firmware manifest `client/emulation/manifests/nds.json` (ADR 0007). Resolved in 0.1.1: TLS renewal and confirmed pin change (ADR 0009). Core package format and index: ADR 0010 (0.2). Install layout and updater: ADR 0011 (0.3). Save retention and TURN decided in ADR 0012 (0.4). Still open: cache limits, rollback. The tree's `client/` additionally has `input/` and `testutil/`. |
| `09-ui-and-navigation.md` | ADR 0006, 0007, 0009, 0014, 0015 | Diagnostics is a tab in game and collapsible (ADR 0006 D6). Settings → Hubs (switch, remove with confirmation, auto-connect) exists since 0.1.1 (ADR 0009 D6) in addition to the connection screen; since 0.6 Hubs can also be edited (host, port) and diagnostics are split into Emulation and Streaming sections, opened by the tab or F3 (ADR 0014). Hub web UI since 0.7 (ADR 0015): fragment navigation, live updates over SSE, Settings sub-pages with network settings stored in the Hub database. |
| `10-identity-pairing-tls.md` | ADR 0002, 0003, 0007, 0009 | Pairing requests expire after 10 min; invite redemption `POST /api/v1/invites/redeem`, invite expiry 15 min / 1 h / 24 h (ADR 0007 D1). Credential store: Windows Credential Manager; Linux/macOS in memory only (open). A pairing-code alternative is not implemented. Since 0.1.1 the Hub renews its own self-signed certificate (30 days before expiry) and the Player offers a two-step confirmed pin change; a changed fingerprint still blocks until confirmed (ADR 0009 D1, D2). |
| `11-metadata-future.md` | none | Future, unchanged. |
