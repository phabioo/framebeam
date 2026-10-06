# AGENTS.md

This file provides guidance to coding agents (Claude Code, OpenAI Codex) working in this repository.

FrameBeam is a self-hosted retro gaming platform (monorepo): a central ROM library and versioned saves, local emulation, and sessions between Players over WebRTC. Motto: **"The Hub manages. The Player emulates. Audio and video flow directly between Players wherever possible."**

## Terminology

- **FrameBeam Hub** (`server/`, Go) and **FrameBeam Player** (`client/`, C++/Qt).
- **Session** = a running/shared game session; "Stream" only in diagnostics.

## Working rules

- Repository language is English: code, comments, UI, docs, commit messages and PRs.
- Briefs for subagents follow the template in `docs/workflow.md` (which also holds the token-saving rules, phase plan and cloud limits).
- One work package per task/thread, one topic per PR.

## Hard rules

- No ROMs, BIOS or firmware in the repo or in tests; only homebrew ROMs or dummy files.
- Never overwrite saves silently, never send them to another Hub.
- Never put secrets in files, profiles or logs.

## Status and commands

- `scripts/bootstrap-vcpkg.sh`: vcpkg (pinned) into `$HOME/.cache/framebeam/vcpkg`; prerequisite for the client build.
- Client prerequisite Qt >= 6.4 (not vcpkg): Linux via apt (packages see `.claude/hooks/session-start.sh`, `QT_PKGS`), Windows CI via install-qt-action (6.8 LTS).
- `scripts/fetch-melonds-ds.sh` (`make fetch-core`): builds melonDS DS (pin in `scripts/melonds-ds.pin`) into `$HOME/.cache/framebeam/cores/`, idempotent, prints the .so path; `make check-client` passes it as `-DFRAMEBEAM_MELONDS_DS_CORE=...` (if it is missing, core tests are skipped). Windows: `scripts/fetch-melonds-ds.ps1`.
- `scripts/fetch-libdatachannel.sh` (`make fetch-deps`): builds libdatachannel (pin in `scripts/libdatachannel.pin`, media on, no own WebSocket) into `$HOME/.cache/framebeam/deps/libdatachannel/`, idempotent, prints the prefix; `make check-client` runs it and passes `-DCMAKE_PREFIX_PATH=...` (mandatory; `CMAKE_BUILD_PARALLEL_LEVEL`, default 3). FFmpeg/Opus/Qt WebSockets via apt on Linux, vcpkg on Windows (`client/vcpkg.json`).
- `scripts/fetch-sdl3.sh` (`make fetch-sdl3`): builds SDL3 (pin in `scripts/sdl3.pin`, gamepad only, no video/audio) into `$HOME/.cache/framebeam/deps/sdl3/`, idempotent, prints the prefix; `make check-client` runs it and passes `-DSDL3_ROOT=...`. Windows: vcpkg port `sdl3`.
- `scripts/setup-cloud.sh`: setup script for Codex/other non-Claude cloud environments; runs `.claude/hooks/session-start.sh` with `FRAMEBEAM_CLOUD_SETUP=1`, then prebuilds libdatachannel, SDL3 and the melonDS DS core so `make check-client` works offline.
- `packaging/windows/framebeam-player.iss`: Windows installer (Inno Setup, per-user), built by ISCC in the Windows CI job (artifact `framebeam-player-windows-x64-setup`).
- `make check`: Hub and client checks, quiet; `make check-hub` / `make check-client` individually (preset via `CLIENT_PRESET`, default `linux-debug`).
- `make build-hub`: Hub binaries `server/dist/framebeam-hub-linux-{amd64,arm64}` (`HUB_VERSION` can be set).
- `make generate`: regenerate Go code (oapi-codegen).
- `scripts/e2e-player-hub.sh` (Player CLI against a locally built Hub incl. save round trip) and `scripts/e2e-session.sh` (two CLI processes share/watch a Session against a local Hub).
- `packaging/linux/install-hub.sh` (+ `framebeam-hub.service`, README there): installs/upgrades/uninstalls the Hub as a systemd service from a local binary (`--port 8444` if 8443 is taken); `scripts/check.sh packaging` (bash -n, shellcheck, unit verify, install smoke test with `FRAMEBEAM_INSTALL_ROOT`) is part of `make check-hub`.

CI (`.github/workflows/ci.yml`): Linux on every push, Windows on PRs against `main` and manually. The SessionStart hook `.claude/hooks/session-start.sh` (also the base of `scripts/setup-cloud.sh`) only prepares cloud sessions (vcpkg, Go modules, Qt apt packages; the core build runs only via `make fetch-core`).

## Pointers

- Architecture: `docs/architecture/README.md` (index, read only the file you need); deviations: `docs/adr/`. Area rules live in `client/AGENTS.md`, `server/AGENTS.md` and `protocol/AGENTS.md`.
- Design (screens, tokens): `docs/design/README.md`.

## Codex

- Roles (subagents) are defined in `.codex/agents/`. The orchestrator may make small, coherent changes itself; subagents are for bounded or parallel packages.
- Subagents never commit; the orchestrator commits and opens the PR.
- The current plan and status is `docs/roadmap.md`; mark finished items there in the PR that finishes them.
- Cloud setup script: `scripts/setup-cloud.sh`.
