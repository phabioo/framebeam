# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

FrameBeam is a self-hosted retro gaming platform (monorepo): a central ROM library and versioned saves, local emulation, and sessions between Players over WebRTC. Motto: **"The Hub manages. The Player emulates. Audio and video flow directly between Players wherever possible."**

## Terminology

- **FrameBeam Hub** (`server/`, Go) and **FrameBeam Player** (`client/`, C++/Qt).
- **Session** = a running/shared game session; "Stream" only in diagnostics.

## Working rules

- Repository language is English: code, comments, UI, docs, commit messages and PRs.
- Opus only orchestrates and writes no product code; implementation is done by the Sonnet 5.5 agents in `.claude/agents/`, searching and log reading by the Haiku 5.5 agent `scout` (alias `haiku`).
- Briefs follow the template in `docs/workflow.md` (which also holds the token-saving rules, phase plan and cloud limits).
- Opus verifies via `git diff --stat`, a targeted diff and the test result; commit/PR by Opus. One work package per thread, one topic per PR.

## Hard rules

- No ROMs, BIOS or firmware in the repo or in tests; only homebrew ROMs or dummy files.
- Never overwrite saves silently, never send them to another Hub.
- Never put secrets in files, profiles or logs.

## Status and commands

Details, flags and the full script/CI description: `docs/development.md` (build, deps, CI, versions) and `docs/guides/`.

- `make check` (Hub and Player, quiet), `make check-hub`, `make check-client` (preset via `CLIENT_PRESET`, default `linux-debug`), `make build-hub` (`HUB_VERSION`/`HUB_CHANNEL`/`HUB_COMMIT`), `make package-hub-deb HUB_VERSION=...`, `make generate` (oapi-codegen), `make notices` (regenerates `server/THIRD-PARTY-NOTICES.txt`; `make check-hub` fails when it is stale after Go dependency changes).
- Dependency scripts (pins in `scripts/*.pin`, idempotent, prefix under `$HOME/.cache/framebeam/`): `scripts/bootstrap-vcpkg.sh` (prerequisite of the Player build), `make fetch-core` (melonDS DS; without it core tests are skipped), `make fetch-deps` (libdatachannel, mandatory for the Player), `make fetch-sdl3`. `make check-client` runs them and passes the results to CMake. Qt >= 6.4 is not vcpkg (Linux apt packages: `QT_PKGS` in `.claude/hooks/session-start.sh`; Windows CI: install-qt-action 6.8 LTS).
- E2E: `scripts/e2e-player-hub.sh`, `scripts/e2e-session.sh`, `scripts/e2e-hub-update.sh` (Linux CI job; the last skips without root/systemd).
- CI: `.github/workflows/ci.yml` (docs-only changes skip the build jobs; Linux on every push; Windows on PRs against `main`, manually and on pushes to `main`, ADR 0008), `release.yml` (publishes the CI artifacts of each `main` build as beta prerelease and adds to the signed `updates-index`), `promote.yml` (manual "Promote to release"), `cores.yml` (signed core packages and `cores-index`, ADR 0010). Signing secret `FRAMEBEAM_SIGNING_KEY`; tool `server/cmd/framebeam-sign`.
- Versions (ADR 0016): every push to main builds a beta with a plain `X.Y.Z` (prerelease tag `vX.Y.Z`, channel `beta`); PR/branch builds are `-dev.<run>`. `VERSION` is only the floor: CI uses max(`VERSION`, highest tag `vX.Y.*` + 1) and reserves the tag before building. Only a milestone PR that starts a new minor line raises `VERSION` (to `X.(Y+1).0`). "Promote to release" adds an existing beta to the `stable` index channel without rebuild; a `v*` tag push builds nothing.
- Hub flags and env: `docs/guides/hub-configuration.md`. Player: `FRAMEBEAM_FORCE_RELAY=1` / CLI `--force-relay` (ADR 0012), `FRAMEBEAM_DISABLE_HW_RENDER=1` forces the cores' software path (ADR 0013; GL tests need a GL 3.3 context, Linux CI uses Mesa llvmpipe under xvfb).
- Packaging: `packaging/linux/` (Hub `.deb` via `build-deb.sh`, `install-hub.sh`; checked by `scripts/check.sh packaging`, part of `make check-hub`, which also runs `scripts/check-trusted-keys.sh`: Hub and Player embed the same release key), `packaging/windows/framebeam-player.iss` (Inno Setup, built in the Windows CI job).
- Internet Sessions (ADR 0012): Hub flags `-turn`, `-public-host`, `-turn-port`, `-turn-relay-ports`, `-turn-relay-ip`; save retention flags `-save-keep-recent|daily|weekly`.

The SessionStart hook `.claude/hooks/session-start.sh` only prepares cloud sessions (vcpkg, Go modules, Qt apt packages; the core build runs only via `make fetch-core`).

## Pointers

- Architecture: `docs/architecture/README.md` (index, read only the file you need); deviations: `docs/adr/`. Area rules: `server/`, `client/`, `protocol/` each have a `CLAUDE.md`.
- Design (screens, tokens): `docs/design/README.md`.
- Plan and status after the PoC: `docs/roadmap.md`; mark finished items there in the PR that finishes them. Docs index: `docs/README.md`; READMEs stay short, detail belongs in `docs/`.
