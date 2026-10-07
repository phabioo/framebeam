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
- `scripts/fetch-libdatachannel.sh` (`make fetch-deps`): builds libdatachannel (pin in `scripts/libdatachannel.pin`, media on, no own WebSocket) into `$HOME/.cache/framebeam/deps/libdatachannel/`, idempotent, prints the prefix; `make check-client` runs it and passes `-DCMAKE_PREFIX_PATH=...` (mandatory; `CMAKE_BUILD_PARALLEL_LEVEL`, default 3). FFmpeg/Opus/Qt WebSockets via apt on Linux, vcpkg on Windows (`client/vcpkg.json`).
- `scripts/fetch-sdl3.sh` (`make fetch-sdl3`): builds SDL3 (pin in `scripts/sdl3.pin`, gamepad only, no video/audio) into `$HOME/.cache/framebeam/deps/sdl3/`, idempotent, prints the prefix; `make check-client` runs it and passes `-DSDL3_ROOT=...`. Windows: vcpkg port `sdl3`.
- Cores (ADR 0010): `.github/workflows/cores.yml` builds/publishes signed core packages and the `cores-index` release (needs secret `FRAMEBEAM_SIGNING_KEY`); `server/cmd/framebeam-sign` (keygen/add/sign/verify/pubkey); offline Hub: `framebeam-hub import-cores <dir>` or `install-hub.sh import-cores <dir>`; `scripts/e2e-player-hub.sh` also runs a core round trip (throwaway key, dummy library, `fetch-core`).
- `packaging/windows/framebeam-player.iss`: Windows installer (Inno Setup, per-user), built by ISCC in the Windows CI job (artifact `framebeam-player-windows-x64-setup`).
- Versions/channels (0.3, spec in the roadmap): root file `VERSION`; CI computes version/channel once (tag `vX.Y.Z` -> stable, push to main -> `X.Y.Z-beta.<run>`, channel `beta`, else `-dev.<run>`) and passes them to `make build-hub` (`HUB_VERSION`/`HUB_CHANNEL`/`HUB_COMMIT`) and to the Player configure (`FRAMEBEAM_VERSION`/`FRAMEBEAM_CHANNEL`/`FRAMEBEAM_COMMIT`, also read by `make check-client` from the environment). A tag different from `VERSION` fails CI.
- `.github/workflows/promote.yml` ("Promote to stable", manual): tags the commit of a beta prerelease (default newest) as `vX.Y.Z`, dispatches CI on the tag (stable build, Release publishes) and opens a PR `release/bump-<next>` raising `VERSION` (needs repo setting "Allow GitHub Actions to create and approve pull requests" for the PR, otherwise a warning with the compare link).
- `make package-hub-deb HUB_VERSION=...` (after `make build-hub`): Hub `.deb` for amd64/arm64 in `server/dist/` via `packaging/linux/build-deb.sh` (dpkg-deb only; units, maintainer scripts in `packaging/linux/deb/`); `scripts/check.sh packaging` (part of `make check-hub`) checks control/contents/units/shellcheck and runs `scripts/check-trusted-keys.sh` (Hub and Player embed the same release key).
- `scripts/e2e-hub-update.sh`: real install on a systemd host (install-hub.sh, .deb migration, signed update round trip); runs in the Linux CI job, skips without root/systemd.
- `.github/workflows/release.yml`: after a successful CI run on main/`v*` tag it publishes the CI artifacts as GitHub (pre)release (beta prereleases `v<X.Y.Z-beta.N>`, newest 5 kept), adds hub and player to the signed `updates-index` release (`framebeam-sign release-add`, secret `FRAMEBEAM_SIGNING_KEY`). Windows CI also runs for `v*` tags; it builds the layout launcher + `bin\`, zip and installer and tests silent install/upgrade.
- Sessions over the internet (0.4, ADR 0012): Hub TURN flags `-turn`, `-public-host`, `-turn-port`, `-turn-relay-ports`, `-turn-relay-ip` (env `FRAMEBEAM_TURN*`, `FRAMEBEAM_PUBLIC_HOST`); Player `FRAMEBEAM_FORCE_RELAY=1` / `--force-relay`; `scripts/e2e-session.sh` runs a second round with forced relay against a Hub with TURN on loopback. Save retention flags `-save-keep-recent|daily|weekly`.
- `make check`: Hub and client checks, quiet; `make check-hub` / `make check-client` individually (preset via `CLIENT_PRESET`, default `linux-debug`).
- `make build-hub`: Hub binaries `server/dist/framebeam-hub-linux-{amd64,arm64}` (`HUB_VERSION` can be set).
- `make generate`: regenerate Go code (oapi-codegen).
- `scripts/e2e-player-hub.sh` (Player CLI against a locally built Hub incl. save round trip) and `scripts/e2e-session.sh` (two CLI processes share/watch a Session against a local Hub); both also run in the Linux CI job.
- `packaging/linux/install-hub.sh` (+ `framebeam-hub.service`, README there): installs/upgrades/uninstalls the Hub as a systemd service from a local binary (`--port 8444` if 8443 is taken); `scripts/check.sh packaging` (bash -n, shellcheck, unit verify, install smoke test with `FRAMEBEAM_INSTALL_ROOT`) is part of `make check-hub`.

CI (`.github/workflows/ci.yml`): Linux on every push; Windows on PRs against `main`, manually, and on pushes to `main` to prime the Windows vcpkg binary cache after merges (see ADR 0008). Cache misses still require a cold dependency build. The SessionStart hook `.claude/hooks/session-start.sh` only prepares cloud sessions (vcpkg, Go modules, Qt apt packages; the core build runs only via `make fetch-core`).

## Pointers

- Architecture: `docs/architecture/README.md` (index, read only the file you need); deviations: `docs/adr/`. Area rules: `server/`, `client/`, `protocol/` each have a `CLAUDE.md`.
- Design (screens, tokens): `docs/design/README.md`.
- Plan and status after the PoC: `docs/roadmap.md`; mark finished items there in the PR that finishes them.
