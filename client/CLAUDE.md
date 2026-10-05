# client/ – FrameBeam Player (C++20, Qt 6/QML, SDL3)

Details only as needed from `docs/architecture/` (index: `docs/architecture/README.md`).

## Responsibilities
- Library access, ROM/core/firmware cache (separate, ROM hash-based), save sync, Session manager, emulation, rendering incl. multiview, audio, encoder/decoder, input. See `01-overview.md`, `02-protocols-and-rom-cache.md`.

## Emulation
- `EmulatorBackend` -> `LibretroBackend` (later possibly `StandaloneBackend`). Systems/cores are data-driven via manifests (e.g. `nds` -> `melonds_ds`), no console-specific launch logic.
- Core options dynamically from Libretro core options, no invented options. See `05-emulation.md`.
- Settings are local: global -> system/core -> game override, partial overrides only. Controller profiles are local (`06-controllers.md`).

## Hub binding
- Exactly one active Hub; library, saves, Sessions, registry only from it.
- Local data and pending sync are separated by Hub ID (and Hub user ID), even with identical ROM hashes. No global accounts.
- Credentials only in the OS credential store, never in profile files/logs. TOFU/pinning: a certificate mismatch warns and is never silently accepted. See `10-identity-pairing-tls.md`.

## Save sync (Player view)
- Upload only when the hash changed; debounce approx. 10-15 s, periodic at most every approx. 60 s; pause/stop/clean exit = immediate final sync.
- Failed uploads are kept as pending sync (Hub, user, game/slot, base version) and retried only to the originating Hub. Save states are separate, not in the PoC. See `03-saves.md`.

## Media and language
- FFmpeg (libavcodec) with NVENC/QSV/AMF, OpenH264 as software fallback; WebRTC with libdatachannel (ADR 0001, `04-sessions-and-multiview.md`).
- C++20 as baseline; C++23 only if MSVC, GCC and AppleClang support it. vcpkg (manifest) + CMake presets.
- Firmware missing: "Firmware required/missing", block launch.
- Design: Player UI `docs/design/README.md`, `docs/design/player.md`, `docs/design/tokens.md`.

## Phase 2 (ADR 0003)
- Targets: `framebeam_core` (core/ + network/), `framebeam_emulation`, `framebeam_ui` (static lib, QML module `FrameBeam.Player`), executable `framebeam_player` in `app/`. Qt >= 6.4, 6.4 API only, not via vcpkg.
- Core path: CMake variable/environment `FRAMEBEAM_MELONDS_DS_CORE`; empty = tests with `NEEDS_CORE` are skipped. Never check in a core, ROM or BIOS.
