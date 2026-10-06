# ADR 0001: Stack additions

- Status: accepted
- Date: 2026-10-05
- Decided by: Fabio

## Context

`docs/architecture/` (draft 0.1, index: `README.md`) defines the base stack but leaves encoder/decoder integration, the Hub web interface, package management and CI open. This ADR fills in those points.

## Decisions

- **Encode/decode:** FFmpeg (libavcodec). Windows: NVENC/QSV/AMF; later VideoToolbox/VAAPI. Software fallback for H.264: OpenH264 (licence less problematic than x264).
- **WebRTC:** libdatachannel stays. Jitter buffer and simple bitrate adaptation are in-house work. libwebrtc and GStreamer are rejected (build and distribution effort).
- **Player language:** C++20 as baseline. C++23 features only if MSVC, GCC and AppleClang support them. This deliberately deviates from "C++23" in `docs/architecture/01-overview.md`.
- **Hub web interface:** Go `html/template` + htmx, embedded in the Hub binary via `embed`. No Node build.
- **C++ dependencies:** vcpkg in manifest mode, build via CMake presets.
- **CI:** GitHub Actions. Linux jobs on every push, Windows jobs on pull requests against `main` and manually (`workflow_dispatch`). The repository is public. The Windows trigger and cache policy is superseded by [ADR 0008](0008-windows-ci-cache.md).

## Rejected

- **Rust instead of Go/C++:** A switch would force Qt, SDL3, FFmpeg and libdatachannel bindings through FFI and restart the existing draft, without advancing the PoC.
- **Electron/Tauri instead of Qt/QML:** Multiple GPU-accelerated video surfaces, native decoder access and the emulator frame loop are more laborious and less lightweight in a web runtime than in Qt.

## Consequences

- The architecture documents remain unchanged; deviations apply via this ADR.
- Licences of the FFmpeg build and OpenH264 must be reviewed during packaging.
