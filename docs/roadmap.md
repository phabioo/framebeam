# Roadmap after the PoC

The PoC (phases 0-5) is complete. From here on, work is planned as versions 0.1.1 to 0.11, each cut into one work package per thread/PR (see `docs/workflow.md`). The order was decided by Fabio on 2026-10-06; on 2026-10-07 he moved Sessions over the internet ahead of the UI passes (renumbered 0.4 to 0.6). On 2026-10-07 Fabio also moved OpenGL hardware rendering for the Player (all emulators/cores, not only 3DS) ahead of the UI passes as the new 0.5; the UI passes, 3DS and everything after shifted by one. ADRs written before 2026-10-07 use the old numbering: old 0.5/0.6/0.7/0.8/0.9/0.10 = new 0.6/0.7/0.8/0.9/0.10/0.11. Items marked open are not decided yet; decisions are recorded as ADRs in `docs/adr/`.

## Overview

| Version | Theme | Goal |
|---|---|---|
| 0.1.1 | Finish the PoC | Close the PoC leftovers and clean up the codebase. |
| 0.2 | Cores from the Hub | Installers no longer ship emulator cores; the Hub distributes signed core packages. |
| 0.3 | Automatic updates | A change on `main` reaches the test devices without manual work. |
| 0.4 | Sessions over the internet and save comfort | Sessions work beyond the LAN; saves get retention, restore and slots. |
| 0.5 | OpenGL hardware rendering | The Player offers an OpenGL context to libretro cores, so hardware-rendered cores run and present without a CPU copy. |
| 0.6 | Player UI pass | FrameBeam Player is clearer, more responsive and consistent with the design tokens. |
| 0.7 | Hub UI pass | FrameBeam Hub web UI swaps fragments instead of full pages and updates live. |
| 0.8 | Second system: 3DS (Azahar) | The Azahar libretro core ships as a plain core package and runs on the OpenGL rendering from 0.5. |
| 0.9 | Metadata and artwork | Central game metadata and boxart in Hub and Player. |
| 0.10 | Hub for Windows / Windows Server | FrameBeam Hub runs as a Windows service with an installer. |
| 0.11 | Linux and macOS Player | FrameBeam Player on Linux and macOS. |

## 0.1.1 Finish the PoC

PoC leftovers. Decisions: [ADR 0009](adr/0009-finish-poc.md) (proposed).

- [x] Done: Hardware encoders NVENC/QSV/AMF: the Windows FFmpeg in `client/vcpkg.json` enables `nvcodec`, `qsv` and `amf`; a Windows-only test checks that `h264_nvenc`, `h264_qsv` and `h264_amf` are compiled in. ADR 0006 is corrected. Opening the encoders needs a GPU and is verified only locally by Fabio.
- [x] Done: Multiview picker lists all Sessions in a scrollable list; the "+N more in the Library" pointer is gone.
- [x] Done: Settings → Hubs (switch, remove with confirmation, auto-connect, current Hub marked); switching ends running work and secures saves like the connection screen.
- [x] Done: Certificate renewal and confirmed pin change. The Hub renews its self-generated certificate at startup when expired or expiring within 30 days (`framebeam-hub renew-cert`, Settings badge); the Player shows both fingerprints and re-pins after a two-step confirmation, keeping the credential (`--accept-fingerprint` in the CLI). Own certificate and key are never modified. Verified only locally by Fabio against a real Hub certificate.
- [x] Done: Session encoding and RTP send run on a worker thread (bounded queue, oldest video frame dropped); RTT is reported in diagnostics via the negotiated DataChannel `fb-diag`.
- [ ] Open: A core version mismatch only warns (ADR 0007). Intentionally unchanged; moves to 0.8, when several cores exist.
- [x] Done: Codebase cleanup: system display name and controller labels from the system manifest, phase-named files and tests renamed (migration `0004_phase5.sql` kept), MSVC C4804 fixed, E2E scripts run in the Linux CI job, staticcheck in `make check-hub`, narrower libdatachannel CI cache path. The `PlayerController` split moves to the Player UI pass (0.6).

Completed 2026-10-06: Windows CI runs on `main` pushes to prime its vcpkg binary cache after merges. The release preset uses a release-only dependency triplet. Cache keys distinguish the triplet and MSVC version and cover the manifest, presets and overlay triplets. The first main run with the new triplet is expected to build cold; later runtimes depend on cache hits and runner performance. See [ADR 0008](adr/0008-windows-ci-cache.md).

## 0.2 Cores from the Hub

Decisions: [ADR 0010](adr/0010-cores-from-the-hub.md) (proposed).

Goal: installers ship no emulator cores. melonDS DS leaves the Windows installer and comes from the Hub.

Decision: the Hub obtains signed core packages from a fixed trusted source and caches them. There is no admin upload.

