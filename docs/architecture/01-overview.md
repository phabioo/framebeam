# Architecture: overview and tech stack

As of: 2026-10-05 · architecture draft 0.1  
Basis: architecture discussion "Concept for retro streaming".

## 1. Goal and basic principle

FrameBeam combines a central ROM library and versioned saves with local emulation and the sharing of running Sessions. FrameBeam Player and FrameBeam Hub are meant to be lightweight, performant and usable long-term on Windows, Linux and macOS. Further emulators should be addable without a fundamental rework.

> **The Hub manages. The Player emulates. Audio and video flow directly between Players wherever possible.**

```text
                   FrameBeam Hub
          Library · Saves · Auth · Registry
          Presence / Session metadata internal
                 HTTPS/JSON + WSS
                    /           \
             Player A         Player B
             Emulator         Emulator
             Rendering        Rendering
                    \           /
                     WebRTC/P2P
                     Video + Audio
```

## 2. Responsibilities and tech stack

| Area | Decision | Task |
|---|---|---|
| Hub | Go, without a large framework | HTTPS API, file management, authentication, registry, internal Session metadata and signaling |
| Management data | SQLite | Games, ROM hashes, Hub-local users, devices, token assignments, save versions, save conflicts and registry; Session metadata internal |
| File storage | Plain files | ROMs, saves and admin-provided firmware; no binary data in SQLite |
| Player | C++23 + CMake | Emulation, media processing, networking and application logic |
| UI | Qt 6 + QML | GPU-accelerated library, settings and multiple video surfaces |
| Emulator integration | `EmulatorBackend` → `LibretroBackend` | Swappable emulator cores |
| First core | melonDS DS via Libretro | Nintendo DS emulation |
| Input/audio | SDL3 | Cross-platform input/audio layer, in particular gamepads |
| Streaming | libdatachannel | WebRTC connections between clients |
| Media formats | H.264 + Opus | Video and audio transmission |
| Windows encoding | NVENC / QSV / AMF; software H.264 as fallback | Hardware encoding where available |

> Superseded by [ADR 0001](../adr/0001-stack-additions.md) (C++20 baseline, not C++23) and [ADR 0007](../adr/0007-phase5.md) (audio output via Qt Multimedia; SDL3 only for gamepads). Hardware-rendered cores: [ADR 0013](../adr/0013-opengl-hardware-rendering.md).

The server performs no emulation, no encoding, no decoding and no multiview rendering. The client contains library access, separate ROM/core caches, local emulator/controller settings, save sync, Session manager, emulator backend, rendering, audio, encoder/decoder and input.

The Hub manages Hub-local users and device pairing. The Player manages the local device identity and stored Hub profiles; a connected Player instance uses exactly one active Hub (see section 14). A central metadata service with a provider abstraction and artwork cache is planned exclusively as a later extension (see section 15).

The server runs as a single service, on Linux for example via `systemd`, with SQLite and data under `/var/lib/framebeam/`. Docker is optional. Node.js, Redis, PostgreSQL and Kubernetes are not required for the planned baseline.
