# client/ – FrameBeam Player (C++20, Qt 6/QML, SDL3)

Details only as needed from `docs/architecture/` (index: `docs/architecture/README.md`).

## Responsibilities
- Library access, ROM/core/firmware cache (separate, ROM hash-based), save sync, Session manager, emulation, rendering incl. multiview, audio, encoder/decoder, input. See `01-overview.md`, `02-protocols-and-rom-cache.md`.

## Emulation
- `EmulatorBackend` -> `LibretroBackend` (later possibly `StandaloneBackend`). Systems and cores are data-driven (ADR 0020 D6): system manifests `emulation/manifests/systems/<system>.json` and core profiles `manifests/cores/<core_id>.json` (`melondsds`, `desmume`; cores without profile are experimental), core choice game > system > Hub default among the cores the Hub serves (`framebeam.core`), no console-specific launch logic. A save last written by another core/build is snapshotted on the Hub before the start (D7).
- Core options dynamically from Libretro core options, no invented options. See `05-emulation.md`.
- Hardware rendering (ADR 0013): offscreen OpenGL context per game on the emulation thread, Player-owned FBO, every frame read back into the XRGB8888 path; unavailable in the CLI, with `FRAMEBEAM_DISABLE_HW_RENDER=1` or if context creation fails (cores fall back to software). ADR 0019: while a Session is shared on NVIDIA the emulation thread also blits a Session encode texture and hands it to `emu::GpuEncodeTarget` (CUDA-GL interop; all interop calls on the emulation thread with GL current).
- Settings are local: global -> system/core -> game override, partial overrides only. Controller profiles are local (`06-controllers.md`).

## Hub binding
- Exactly one active Hub; library, saves, Sessions, registry only from it.
- Local data and pending sync are separated by Hub ID (and Hub user ID), even with identical ROM hashes. No global accounts.
- Credentials only in the OS credential store, never in profile files/logs. TOFU/pinning: a certificate mismatch warns and is never silently accepted. See `10-identity-pairing-tls.md`.

## Save sync (Player view)
- Upload only when the hash changed; debounce approx. 10-15 s, periodic at most every approx. 60 s; pause/stop/clean exit = immediate final sync.
- Failed uploads are kept as pending sync (Hub, user, game/slot, base version) and retried only to the originating Hub. Save states are separate, not in the PoC. See `03-saves.md`.

- Data: all Player data under `ProfileStore::baseDir()` (portable `<exe-dir>/data`, ADR 0004); never invent own paths.

## Media and language
- FFmpeg (libavcodec) with NVENC/QSV/AMF, OpenH264 as software fallback; encoder order h264_nvenc, h264_qsv, h264_amf, libopenh264, libx264 (ADR 0006 D5; the Windows vcpkg FFmpeg enables `nvcodec`, `qsv` and `amf` since 0.1.1, ADR 0009 D7; opening them needs a GPU and is verified only locally, otherwise it falls back to libopenh264); GPU-direct encoding (ADR 0019): `CudaGlCapture` feeds CUDA frames to a second `VideoEncoder` (`h264_nvenc`); failures fall back to readback frames, never to the sticky encoder failure; `FRAMEBEAM_DISABLE_GPU_ENCODE=1` and a forced `FRAMEBEAM_H264_ENCODER` keep readback; audio output via Qt Multimedia, SDL3 only for gamepads (ADR 0007); WebRTC with libdatachannel (ADR 0001, `04-sessions-and-multiview.md`).
- C++20 as baseline; C++23 only if MSVC, GCC and AppleClang support it. vcpkg (manifest) + CMake presets.
- Firmware missing: "Firmware required/missing", block launch.
- Design: Player UI `docs/design/README.md`, `docs/design/player.md`, `docs/design/tokens.md`.

## Build targets (ADR 0003, 0006, 0007)
- Targets: `framebeam_core` (core/ + network/ sources), `framebeam_emulation`, `framebeam_input` + `framebeam_sdl3` (input/), `framebeam_media` + `framebeam_audioutil` + `framebeam_media_deps` (media/), `framebeam_ui` (static lib, QML module `FrameBeam.Player`), executables `framebeam_player` (app/) and `framebeam_player_cli` (network/cli/). Qt >= 6.4, 6.4 API only, not via vcpkg.
- Core path: CMake variable/environment `FRAMEBEAM_MELONDS_DS_CORE` (`FRAMEBEAM_DESMUME_CORE` for DeSmuME, test `emulation_core_desmume`); empty = tests with `NEEDS_CORE` are skipped. Never check in a core, ROM or BIOS.
