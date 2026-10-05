# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

FrameBeam is a self-hosted retro gaming platform (monorepo): a central ROM library and versioned saves, local emulation, and sessions between Players over WebRTC. Motto: **"The Hub manages. The Player emulates. Audio and video flow directly between Players wherever possible."**

## Terminology

- **FrameBeam Hub** (`server/`, Go) and **FrameBeam Player** (`client/`, C++/Qt).
- **Session** = a running/shared game session; "Stream" only in diagnostics.

## Working rules

- Repository language is English: code, comments, UI, docs, commit messages and PRs.
- Opus only orchestrates and writes no product code; implementation is done by the Sonnet agents in `.claude/agents/`, searching and log reading by the Haiku agent `scout`.
- Briefs follow the template in `docs/workflow.md` (which also holds the token-saving rules, phase plan and cloud limits).
- Opus verifies via `git diff --stat`, a targeted diff and the test result; commit/PR by Opus. One work package per thread, one topic per PR.

## Hard rules

- No ROMs, BIOS or firmware in the repo or in tests; only homebrew ROMs or dummy files.
- Never overwrite saves silently, never send them to another Hub.
- Never put secrets in files, profiles or logs.

## Status and commands

- `scripts/bootstrap-vcpkg.sh`: vcpkg (pinned) into `$HOME/.cache/framebeam/vcpkg`; prerequisite for the client build.
- Client prerequisite Qt >= 6.4 (not vcpkg): Linux via apt (packages see `.claude/hooks/session-start.sh`, `QT_PKGS`), Windows CI via install-qt-action (6.8 LTS).
- `scripts/fetch-melonds-ds.sh` (`make fetch-core`): builds melonDS DS (pin in `scripts/melonds-ds.pin`) into `$HOME/.cache/framebeam/cores/`, idempotent, prints the .so path; `make check-client` passes it as `-DFRAMEBEAM_MELONDS_DS_CORE=...` (if it is missing, core tests are skipped). Windows: `scripts/fetch-melonds-ds.ps1`.
- `make check`: Hub and client checks, quiet; `make check-hub` / `make check-client` individually (preset via `CLIENT_PRESET`, default `linux-debug`).
- `make build-hub`: Hub binaries `server/dist/framebeam-hub-linux-{amd64,arm64}` (`HUB_VERSION` can be set).
- `make generate`: regenerate Go code (oapi-codegen).

CI (`.github/workflows/ci.yml`): Linux on every push, Windows on PRs against `main` and manually. The SessionStart hook `.claude/hooks/session-start.sh` only prepares cloud sessions (vcpkg, Go modules, Qt apt packages; the core build runs only via `make fetch-core`).

## Pointers

- Architecture: `docs/architecture/README.md` (index, read only the file you need); deviations: `docs/adr/`. Area rules: `server/`, `client/`, `protocol/` each have a `CLAUDE.md`.
- Design (screens, tokens): `docs/design/README.md`.
