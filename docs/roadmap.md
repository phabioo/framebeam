# Roadmap after the PoC

The PoC (phases 0-5) is complete. From here on, work is planned as versions 0.1.1 to 0.11, each cut into one work package per thread/PR (see `docs/workflow.md`). The order was decided by Fabio on 2026-10-06; on 2026-10-07 he moved Sessions over the internet ahead of the UI passes (renumbered 0.4 to 0.6). On 2026-10-07 Fabio also moved OpenGL hardware rendering for the Player (all emulators/cores, not only 3DS) ahead of the UI passes as the new 0.5; the UI passes, 3DS and everything after shifted by one. ADRs written before 2026-10-07 use the old numbering: old 0.5/0.6/0.7/0.8/0.9/0.10 = new 0.6/0.7/0.8/0.9/0.10/0.11. Items marked open are not decided yet; decisions are recorded as ADRs in `docs/adr/`.

## PoC phases (history)

Phase plan: [workflow.md](workflow.md#phase-plan).

| Phase | Status | Scope |
|---|---|---|
| 0 Foundation | done | Monorepo, CLAUDE.md, architecture, CI (Linux/Windows), build scaffolding, check scripts |
| 1 Protocol and Hub basics | done | OpenAPI `/api/v1`, WSS schemas, Hub with SQLite, admin setup, TLS, pairing, tokens, library, ROM download, web interface |
| 2 Playable vertical slice | done | Player core (profile, pairing, library, ROM cache), melonDS DS via Libretro, minimal Qt UI |
| 3 Saves | done | Save storage, sync, versions, conflict model ([ADR 0005](adr/0005-saves-phase3.md)) |
| 4 Session sharing and multiview | done (tested locally on two Windows PCs) | Presence, signaling, WebRTC, multiview ([ADR 0006](adr/0006-sessions-phase4.md), accepted) |
| 5 Remainder and polish | done (tested locally) | Users and invites, user uploads, systems and firmware, Emulation and Controllers pages, appearance, Windows installer ([ADR 0007](adr/0007-phase5.md), accepted) |

## Overview

| Version | Status | Theme | Goal |
|---|---|---|---|
| 0.1.1 | done | Finish the PoC | Close the PoC leftovers and clean up the codebase. |
| 0.2 | done | Cores from the Hub | Installers no longer ship emulator cores; the Hub distributes signed core packages. |
| 0.3 | done | Automatic updates | A change on `main` reaches the test devices without manual work. |
| 0.4 | done (relay and multiview verified across networks only locally) | Sessions over the internet and save comfort | Sessions work beyond the LAN; saves get retention, restore and slots. |
| 0.5 | done in code (GPUs and drivers verified only locally) | OpenGL hardware rendering | The Player offers an OpenGL context to libretro cores, so hardware-rendered cores run; frames are read back to the CPU, zero-copy deferred ([ADR 0013](adr/0013-opengl-hardware-rendering.md)). |
| 0.6 | done in code (look and feel, GPU values, real sessions verified only locally) | Player UI pass | FrameBeam Player is clearer, more responsive and consistent with the design tokens ([ADR 0014](adr/0014-player-ui-pass.md)). |
| 0.7 | done in code | Hub UI pass | FrameBeam Hub web UI swaps fragments instead of full pages, updates live and has network settings in the browser ([ADR 0015](adr/0015-hub-ui-pass.md)). |
| 0.7.1 | done | Revision pass | Bug fixes and cleanup in Hub, Player and CI (flaky Session visibility race, query error handling, job timeouts); docs and READMEs consolidated. |
| 0.7.x | done | Player tech debt | Session visibility lives in `settings/player.json`, a core with another major version blocks the launch, `PlayerController` split into helpers ([ADR 0017](adr/0017-player-tech-debt.md)). |
| 0.7.x | done in code | Speed-up | The Player can run a game faster than real time ("Speed-up", Space, 1.5x to 8x) when the libretro core allows it ([ADR 0018](adr/0018-player-speed-up.md)). |
| 0.8 | planned | Second system: 3DS (Azahar) | The Azahar libretro core ships as a plain core package and runs on the OpenGL rendering from 0.5. |
| 0.9 | planned | Metadata and artwork | Central game metadata and boxart in Hub and Player. |
| 0.10 | planned | Hub for Windows / Windows Server | FrameBeam Hub runs as a Windows service with an installer. |
| 0.11 | planned | Linux and macOS Player | FrameBeam Player on Linux and macOS. |

## 0.1.1 Finish the PoC

PoC leftovers. Decisions: [ADR 0009](adr/0009-finish-poc.md) (accepted).

- [x] Done: Hardware encoders NVENC/QSV/AMF: the Windows FFmpeg in `client/vcpkg.json` enables `nvcodec`, `qsv` and `amf`; a Windows-only test checks that `h264_nvenc`, `h264_qsv` and `h264_amf` are compiled in. ADR 0006 is corrected. Opening the encoders needs a GPU and is verified only locally by Fabio.
- [x] Done: Multiview picker lists all Sessions in a scrollable list; the "+N more in the Library" pointer is gone.
- [x] Done: Settings → Hubs (switch, remove with confirmation, auto-connect, current Hub marked); switching ends running work and secures saves like the connection screen.
- [x] Done: Certificate renewal and confirmed pin change. The Hub renews its self-generated certificate at startup when expired or expiring within 30 days (`framebeam-hub renew-cert`, Settings badge); the Player shows both fingerprints and re-pins after a two-step confirmation, keeping the credential (`--accept-fingerprint` in the CLI). Own certificate and key are never modified. Verified only locally by Fabio against a real Hub certificate.
- [x] Done: Session encoding and RTP send run on a worker thread (bounded queue, oldest video frame dropped); RTT is reported in diagnostics via the negotiated DataChannel `fb-diag`.
- [x] Done (0.7.x, ADR 0017 D1): A core whose major version differs from the Hub's expected version blocks the launch; other differences only warn.
- [x] Done: Codebase cleanup: system display name and controller labels from the system manifest, phase-named files and tests renamed (migration `0004_phase5.sql` kept), MSVC C4804 fixed, E2E scripts run in the Linux CI job, staticcheck in `make check-hub`, narrower libdatachannel CI cache path. The `PlayerController` split moves to the Player UI pass (0.6).

Completed 2026-10-06: Windows CI runs on `main` pushes to prime its vcpkg binary cache after merges. The release preset uses a release-only dependency triplet. Cache keys distinguish the triplet and MSVC version and cover the manifest, presets and overlay triplets. The first main run with the new triplet is expected to build cold; later runtimes depend on cache hits and runner performance. See [ADR 0008](adr/0008-windows-ci-cache.md).

## 0.2 Cores from the Hub

Decisions: [ADR 0010](adr/0010-cores-from-the-hub.md) (accepted).

Goal: installers ship no emulator cores. melonDS DS leaves the Windows installer and comes from the Hub.

Decision: the Hub obtains signed core packages from a fixed trusted source and caches them. There is no admin upload.

- [x] Done: Hub: Core Package Cache (storage per core ID, version, platform/architecture, SHA-256, origin, license text). The Hub downloads signed packages from the trusted source, verifies the signature and caches them. On "Systems & Cores" (replaces the "LATER" placeholder) the admin only selects the version. Download endpoint for Players with ETag.
- [x] Done: Protocol: endpoints for package metadata and download, a handshake feature (for example `cores_v1`); `protocol_version` stays 1.
- [x] Done: Player: core cache `cache/cores/<core-id>/<version>/<platform>/`, download on demand with hash, version and platform checks, `CoreLocator` searches there. Library and Emulation page show "core loading / missing / incompatible".
- [x] Done: Trust: Ed25519-signed index (`framebeam-sign`, `.github/workflows/cores.yml`), checked by the Hub against compiled-in and `--core-trust-key` keys; the Player checks size and SHA-256 (ADR 0010). The key is reused by the updater in 0.3.
- [x] Done: FrameBeam release key generated by Fabio (GitHub secret `FRAMEBEAM_SIGNING_KEY`, public key in `corepkg.DefaultTrustedKeys`).
- [x] Done: Package format and index defined (ADR 0010, `docs/reference/protocol.md`). Source: FrameBeam's own GitHub Releases; the Hub needs internet access to github.com or uses `framebeam-hub import-cores <dir>` offline.
- [x] Done: Windows installer and package no longer contain cores.
- Template: the firmware path from phase 5.

## 0.3 Automatic updates

Goal: a change on `main` lands on the test devices (Windows Player, Hub on the Pi) without manual work.

- [x] Done: Release pipeline: `VERSION` file, version and channel computed by CI; `.github/workflows/release.yml` publishes a stable release per `vX.Y.Z` tag and a beta prerelease per `main` build (newest 5 kept) with Hub and Player artifacts (ADR 0011 D1, D3).
- [x] Done: Windows installer completed: launcher plus `bin\` layout instead of a DLL subfolder, upgrade over an existing flat installation, data is preserved (ADR 0011 D7).
- [x] Done: Hub as a Linux package (.deb amd64/arm64) with systemd units; migration from `install-hub.sh` installs; the script stays for non-Debian systems (ADR 0011 D5).
- [x] Done: Integrated updater for Hub and Player: Ed25519-signed update index (`updates-index`, same key as 0.2), channel setting, automatic install on beta and confirmation on stable, compatibility via `protocol_version` (ADR 0011 D2, D4, D5, D7). The Hub applies updates through a root helper started by a systemd path unit.
- [x] Done: Hub: database backup before schema migration, service restart by the package after the update (ADR 0011 D6).
- [x] Done: Windows Player and Linux Hub only; other platforms follow later.
- Rollback: proposed in ADR 0011 (D8), decided with the merge. No automatic or one-click rollback in 0.3; manual downgrade plus DB backup restore.
- Moved: the Player-side signature check of core packages (ADR 0010 D5) is not part of 0.3; it is in 0.4 (done there, see below).

## 0.4 Sessions over the internet and save comfort

Moved ahead of the UI passes by Fabio on 2026-10-07: testing with friends outside the LAN comes first.

- [x] Done: Supported setups decided and documented: LAN, internet with port forward (recommended), VPN; no external TURN server in 0.4 (ADR 0012 D1; `docs/guides/sessions-over-the-internet.md`).
- [x] Done: Hub: embedded STUN/TURN relay (pion/turn, off by default) with short-lived credentials in `hello_ack` and the join response; Settings shows TURN status and the router port forwards (ADR 0012 D2-D4).
- [x] Done: Player: TURN via libdatachannel, connection type (direct / relay) in diagnostics, `FRAMEBEAM_FORCE_RELAY` / `--force-relay`, AIMD bitrate adaptation to the worst viewer (ADR 0012 D5).
- [x] Done: Multiview with up to 4 surfaces (several remote Sessions) and the audio focus rule (ADR 0012 D8).
- [x] Done: Player-side signature check of core packages (moved from 0.3, ADR 0010 D5): the Player verifies the signed core index served by the Hub (ADR 0012 D6).
- [x] Done: Saves: retention/thinning, restore from history, manual snapshot, WebSocket push `save_updated`, multiple slots (ADR 0012 D7; `docs/architecture/03-saves.md`).
- Open: verification across real networks (two Players behind different routers, forced relay, more than one remote Session) is done locally by Fabio.

## 0.5 OpenGL hardware rendering

Decided by Fabio on 2026-10-07: OpenGL hardware rendering for the Player, for all emulators/cores and not only 3DS, comes before the UI passes.

- [x] Done: Libretro hardware rendering (ADR 0013 D1-D4; largest work item): the Player's libretro backend handles only software framebuffers today (no `RETRO_ENVIRONMENT_SET_HW_RENDER`). Needed: an OpenGL 3.3 core context (shared with the Qt Quick scene or offscreen), FBO handed to the core via `get_current_framebuffer`, `context_reset`/`context_destroy` handling, presenting the FBO without a CPU copy, and a GPU readback (or zero-copy path to the encoder) when the Session is shared. Delivered as an offscreen context per game, Player-owned FBO and a readback of every frame into the existing frame path. Deviation: no zero-copy presentation yet (ADR 0013 D3); revisit if profiling shows the readback costs frame time. `FRAMEBEAM_DISABLE_HW_RENDER=1` switches it off.
- [x] Done: CI (ADR 0013 D6): Mesa llvmpipe under xvfb on Linux, GL tests skipped without a GL 3.3 context. Original note: headless CI cannot run hardware-rendered cores end to end; core tests need a GL context (for example Mesa llvmpipe) or stay local.
- [x] Done: melonDS DS OpenGL renderer and internal resolution (ADR 0013 D5). Original note: melonDS DS then gets its OpenGL renderer and the internal resolution option; both are hidden today because the Player offers no GL context (see the Emulation page in `docs/design/player.md`).
- Open: verification on real GPUs and drivers and frame time (4x internal resolution) is done locally by Fabio.
- Later Nintendo systems rely on it: the 3DS (0.8) and, among the later systems, N64 and GameCube/Wii.

## 0.6 Player UI pass

Done in code (decisions: [ADR 0014](adr/0014-player-ui-pass.md), accepted); looks, GPU values and real sessions are verified only locally by Fabio.

- [x] Done: Clarity: rework information density and grouping per screen (Library, Detail, game view, Emulation, Controllers, Settings).
- [x] Done: Spacing, sizes and alignment consistently from the design tokens (`docs/design/tokens.md`) instead of single values.
- [x] Done: Responsiveness: nothing blocking on the UI thread (network, hashing, encoding), loading and progress states instead of freezing, immediate click feedback.
- [x] Done: Dynamics: transitions and animations (page change, hover, lists); live updates of Library, Sessions and sync status without manual reload.
- [x] Done: One pass with screenshots per screen: the screenshot tests (`FRAMEBEAM_SCREENSHOT_DIR`, `ctest -R screenshots`) render all screens at 1440x900 and produce the "after" set, which the PR carries. There is no "before" set.
- [x] Done (D15): Core state refresh: after a core download the "core missing" notice on NDS games stays until the Player restarts (found by Fabio on 2026-10-07); Library and Detail must re-evaluate the core state live.
- [x] Done (D3, D9, D12, D13): From 0.4 (requested by Fabio on 2026-10-07): Settings → Hubs lets the user edit a Hub's address and port (for example after the Hub port changed or when switching between LAN address and public name), keeping the pinned fingerprint and credential; connection type (direct / relay) and target bitrate are shown in the Streaming section of the diagnostics (next item); multiview layouts and the save history, restore, snapshot and slot picker get their final design.
- [x] Done (D4-D8, D10, D11): Diagnostics split (requested by Fabio on 2026-10-07; design 3t-3y in `docs/design/player.md`, accepted by Fabio on 2026-10-08): one overlay with two separately collapsible sections, Emulation and Streaming, opened by the Diagnostics tab, the side-panel toggle or F3, open/closed remembered per section; the same in window, multiview (per tile) and fullscreen (Emulation only); Streaming shows "No active session" without a session. New measurements needed (today missing in the Player): actual emulation fps and frame time with the emu/readback split, audio buffer and underruns, GPU/driver string and the reason for an OpenGL fallback as UI values, decoder name, and remote viewer values on the host (loss and bitrate arrive already in the viewer's receiver report and only need to be kept; decoded fps is new); the host line shows the target bitrate next to the measured one. Open points m-t are decided in ADR 0014. Built: split overlay (Emulation / Streaming) with F3 and persisted section states, new measurements, viewer rx report with optional `fps`/`dec` fields kept on the host, target bitrate on the host line; fullscreen with F11/Esc; Library filter chips, Settings jump list, Hub switch/edit/remove with address and port validation, layout switch from the manifest, Multiview Grid 2×2.

Open in 0.6:

- The Player does not know the Hub user's display name; the sidebar shows the device name.
- Per-game settings are still "coming later".
- [x] Done (0.7.x): Hotkeys tab (3f) and "Duplicate to edit" for built-in controller profiles, see "0.7.x Hotkeys and controller profiles".
- [x] Done (0.7.x, ADR 0017 D3): `PlayerController` split into `CoreCatalog`, `GameDetail`, `HubPresenter` and `GameStarter`.
- Verification of look and feel, GPU values and real sessions is done locally by Fabio.

## 0.7 Hub UI pass

Implemented, see [ADR 0015](adr/0015-hub-ui-pass.md) (accepted).

- [x] Done: pages restyled to the v4 design (Library with saves column and conflict marker, Saves with slot tabs, history timeline, snapshot filter, inline restore confirmation and retention box from the real rules, Systems list and detail tabs, Clients, Users); nav badge "firmware" is now "{n} issues".
- [x] Done: no full page loads on navigation; sidebar swaps only the content area (ADR 0015 D1).
- [x] Done: live updates without reload via an in-process event bus and Server-Sent Events; badges and affected tables refresh themselves, Clients polling removed (D2).
- [x] Done: Settings sub-pages Updates, General, Network, Security with per-field autosave (D3).
- [x] Done: network settings form (Hub port, embedded TURN, public host, TURN port, relay range and IP, STUN servers, save retention). Decided by Fabio on 2026-10-07: web settings win; `hub.env` and flags only give the initial value (D4). Changes that need it are applied by a restart the Hub triggers itself, without root (D5). Ports below 1024 are refused on the web, use `install-hub.sh --port` (D6). Startup fallbacks keep the Hub reachable after a bad setting (D7). The TURN status and port forward list moved into Network; reachability is derived from the configuration (D8).
- Open (left out of 0.7, remaining): thinned-out history marker, metadata actions, Library "of total" disk capacity, external TURN servers with login (only a STUN list), reachability probe from outside, core packages not filtered per system, transport options "Custom cert/key" and "Reverse proxy".
- [x] Done (0.7.x follow-up): Hub Saves page "+ New slot" (new slot starts from the current version of the selected slot; web only, no API) and deleting a snapshot (inline confirmation; `DELETE /api/v1/games/{game_id}/saves/{slot}/history/{version}`, handshake feature `saves_v3`, OpenAPI 1.6.0, `protocol_version` unchanged). Renaming/deleting a slot stay open.
- [x] Done (0.7.x follow-up): Library "Rescan folder" (non-recursive import from `-library-import-dir`), invite "Copy link" (only in the creating response; public `/invite` page), "Player too old" marker in the Clients table, certificate "Renew now" (swap without restart; tlsutil writes key/cert atomically).

## 0.7.x Speed-up (fast-forward)

See [ADR 0018](adr/0018-player-speed-up.md) (proposed).

- [x] Done: "Speed-up" (libretro fast-forward) in the Player for cores that do not inhibit it; Space toggles; speed 1.5×, 2×, 3×, 4×, 6×, 8× (default 2×, the user's choice wins over a core ratio); screen frames limited to the base rate.
- [x] Done: header button "Speed-up" ("»" when narrow) and a speed select in the Session panel (running game only) and indicator "Speed-up ×N"; hidden when the core inhibits it; stays usable while the Session is shared (viewers get the normal stream frame rate).
- [x] Done: Emulation settings "Speed-up speed", "Speed-up on start" (default off) and "Audio during speed-up" (default on, resampled; off drops audio) with the global > system > game hierarchy.
- [x] Done (0.7.x): the speed-up key is configurable in Controllers → Hotkeys.
- Open: gamepad hotkey.
- [x] Done (0.7.x follow-up): hardware frames are scaled down on the GPU to the size the view (and, while shared, the stream) needs before the readback; diagnostics show "read back W×H", an honest readback mean and reads per second. melonDS DS ignores `GET_AUDIO_VIDEO_ENABLE`, so it still renders every frame during speed-up.

## 0.7.x Direct GPU display (zero-copy)

Follow-up to ADR 0013 D3 (requested 2026-10-08 after speed-up stutter at 8× internal resolution).

- Open: show hardware-rendered frames in the game view straight from the GPU (shared texture or interop with the Qt Quick scene graph, D3D11 on Windows), without the CPU readback and QImage upload; readback only while the Session is shared.

## 0.7.x Hotkeys and controller profiles

- [x] Done: Hotkeys tab in Controllers (3f): Player keyboard hotkeys for fullscreen (F11), diagnostics (F3), snapshot (F5) and speed-up (Space) are configurable, can be cleared and reset; Esc stays fixed (leave fullscreen, else pause). Stored locally in `settings/controllers.json` (`hotkeys`, only non-default entries); a key used by another hotkey is refused; a hotkey wins over the keyboard profile and is never sent to the core. Key hints in the game screen, diagnostics and Session panel follow the configured keys.
- [x] Done: built-in controller profiles stay read-only; "Duplicate to edit" (and clicking a mapping of a built-in profile) creates a copy, assigns it to the device and starts editing it.
- Open: gamepad hotkeys.

## 0.7.x Background game

Decided by the project owner on 2026-10-08.

- [x] Done (interim UI): "← Library" in the game view pauses the game and keeps it loaded (a shared Session stays shared); the Library shows "Now running" with Resume and Quit game, the tile and the detail pane mark the game, input does not reach the core, and starting another game asks "Quit {running} and start {new}?" first. Quitting is an explicit action (header "Quit", Session panel, Library).

## 0.7.x UI revision after the playtest

Design handoff of 2026-10-08 after a friends playtest, imported in [docs/design/README.md](design/README.md) (screens 3c-2 to 3c-5, 3e-2, 3p-2, 3f-2/3, 3g-2/3, 3r-2, 3h-2, 3i-2, 3t-2, 3x-2, 3s-2/3). Open points and the defaults decided: [design/decisions.md](design/decisions.md) (u-al). Per screen specs: [design/player.md](design/player.md), [design/hub.md](design/hub.md).

- [x] Settings rows: one SettingsRow component for Emulation and Settings (3e-2, 3p-2): grid 14 | flex | 280 | 72, changed dot, "More" description with option list, segment/select rule, disabled reason, hover and focus, core-agnostic rendering of the core's options and categories.
- [ ] Library sort and Ready first, compact SAVE summary, saves view and upload confirmation (3c-2, 3c-3, 3c-4): toolbar with count, Sort menu and "Ready first" (saved per device), NOT READY group divider, empty-result state, "Upload ROM" in the header, saves mode in the 392 column (slot tabs or switcher, CURRENT bar, snapshot, new slot, restore and delete confirmations, file details, offline/running/conflict states, upload with local backup).
- [x] Controllers glyphs (3f-2, 3f-3): PadGlyph chips and drawn D-pad icons, "Button labels" select (Auto, Xbox, PlayStation, Generic; saved per device), mapping table `14 | flex | 240 | 72`, controller-shaped input test grid.
- [x] Hub saves upload (3s-2, 3s-3): "Upload save" panel in the CURRENT bar, standalone form removed, Library row link opens the panel, timeline reasons "Uploaded" and "Before upload", upload counts in retention, states (identical file, blocked while a session runs), undo link.
- [ ] In-game header, panel and diagnostics (3g-2, 3g-3, 3r-2, 3h-2, 3i-2, 3t-2, 3x-2, 3c-5): GameHeader in five zones with Reset popover and compact 1280 mode, GamePanel (Sharing, Save, Quit game pinned; collapsible rail; remote-tile panel), multiview tile selection vs audio focus and "+ Add" picker, one diagnostics toggle (header + F3) with the overlay at the top right, "Now running" strip, paused background game with Resume/Quit and the quit-and-start dialog.

## 0.8 Second system: Nintendo 3DS with Azahar

Decided by Fabio on 2026-10-07: the second system is the Nintendo 3DS with the Azahar libretro core (replaces the mGBA proposal).

- Core: `azahar_libretro` (GPLv2+), available prebuilt on the libretro buildbot (checked 2026-10-07: `nightly/windows/x86_64/latest/azahar_libretro.dll.zip`) and described by `azahar_libretro.info` in libretro-core-info. GPLv2+ allows mirroring only with the complete corresponding source: the workflow archives the exact source revision (including build scripts and submodules) as a release asset next to every mirrored binary and keeps it as long as that binary is published (GPLv2 §3(a)); the package carries the license text and names that archive. The same applies to every GPL core in the core list.
- Hardware rendering: the core info sets `hw_render = true`, `required_hw_api = OpenGL Core >= 3.3`. The Player's OpenGL hardware rendering is a prerequisite and arrives in 0.5; this version only uses it.
- Display: two screens of different sizes (top 400 x 240, bottom 320 x 240 touch); the system manifest describes layout and touch mapping, and the Session encoder sends the composed frame.
- Games: decrypted dumps only (`.3ds`/`.cci`/`.cxi`/`.3dsx`, plus the compressed `z*` variants); FrameBeam never decrypts. Some games need system files such as Mii data dumped from the user's own console; these follow the firmware path from phase 5 and are never shipped.
- Performance: 3DS emulation needs a considerably faster CPU/GPU than DS; check on Fabio's devices whether play plus encoding fits.
- It arrives only as another package through the core distribution from 0.2, without installer changes.
- ~~Re-evaluate the core version check (warning vs. block, ADR 0007).~~ Decided in ADR 0017 D1.
- Game override UI on the Emulation page.
- ROM cache limit and cleanup.
- Core sourcing (decided 2026-10-07, builds on ADR 0010):
  - Core list: one file in the repo lists all cores (core id, upstream, pinned version/build, SHA-256 per platform, license); `cores.yml` processes the list instead of one job per core.
  - Source: where possible take prebuilt cores from the libretro buildbot (buildbot.libretro.com) instead of building. The workflow downloads once, pins the SHA-256, signs with the FrameBeam key and mirrors the files into FrameBeam's own releases, because the buildbot overwrites "latest" and signs nothing. Hubs keep using only FrameBeam's signed index. Own builds stay possible per entry (e.g. melonDS DS today).
  - Metadata: use libretro core info files (supported extensions, firmware with checksums) to generate or check parts of the system manifests; a short manifest per system (input, options, firmware mode) stays manual.
  - Updates: a scheduled workflow checks upstream for new versions weekly and opens a PR that bumps the pin; merging publishes the package.
  - Licenses: keep each core's exact license terms with the package and show them. Some cores are non-commercial only; that restricts use as well as redistribution, so the core list marks such cores and FrameBeam does not mirror them without checking their terms.

## 0.9 Metadata and artwork

- Hub Metadata Service with provider abstraction, hash matching, overrides, artwork cache (`docs/architecture/11-metadata-future.md`).
- Hub: Settings → Metadata, actions in the library entry. Player: boxart and basic data in Library and game view.

## 0.10 Hub for Windows / Windows Server

- Windows amd64 Hub build in CI and release; runs as a Windows service (start/stop, automatic start).
- Hub installer (data directory, port, firewall rule, admin setup); updates via the updater from 0.3.
- Review paths, file permissions (instead of 0600) and certificate storage on Windows.

## 0.11 Linux and macOS Player

- Linux: Secret Service instead of in-memory credentials, package (AppImage or .deb/Flatpak), check gamepads and audio.
- macOS: Keychain, app bundle/dmg, signing and notarization, VideoToolbox encoder, CI on a macOS runner.
- Updater and core distribution for both platforms.

## Later, only on demand

- Save States, remote control/input for viewers, netplay, emulation settings sync, StandaloneBackend, Hub as a macOS service.
- Further library features: row actions (delete, edit title), paging.
- Remaining Nintendo systems from NES to GameCube/Wii (Fabio, 2026-10-07), as plain core packages after 0.8, rendered with OpenGL first: NES, SNES, N64, GB/GBC, GBA, Virtual Boy and GameCube/Wii (Dolphin; the libretro port lags upstream). N64 and GameCube/Wii build on the hardware rendering from 0.5.
- Afterwards Vulkan hardware rendering next to OpenGL (Fabio, 2026-10-07), so cores that offer both can be switched per core option in the Emulation settings.
- Wii U deferred (Fabio, 2026-10-07): Cemu has no libretro core and would need the StandaloneBackend.
- To evaluate (Fabio, 2026-10-07): Cloudflare in front of a self-hosted Hub, so no router port forwards are needed. The Hub itself stays self-hosted; running it on Workers/Pages would be a rewrite and is not planned.
  - Cloudflare TURN (Realtime) as an optional TURN provider next to the embedded `pion/turn` from 0.4: the Hub hands out short-lived Cloudflare TURN credentials (API token as a secret on the Hub host). Removes the UDP forwards for 3478 and the relay range; costs only for relayed traffic.
  - Cloudflare Tunnel (`cloudflared` on the Hub host) for HTTPS, API and WebSocket signaling. Prerequisites: a Player Hub profile mode that validates the certificate by hostname/CA instead of pinning (Cloudflare terminates TLS with rotating certificates), and chunked uploads in the Player, the Hub web uploader and the Hub API, because proxied requests are limited to 100 MB on the free plan while ROMs may be up to 4 GiB (otherwise large uploads have to happen locally). WebRTC media and TURN never go through the Tunnel (no raw UDP).
  - Check Cloudflare's terms for large binary downloads through the proxy and the plan limits before deciding; record the decision as an ADR.

## Out of scope

Hosted emulation, friends list, public Session links, guest access, email/password recovery, own ACME client, simultaneous multi-Hub use, federation, cross-Hub Sessions, OAuth/central accounts, web client. They stay out until actively wanted.

## Open decisions

- **Second system:** decided on 2026-10-07: Nintendo 3DS with Azahar (see 0.8). The earlier proposal GBA with mGBA is dropped.
- **Internet reachability:** decided in 0.4 (ADR 0012); the text below is the original proposal. Which setups FrameBeam supports and documents (Hub port forward, VPN, own TURN server on the Hub host or elsewhere). Proposal (2026-10-07): the Hub embeds an optional STUN/TURN server (for example `pion/turn`, pure Go, same binary) on UDP/TCP 3478 plus a small UDP relay port range, hands out short-lived TURN credentials in `hello_ack`/join instead of static secrets, and learns its public address from a configured hostname (DynDNS). A typical setup: a DynDNS name pointing at the Hub's router, port forwards for the Hub port, 3478 and the relay range; check first that the connection has a public IPv4 (no CGNAT/DS-Lite).
- **Update channels:** decided on 2026-10-06: the pre-release channel (renamed from test to beta on 2026-10-07) updates automatically, the stable channel only after confirmation. Details and rollback proposal: [ADR 0011](adr/0011-automatic-updates.md). Numbering, "Promote to release" and the default channel changed on 2026-10-08: [ADR 0016](adr/0016-release-numbering-and-promotion.md).
- **0.2:** decided in ADR 0010 (source, index, signing tooling, offline import, license file per package, cached cores stay usable without a Hub connection to GitHub). The FrameBeam release key exists since 2026-10-07.
- **Core sourcing:** decided on 2026-10-07: libretro buildbot cores, mirrored and signed by FrameBeam (see 0.8).
