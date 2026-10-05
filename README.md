# FrameBeam

FrameBeam is a self-hosted retro gaming platform. The **FrameBeam Hub** manages the central ROM library, versioned saves, users and devices. The **FrameBeam Player** emulates locally and shares running Sessions directly with other Players via WebRTC. The Hub never emulates, encodes or renders.

## Status

Phase plan: [Workflow](docs/workflow.md#phase-plan).

| Phase | Status | Scope |
|---|---|---|
| 0 Foundation | done | Monorepo, CLAUDE.md, architecture, CI (Linux/Windows), build scaffolding, check scripts |
| 1 Protocol and Hub basics | done | OpenAPI `/api/v1`, WSS schemas, Hub with SQLite, admin setup, TLS, pairing, tokens, library, ROM download, web interface |
| 2 Playable vertical slice | done | Player core (profile, pairing, library, ROM cache), melonDS DS via Libretro, minimal Qt UI |
| 3 Saves | planned | Sync, versions, conflict model |
| 4 Session sharing and multiview | planned | Presence, signaling, WebRTC, multiview |
| 5 Remainder and polish | planned | Firmware path, users, remaining pages, packaging |

## What works

**FrameBeam Hub** (`server/`)

- Admin setup via `setup-admin` and via `/setup` in the web interface (loopback only).
- HTTPS with a self-signed certificate (or your own certificate); the fingerprint is logged at startup.
- Web interface with login: Library, Clients, Settings.
- Pairing of new devices with Allow/Deny; issue and revoke tokens (Revoke).
- ROM upload in the web interface.
- ROM download via API with Range and ETag.
- Info endpoint `/.well-known/framebeam` and handshake with `protocol_version`.
- Ships with a systemd installer for Linux / Raspberry Pi (`packaging/linux/`).

**Protocol** (`protocol/`)

- OpenAPI 3.0.3 for `/api/v1` and WSS message schemas; `protocol_version` is 1.

**FrameBeam Player** (`client/`, [ADR 0003](docs/adr/0003-player-phase2.md))

- Connection screen with Hub profiles and auto-connect.
- Hub identification with fingerprint confirmation on first contact (TOFU); on mismatch the connection is blocked.
- Pairing via approval request, token renewal and Revoke.
- Library with search and filter.
- Hash-verified ROM cache with resumable download.
- Launch NDS games locally with melonDS DS: video, audio via Qt Multimedia, keyboard, touch via mouse.
- Credentials in the Credential Manager on Windows, in memory only on Linux (new pairing after restart).

Still missing: saves/sync (phase 3), Sessions (phase 4), gamepads, firmware path and settings pages (phase 5).

## Build and run

Prerequisites: Go 1.24 (per `server/go.mod`); for the client CMake, a C++ compiler and Qt >= 6.4 (not via vcpkg): on Linux via apt (package list `QT_PKGS` in `.claude/hooks/session-start.sh`), on Windows Qt 6.8.

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
```

The data directory (`-data-dir`, default `/var/lib/framebeam`) contains the database and certificate. Further flags: `-listen`, `-name`, `-tls-cert`, `-tls-key`; each also available via `FRAMEBEAM_*`.

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

Player data storage (ROM cache, `profiles.json`, `device.json`, `hubs/<id>/` with saves, `system/`): portable by default in `<directory of the executable>/data`. If that is not writable (e.g. Program Files), it falls back to AppData (`QStandardPaths::AppDataLocation`). On the first portable start, existing AppData data is copied once (nothing is moved, deleted or overwritten; the ROM cache is re-downloaded). Credentials stay in the OS credential store. `--data-dir` or `FRAMEBEAM_DATA_DIR` take precedence.

`--data-dir` replaces the default; `--dev-allow-http` allows HTTP Hubs outside localhost (development only). `scripts/e2e-player-hub.sh` checks the Player CLI against a locally built Hub.

Windows test package: unpack the CI artifact `framebeam-player-windows-x64` from the Windows job and start `framebeam_player.exe` (core under `cores/`).

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
- [ADR 0001: Stack additions](docs/adr/0001-stack-additions.md)
- [ADR 0002: Protocol and Hub in phase 1](docs/adr/0002-protocol-and-hub-phase1.md)
- [ADR 0003: Player in phase 2](docs/adr/0003-player-phase2.md) (accepted)
- [Design](docs/design/README.md)
- [Working with Claude Code](docs/workflow.md)