- [x] Done: Hub: Core Package Cache (storage per core ID, version, platform/architecture, SHA-256, origin, license text). The Hub downloads signed packages from the trusted source, verifies the signature and caches them. On "Systems & Cores" (replaces the "LATER" placeholder) the admin only selects the version. Download endpoint for Players with ETag.
- [x] Done: Protocol: endpoints for package metadata and download, a handshake feature (for example `cores_v1`); `protocol_version` stays 1.
- [x] Done: Player: core cache `cache/cores/<core-id>/<version>/<platform>/`, download on demand with hash, version and platform checks, `CoreLocator` searches there. Library and Emulation page show "core loading / missing / incompatible".
- [x] Done: Trust: Ed25519-signed index (`framebeam-sign`, `.github/workflows/cores.yml`), checked by the Hub against compiled-in and `--core-trust-key` keys; the Player checks size and SHA-256 (ADR 0010). The key is reused by the updater in 0.3.
- [x] Done: FrameBeam release key generated by Fabio (GitHub secret `FRAMEBEAM_SIGNING_KEY`, public key in `corepkg.DefaultTrustedKeys`).
- [x] Done: Package format and index defined (ADR 0010, `protocol/README.md`). Source: FrameBeam's own GitHub Releases; the Hub needs internet access to github.com or uses `framebeam-hub import-cores <dir>` offline.
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

- [x] Done: Supported setups decided and documented: LAN, internet with port forward (recommended), VPN; no external TURN server in 0.4 (ADR 0012 D1; `packaging/linux/README.md`, "Sessions over the internet").
- [x] Done: Hub: embedded STUN/TURN relay (pion/turn, off by default) with short-lived credentials in `hello_ack` and the join response; Settings shows TURN status and the router port forwards (ADR 0012 D2-D4).
- [x] Done: Player: TURN via libdatachannel, connection type (direct / relay) in diagnostics, `FRAMEBEAM_FORCE_RELAY` / `--force-relay`, AIMD bitrate adaptation to the worst viewer (ADR 0012 D5).
- [x] Done: Multiview with up to 4 surfaces (several remote Sessions) and the audio focus rule (ADR 0012 D8).
- [x] Done: Player-side signature check of core packages (moved from 0.3, ADR 0010 D5): the Player verifies the signed core index served by the Hub (ADR 0012 D6).
- [x] Done: Saves: retention/thinning, restore from history, manual snapshot, WebSocket push `save_updated`, multiple slots (ADR 0012 D7; `docs/architecture/03-saves.md`).
- Open: verification across real networks (two Players behind different routers, forced relay, more than one remote Session) is done locally by Fabio.

## 0.5 OpenGL hardware rendering

Decided by Fabio on 2026-10-07: OpenGL hardware rendering for the Player, for all emulators/cores and not only 3DS, comes before the UI passes.

- Libretro hardware rendering (new for the Player, largest work item): the Player's libretro backend handles only software framebuffers today (no `RETRO_ENVIRONMENT_SET_HW_RENDER`). Needed: an OpenGL 3.3 core context (shared with the Qt Quick scene or offscreen), FBO handed to the core via `get_current_framebuffer`, `context_reset`/`context_destroy` handling, presenting the FBO without a CPU copy, and a GPU readback (or zero-copy path to the encoder) when the Session is shared.
- CI: headless CI cannot run hardware-rendered cores end to end; core tests need a GL context (for example Mesa llvmpipe) or stay local.
- melonDS DS then gets its OpenGL renderer and the internal resolution option; both are hidden today because the Player offers no GL context (see the Emulation page in `docs/design/player.md`).
- Later Nintendo systems rely on it: the 3DS (0.8) and, among the later systems, N64 and GameCube/Wii.

## 0.6 Player UI pass

- Clarity: rework information density and grouping per screen (Library, Detail, game view, Emulation, Controllers, Settings).
- Spacing, sizes and alignment consistently from the design tokens (`docs/design/tokens.md`) instead of single values.
- Responsiveness: nothing blocking on the UI thread (network, hashing, encoding), loading and progress states instead of freezing, immediate click feedback.
- Dynamics: transitions and animations (page change, hover, lists); live updates of Library, Sessions and sync status without manual reload.
- One pass with before/after screenshots per screen to allow targeted feedback.
- Core state refresh: after a core download the "core missing" notice on NDS games stays until the Player restarts (found by Fabio on 2026-10-07); Library and Detail must re-evaluate the core state live.
- From 0.4 (requested by Fabio on 2026-10-07): Settings → Hubs lets the user edit a Hub's address and port (for example after the Hub port changed or when switching between LAN address and public name), keeping the pinned fingerprint and credential; diagnostics present the connection type (direct / relay) and target bitrate clearly; multiview layouts and the save history, restore, snapshot and slot picker get their final design.

## 0.7 Hub UI pass

