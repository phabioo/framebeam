# ADR 0003: FrameBeam Player in phase 2

- Status: accepted (storage location superseded by ADR 0004, audio plan by ADR 0007)
- Date: 2026-10-05
- Decided by: Fabio (proposal by the orchestrator, confirmed for the PoC on 2026-10-05)

## Context

Phase 2 ("Playable vertical slice") builds the FrameBeam Player: Hub profile, pairing, library, ROM cache, `LibretroBackend` with melonDS DS and a minimal Qt UI. The architecture leaves toolchain, core sourcing, storage and credential store open or deviates from them. This ADR records the decisions; Fabio accepted them for the PoC on 2026-10-05. The architecture documents remain unchanged.

## Decisions

- **Qt:** Qt >= 6.4, not via vcpkg. Linux: apt (6.4.2, Ubuntu noble). Windows: `install-qt-action` (6.8 LTS). The code uses only the 6.4 API. Reason: A Qt build via vcpkg takes hours in CI, and the cloud cannot fetch vcpkg sources. vcpkg remains for later packages.
- **melonDS DS:** Version v1.4.0, commit pinned (`scripts/melonds-ds.pin`). Linux builds the core from source via git (`scripts/fetch-melonds-ds.sh`). Windows uses the official release asset `melondsds_libretro-win32-x86_64-Release.zip` with a pinned SHA-256 (`scripts/fetch-melonds-ds.ps1`); its build is MinGW, and loading it via `LoadLibrary` over the C ABI is unproblematic. The Windows CI artifact `framebeam-player-windows-x64` contains the Player, the Qt runtime, the core under `cores/` and the GPL notice.
- **Audio:** In phase 2 via Qt Multimedia. SDL3 arrives with gamepads (phase 5). (Superseded in part by ADR 0007: SDL3 is used for gamepads only; audio stays on Qt Multimedia.)
- **HTTP/TLS:** via QtNetwork. Trust exclusively via the leaf fingerprint (SHA-256 over DER, format as in the Hub), also for CA-signed certificates. First contact shows the fingerprint and requires confirmation (architecture 10; deviation from mock-up 3b). A fingerprint mismatch blocks the connection. Consequence: A certificate change (including Let's Encrypt behind a reverse proxy) requires removing the profile and reconnecting until the confirmed pin change is specified.
- **Storage:** `AppDataLocation` (superseded by ADR 0004). `profiles.json` and `device.json` contain no secrets. The device ID is generated locally. Hub-specific data lives under `hubs/<hub_id>/`, including the core's save directory (sync follows in phase 3). The ROM cache is content-addressed and shared across Hubs: `cache/roms/<sha256>.<ext>`. Rationale: The content is uniquely identified by its hash, and the files contain no data attributable to a Hub. The download goes into a `.part` file with Range resume; the hash is verified before the atomic rename; a sidecar with size and mtime avoids re-hashing.
- **Credentials:** Windows: Credential Manager. Linux/macOS: in-memory only in phase 2 (not in PoC scope); a new pairing is needed there after a restart.
- **Emulation:** `EmulatorBackend` and `LibretroBackend`. Only one core instance per process (libretro globals). Software renderer; hardware rendering is rejected. Systems come via manifest (`client/emulation/manifests/nds.json`) including core option defaults (`render_mode` software, layout `top-bottom`, `boot_mode` direct). Homebrew needs no firmware (FreeBIOS); the firmware path follows in phase 5. Tests use a homebrew test ROM generated at build time, never a file in the repository.
- **Pairing cancel:** local only. The API has no cancel; the request expires after 10 min (ADR 0002).

## Open

- ~~Confirmed pin change on certificate renewal (as in ADR 0002).~~ Update 2026-10-06: resolved in 0.1.1 ([ADR 0009](0009-finish-poc.md) D1).
- Credential store for Linux and macOS (Secret Service or Keychain, respectively).

## Rejected

- **Qt via vcpkg:** CI build time, sources not fetchable in the cloud.
- **Core for Windows built from source with MSVC:** The official MinGW release asset suffices over the C ABI.
- **SDL3 audio in phase 2:** no gain before gamepads; Qt Multimedia is enough.
- **Hardware rendering of the core:** The software renderer is sufficient for the vertical slice and easier to test.
- **ROM cache per Hub:** Duplicates without benefit, since the hash uniquely determines the content.
- **Silently accepting a changed certificate:** contradicts the hard rules (a mismatch blocks).

## Consequences

- The Player code in `client/` follows these decisions; changes only via a new ADR.
- Linux/macOS Players lose the pairing on restart until the credential store is implemented.
- A certificate change at the Hub forces reconnecting.
