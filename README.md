# FrameBeam

FrameBeam is a self-hosted retro gaming platform. The **FrameBeam Hub** manages the central ROM library, versioned saves, users and devices. The **FrameBeam Player** emulates locally and shares running Sessions directly with other Players via WebRTC. The Hub never emulates, encodes or renders.

## Status

Phase plan: [Workflow](docs/workflow.md#phase-plan). The PoC (phases 0-5) is complete; post-PoC work follows the [Roadmap](docs/roadmap.md).

| Phase | Status | Scope |
|---|---|---|
| 0 Foundation | done | Monorepo, CLAUDE.md, architecture, CI (Linux/Windows), build scaffolding, check scripts |
| 1 Protocol and Hub basics | done | OpenAPI `/api/v1`, WSS schemas, Hub with SQLite, admin setup, TLS, pairing, tokens, library, ROM download, web interface |
| 2 Playable vertical slice | done | Player core (profile, pairing, library, ROM cache), melonDS DS via Libretro, minimal Qt UI |
| 3 Saves | done | Save storage, sync, versions, conflict model ([ADR 0005](docs/adr/0005-saves-phase3.md)) |
| 4 Session sharing and multiview | done (tested locally on two Windows PCs) | Presence, signaling, WebRTC, multiview ([ADR 0006](docs/adr/0006-sessions-phase4.md), accepted) |
| 5 Remainder and polish | done (tested locally) | Users and invites, user uploads, systems and firmware, Emulation and Controllers pages, appearance, Windows installer ([ADR 0007](docs/adr/0007-phase5.md), accepted) |
| 0.1.1 Finish the PoC | done in code; hardware encoders and real certificates verified only locally | Hardware encoder features on Windows, Settings → Hubs, certificate renewal and change confirmation, RTT in diagnostics ([ADR 0009](docs/adr/0009-finish-poc.md), proposed) |
| Post-PoC | planned | See [Roadmap](docs/roadmap.md) (versions 0.2 to 0.10) |

## What works

**FrameBeam Hub** (`server/`)

- Admin setup via `setup-admin` and via `/setup` in the web interface (loopback only).
- HTTPS with a self-signed certificate (or your own certificate); the fingerprint is logged at startup. The Hub renews its own certificate at startup when it is expired or expires within 30 days (`framebeam-hub renew-cert` does it on demand); the Settings page shows "Expires within 30 days". Own certificates are never modified.
- Web interface with login (admins only): Library, Saves, Systems & Cores, Clients, Users, Settings.
- Pairing of new devices with Allow/Deny; issue and revoke tokens (Revoke).
- ROM upload in the web interface.
- ROM download via API with Range and ETag.
- Versioned saves per user and game (API tag `saves`, added in API 1.1.0, `protocol_version` stays 1, handshake feature `saves_v1`); the Hub stores the save as an opaque blob and never merges it.
- Web page "Saves" (admin): slots, conflicts ("Use Hub version" / "Adopt local save"), history with Download, badge in the navigation.
- Info endpoint `/.well-known/framebeam` and handshake with `protocol_version`.
- Sessions: session API (visibility Private / Hub users / Invite only, invites, viewers), WSS presence and signaling relay, revoke on visibility change; optional STUN servers via `-ice-servers` / `FRAMEBEAM_ICE_SERVERS`.
- Users and invites: Users page creates single-use invite codes (shown once), disable/enable users, display names unique; Clients page assigns a pending device to a user.
- Settings: "Allow users to upload games" and Appearance (Light / Dark / System).
- Systems & Cores page: expected core version, reports from clients, firmware mode and firmware files per system (user-supplied, never shipped).
- Ships with a systemd installer for Linux / Raspberry Pi (`packaging/linux/`).

**Protocol** (`protocol/`)

- OpenAPI 3.0.3 for `/api/v1` and WSS message schemas; OpenAPI spec version 1.3.0, `protocol_version` is 1; handshake features `saves_v1`, `sessions_v1`, `users_v1`, `uploads_v1`, `firmware_v1`.

**FrameBeam Player** (`client/`, [ADR 0003](docs/adr/0003-player-phase2.md))

- Connection screen with Hub profiles and auto-connect.
- Hub identification with fingerprint confirmation on first contact (TOFU); on mismatch the connection is blocked and no credential is sent. After a Hub certificate change the Hub card shows the stored and the presented fingerprint; "Trust new certificate" and then "Yes, trust this certificate" re-pin it and keep the credential (CLI: `--accept-fingerprint <sha256>`).
- Settings → Hubs: switch, remove (with confirmation), auto-connect; the current Hub is marked.
- Pairing via approval request, token renewal and Revoke.
- Library with search and filter.
- Hash-verified ROM cache with resumable download.
- Launch NDS games locally with melonDS DS: video, audio via Qt Multimedia, keyboard, touch via mouse.
- Save sync with the Hub: sync before launch, auto checkpoint while playing (12 s after the last change, at most every 60 s), final sync on pause, stop and exit; pending uploads are kept per Hub and user.
- Conflict dialog with "Keep both, decide later" as default; per-game badge Synced / Sync pending / Conflict.
- CLI: `saves list`, `save push`, `save pull`, `save resolve`.
- Share a running game as a Session (Private / Hub users / Invite only with invites, Join/Decline); "Sessions on this Hub" in the library.
- Watch a Session over direct WebRTC (H.264 + Opus); watch-only mode without a running game.
- Multiview: side-by-side and PiP, one audible surface; the picker lists all Sessions; diagnostics tab with RTT (negotiated DataChannel `fb-diag`). Encoding runs on a worker thread; under load the oldest pending video frame is dropped.
- Windows: the FFmpeg build enables the NVENC, QSV and AMF H.264 encoders (selected at runtime, software H.264 as fallback). A Windows test checks they are compiled in; opening them needs a GPU and is verified only locally.
- CLI: `session-share --synthetic`, `session-watch`.
- Credentials in the Credential Manager on Windows, in memory only on Linux (new pairing after restart).
- Redeem an invite code to join a Hub as a new user (no password).
- Upload ROMs to the Hub from the Player when the Hub allows user uploads.
- Firmware path for NDS: in native mode the Player fetches your firmware files from the Hub and shows "Firmware required" / "Firmware missing" instead of launching without them.
- Emulation page: core options per global/system level (locked options are hidden).
- Controllers page: SDL3 gamepads, built-in and user profiles, remapping, input test.
- Settings page: Appearance (Dark / Light / System).
- Windows installer (Inno Setup), CI artifact `framebeam-player-windows-x64-setup`.

Details: [server/README.md](server/README.md), [client/README.md](client/README.md).

## How saves work

- The core writes its battery save to the Player's local save directory (melonDS DS: `<rom sha256>.sav`).
- Before launch the Player compares the local save with the Hub's current checkpoint: it downloads a newer Hub save, uploads a changed local one, or starts empty.
- While playing, changes are uploaded as checkpoints; on pause, stop and exit a final upload follows. Every upload names the Hub revision it is based on.
- If the Hub moved on in the meantime, the Hub never overwrites: it keeps the upload in history and opens a conflict. You decide in the Player dialog or on the Hub page "Saves": use the Hub version or adopt the local save, or decide later (the game starts with the local save, uploads for that game pause).
- Without a connection the game starts with the local save and the upload stays pending for the same Hub and user. Saves are never sent to another Hub.
- Locations: Player `<data dir>/hubs/<hub_id>/users/<user_id>/saves/<game_id>/`; Hub `<data dir>/saves/`.

## Build and run

Prerequisites: Go 1.24 (per `server/go.mod`); for the client CMake, a C++ compiler and Qt >= 6.4 (not via vcpkg): on Linux via apt (package list `QT_PKGS` in `.claude/hooks/session-start.sh`), on Windows Qt 6.8 with the modules `qtmultimedia` and `qtwebsockets`. Sessions additionally need libdatachannel, FFmpeg and Opus: on Linux the media apt packages from the same list plus `make fetch-deps` (builds pinned libdatachannel via `scripts/fetch-libdatachannel.sh`); on Windows via vcpkg (`client/vcpkg.json`). Gamepads need SDL3 (>= 3.2): on Linux `make fetch-sdl3` (pinned 3.2.30 source build, also run by `make check-client`), on Windows via vcpkg.

```sh
make check          # Hub and client check, quiet
make check-hub      # Hub only
make build-hub      # server/dist/framebeam-hub-linux-{amd64,arm64}
make generate       # Go codegen from OpenAPI
```

Start the Hub (details: [server/README.md](server/README.md)):

```sh
framebeam-hub setup-admin -username <name>   # password as a single line from stdin
framebeam-hub -data-dir <directory>        # HTTPS, default listen :8443
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP instead of HTTPS
framebeam-hub renew-cert -data-dir <directory>   # renew the self-generated certificate now (refused with your own cert/key)
# systemd install: sudo packaging/linux/install-hub.sh renew-cert (instead of calling the binary)
```

The data directory (`-data-dir`, default `/var/lib/framebeam`) contains the database and certificate. Further flags: `-listen`, `-name`, `-tls-cert`, `-tls-key`, `-ice-servers` (comma-separated `stun:` URLs, default none); each also available via `FRAMEBEAM_*`.

### Run the Hub as a service (Linux / Raspberry Pi)

```sh
sudo packaging/linux/install-hub.sh install --binary framebeam-hub-linux-arm64 --port 8444 --admin <name>
sudo packaging/linux/install-hub.sh upgrade --binary framebeam-hub-linux-arm64
```

Installs a systemd service (`framebeam-hub`); `--port 8444` avoids a clash when 8443 is taken. Details: [packaging/linux/README.md](packaging/linux/README.md).

Player (details: [client/README.md](client/README.md)):

```sh
scripts/bootstrap-vcpkg.sh                 # once
make fetch-core                            # build melonDS DS (pinned), prints the .so path
make check-client                          # preset via CLIENT_PRESET, default linux-debug; without a core, core tests are skipped
client/build/linux-debug/app/framebeam_player [--data-dir <path>] [--dev-allow-http]
```

Player data storage (ROM cache, `profiles.json`, `device.json`, `hubs/<id>/users/<user_id>/saves/<game_id>/`, `system/`): portable by default in `<directory of the executable>/data`. If that is not writable (e.g. Program Files), it falls back to AppData (`QStandardPaths::AppDataLocation`). On the first portable start, existing AppData data is copied once (nothing is moved, deleted or overwritten; the ROM cache is re-downloaded). Credentials stay in the OS credential store. `--data-dir` or `FRAMEBEAM_DATA_DIR` take precedence.

`--data-dir` replaces the default; `--dev-allow-http` allows HTTP Hubs outside localhost (development only). `scripts/e2e-player-hub.sh` checks the Player CLI against a locally built Hub, including a save round trip.

Windows test package: unpack the CI artifact `framebeam-player-windows-x64` from the Windows job and start `framebeam_player.exe` (core under `cores/`).

### Test Sessions locally

Two Players on the LAN, both paired to the same Hub. With only the admin, both devices belong to the admin: Private (own devices only), Hub users and Invite only can be tested. With a second Hub user (see below) Private rejects the foreign user and Hub users lets them in. STUN (`-ice-servers`) is not needed on a LAN. `scripts/e2e-session.sh` runs the same flow headless with two CLI processes against a local Hub.

### Try the phase 5 features

1. Hub, page Users: create an invite (the code is shown only once). In a second Player choose to redeem an invite code and enter it.
2. Start a Session on the first Player with Private and with Hub users; check that the second user is rejected, then let in.
3. Hub, Settings: enable "Allow users to upload games"; upload a homebrew ROM from the second Player.
4. Hub, Systems & Cores: switch NDS to native firmware mode without files; the Player shows "Firmware required" / "Firmware missing".
5. Upload your own BIOS/firmware dumps (`bios7.bin`, `bios9.bin`, `firmware.bin`; never in the repo) and launch.
6. Connect a gamepad and remap it on Controllers.
7. Install the Player via the `framebeam-player-windows-x64-setup` artifact.

Keyboard: Arrows, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter=Start, Backspace=Select, Esc=Pause.

## Repository structure

- `server/`: FrameBeam Hub (Go)
- `client/`: FrameBeam Player (C++/Qt)
- `protocol/`: OpenAPI and WSS schemas, shared by Hub and Player
- `docs/`: architecture, ADRs, design, workflow
- `scripts/`: check and bootstrap scripts
- `packaging/`: packaging

## Documentation

- [Architecture (index)](docs/architecture/README.md)
- [ADR 0001: Stack additions](docs/adr/0001-stack-additions.md) (accepted)
- [ADR 0002: Protocol and Hub in phase 1](docs/adr/0002-protocol-and-hub-phase1.md) (accepted)
- [ADR 0003: Player in phase 2](docs/adr/0003-player-phase2.md) (accepted)
- [ADR 0004: Portable data directory of the Player](docs/adr/0004-player-portable-data.md) (accepted)
- [ADR 0005: Saves in phase 3](docs/adr/0005-saves-phase3.md) (accepted)
- [ADR 0006: Sessions in phase 4](docs/adr/0006-sessions-phase4.md) (accepted)
- [ADR 0007: Phase 5, users, firmware, settings pages, gamepads, installer](docs/adr/0007-phase5.md) (accepted)
- [ADR 0009: Finish the PoC (0.1.1)](docs/adr/0009-finish-poc.md) (proposed)
- [Roadmap after the PoC](docs/roadmap.md)
- [Design](docs/design/README.md)
- [Working with Claude Code](docs/workflow.md)