- Same approach for the web UI: clarity, spacing from tokens.
- No full page loads on navigation: switching pages swaps only the content area. Some actions already swap htmx fragments (Library filter/delete, Clients actions and 15 s polling, Saves filter); sidebar navigation still loads whole pages.
- Live updates without reload: sidebar badges (new pending clients, save conflicts, firmware) and affected tables update themselves, for example via Server-Sent Events.
- From 0.4 (requested by Fabio on 2026-10-07): a network settings form instead of editing `hub.env`: Hub port, embedded TURN on/off, public host, TURN port and relay range, and the save retention values. Decided by Fabio on 2026-10-07: web settings win; `hub.env` and flags only provide the initial value; changes that need it (for example the port) are applied by a service restart the Hub triggers itself, like the updater. Open: ports below 1024. The TURN status panel and the router port forward list from 0.4 move into this form.

## 0.8 Second system: Nintendo 3DS with Azahar

Decided by Fabio on 2026-10-07: the second system is the Nintendo 3DS with the Azahar libretro core (replaces the mGBA proposal).

- Core: `azahar_libretro` (GPLv2+), available prebuilt on the libretro buildbot (checked 2026-10-07: `nightly/windows/x86_64/latest/azahar_libretro.dll.zip`) and described by `azahar_libretro.info` in libretro-core-info. GPLv2+ allows mirroring only with the complete corresponding source: the workflow archives the exact source revision (including build scripts and submodules) as a release asset next to every mirrored binary and keeps it as long as that binary is published (GPLv2 §3(a)); the package carries the license text and names that archive. The same applies to every GPL core in the core list.
- Hardware rendering: the core info sets `hw_render = true`, `required_hw_api = OpenGL Core >= 3.3`. The Player's OpenGL hardware rendering is a prerequisite and arrives in 0.5; this version only uses it.
- Display: two screens of different sizes (top 400 x 240, bottom 320 x 240 touch); the system manifest describes layout and touch mapping, and the Session encoder sends the composed frame.
- Games: decrypted dumps only (`.3ds`/`.cci`/`.cxi`/`.3dsx`, plus the compressed `z*` variants); FrameBeam never decrypts. Some games need system files such as Mii data dumped from the user's own console; these follow the firmware path from phase 5 and are never shipped.
- Performance: 3DS emulation needs a considerably faster CPU/GPU than DS; check on Fabio's devices whether play plus encoding fits.
- It arrives only as another package through the core distribution from 0.2, without installer changes.
- Re-evaluate the core version check (warning vs. block, ADR 0007).
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
  - Cloudflare Tunnel (`cloudflared` on the Hub host) for HTTPS, API and WebSocket signaling. Prerequisites in the Player: a Hub profile mode that validates the certificate by hostname/CA instead of pinning (Cloudflare terminates TLS with rotating certificates), and chunked uploads, because proxied requests are limited to 100 MB on the free plan while ROMs may be up to 4 GiB. WebRTC media and TURN never go through the Tunnel (no raw UDP).
  - Check Cloudflare's terms for large binary downloads through the proxy and the plan limits before deciding; record the decision as an ADR.

## Out of scope

Hosted emulation, friends list, public Session links, guest access, email/password recovery, own ACME client, simultaneous multi-Hub use, federation, cross-Hub Sessions, OAuth/central accounts, web client. They stay out until actively wanted.

## Open decisions

- **Second system:** decided on 2026-10-07: Nintendo 3DS with Azahar (see 0.8). The earlier proposal GBA with mGBA is dropped.
- **Internet reachability:** decided in 0.4 (ADR 0012); the text below is the original proposal. Which setups FrameBeam supports and documents (Hub port forward, VPN, own TURN server on the Hub host or elsewhere). Proposal (2026-10-07): the Hub embeds an optional STUN/TURN server (for example `pion/turn`, pure Go, same binary) on UDP/TCP 3478 plus a small UDP relay port range, hands out short-lived TURN credentials in `hello_ack`/join instead of static secrets, and learns its public address from a configured hostname (DynDNS). A typical setup: a DynDNS name pointing at the Hub's router, port forwards for the Hub port, 3478 and the relay range; check first that the connection has a public IPv4 (no CGNAT/DS-Lite).
- **Update channels:** decided on 2026-10-06: the pre-release channel (renamed from test to beta on 2026-10-07) updates automatically, the stable channel only after confirmation. Details and rollback proposal: [ADR 0011](adr/0011-automatic-updates.md).
- **0.2:** decided in ADR 0010 (source, index, signing tooling, offline import, license file per package, cached cores stay usable without a Hub connection to GitHub). The FrameBeam release key exists since 2026-10-07.
- **Core sourcing:** decided on 2026-10-07: libretro buildbot cores, mirrored and signed by FrameBeam (see 0.8).
