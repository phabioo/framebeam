# Development

Build, test, dependencies, CI and versioning. Agent workflow and briefs: [workflow.md](workflow.md). Agent-facing summary of commands: root `CLAUDE.md`.

## Prerequisites

- **Hub:** Go >= 1.25 (per `server/go.mod`; CI uses the pinned toolchain go1.26.8).
- **Player:** CMake >= 3.25, Ninja, a C++20 compiler and Qt >= 6.4 (not via vcpkg). Linux: apt (package list `QT_PKGS` in `.claude/hooks/session-start.sh`). Windows: Qt 6.8 LTS via install-qt-action with the modules `qtmultimedia` and `qtwebsockets`.
- **Sessions** additionally need libdatachannel, FFmpeg and Opus (and OpenSSL for the update signature check): on Linux the media apt packages from the same list plus `make fetch-deps`; on Windows via vcpkg (`client/vcpkg.json`).
- **Gamepads** need SDL3 >= 3.2: on Linux `make fetch-sdl3`; on Windows via vcpkg.
- vcpkg (pinned) is bootstrapped once with `scripts/bootstrap-vcpkg.sh` into `$HOME/.cache/framebeam/vcpkg`; it is a prerequisite for the Player build.

## Make targets

```sh
make check            # Hub and Player checks, quiet (errors + summary only)
make check-hub        # Hub only: gofmt, vet, staticcheck, tests, codegen freshness, packaging checks
make check-client     # Player only (preset via CLIENT_PRESET, default linux-debug)
make build-hub        # server/dist/framebeam-hub-linux-{amd64,arm64} (HUB_VERSION, HUB_CHANNEL, HUB_COMMIT)
make package-hub-deb HUB_VERSION=...   # Hub .deb for amd64/arm64 in server/dist/ (after build-hub)
make generate         # regenerate Go code from OpenAPI (oapi-codegen)
make fetch-core       # build melonDS DS (pinned) and print the .so path
make fetch-deps       # build libdatachannel (pinned) and print the prefix
make fetch-sdl3       # build SDL3 (pinned) and print the prefix
```

`make check-client` runs the dependency scripts itself and passes their results to CMake (`-DFRAMEBEAM_MELONDS_DS_CORE=...`, `-DCMAKE_PREFIX_PATH=...`, `-DSDL3_ROOT=...`); `CMAKE_BUILD_PARALLEL_LEVEL` defaults to 3. Without a core, tests with `NEEDS_CORE` are skipped. Tests run headless (`QT_QPA_PLATFORM=offscreen`) against a fake Hub and a homebrew test ROM generated at build time. Run the Player after a build: `client/build/linux-debug/app/framebeam_player [--data-dir <path>] [--dev-allow-http]`.

## Dependency scripts

| Script | What it does |
|---|---|
| `scripts/bootstrap-vcpkg.sh` | vcpkg (pinned) into `$HOME/.cache/framebeam/vcpkg` |
| `scripts/fetch-melonds-ds.sh` / `.ps1` | Builds the melonDS DS core (pin: `scripts/melonds-ds.pin`) into `$HOME/.cache/framebeam/cores/`, idempotent, prints the path. No core in the repository |
| `scripts/fetch-libdatachannel.sh` | Builds libdatachannel (pin: `scripts/libdatachannel.pin`, media on, no own WebSocket) into `$HOME/.cache/framebeam/deps/libdatachannel/`, idempotent, prints the prefix (mandatory for the Player build) |
| `scripts/fetch-sdl3.sh` | Builds SDL3 (pin: `scripts/sdl3.pin`, gamepad only, no video/audio) into `$HOME/.cache/framebeam/deps/sdl3/`, idempotent, prints the prefix. Windows: vcpkg port `sdl3` |
| `scripts/check.sh <step>` | The quiet check steps behind the make targets |
| `scripts/check-trusted-keys.sh`, `scripts/check-core-signing-key.sh` | Hub and Player embed the same release key; part of `make check-hub` |

Windows release builds use the `x64-windows-release` overlay triplet (Release-only dependency builds); the debug preset uses the standard `x64-windows` triplet.

## End-to-end and UI tests

- `scripts/e2e-player-hub.sh`: Player CLI against a locally built Hub, including a save round trip and a core round trip (throwaway signing key, dummy library, `fetch-core`).
- `scripts/e2e-session.sh`: two CLI processes share and watch a Session against a local Hub; a second round forces the relay against a Hub with TURN on loopback.
- `scripts/e2e-hub-update.sh`: real install on a systemd host (`install-hub.sh`, `.deb` migration, signed update round trip); skips without root/systemd.
- All three run in the Linux CI job.
- Screenshot tests (`ctest -R screenshots`) render all Player screens at 1440x900 into the directory named by `FRAMEBEAM_SCREENSHOT_DIR`.
- OpenGL tests need a GL 3.3 context (Linux CI uses Mesa llvmpipe under xvfb) and are skipped without one (ADR 0013). `FRAMEBEAM_DISABLE_HW_RENDER=1` forces the cores' software path.
- Windows test package: unpack the CI artifact `framebeam-player-windows-x64` from the Windows job and start `framebeam_player.exe` (it ships no core; the Hub provides it on first game start).

## CI and releases

- `.github/workflows/ci.yml`: Linux on every push; Windows on PRs against `main`, manually, and on pushes to `main` and `v*` tags (primes the Windows vcpkg binary cache after merges, [ADR 0008](adr/0008-windows-ci-cache.md); cache misses still need a cold dependency build). The Windows job builds the layout launcher plus `bin\`, the zip and the installer and tests silent install and upgrade.
- Changes that touch only `docs/*` or `*.md` files are detected as docs-only on branches and PRs; the Hub, Player and Windows jobs are skipped for them.
- `.github/workflows/release.yml`: after a successful CI run on `main` or a `v*` tag it publishes the CI artifacts as GitHub (pre)release (beta prereleases `v<X.Y.Z-beta.N>`, newest 5 kept) and adds Hub and Player to the signed `updates-index` release (`framebeam-sign release-add`, secret `FRAMEBEAM_SIGNING_KEY`).
- `.github/workflows/promote.yml` ("Promote to stable", manual): see [guides/updates.md](guides/updates.md).
- `.github/workflows/cores.yml`: builds and publishes signed core packages and the `cores-index` release (needs secret `FRAMEBEAM_SIGNING_KEY`) ([ADR 0010](adr/0010-cores-from-the-hub.md)). Tooling: `server/cmd/framebeam-sign` (keygen / add / sign / verify / pubkey / release-add). Offline Hub: `framebeam-hub import-cores <dir>` or `install-hub.sh import-cores <dir>`.
- The SessionStart hook `.claude/hooks/session-start.sh` only prepares cloud sessions (vcpkg, Go modules, Qt apt packages); the core build runs only via `make fetch-core`.

## Versions and channels

- The root file `VERSION` (`X.Y.Z`) is shared by Hub and Player. CI computes version and channel once: tag `vX.Y.Z` gives stable; a push to `main` gives `X.Y.Z-beta.<run>` on channel `beta`; anything else `-dev.<run>`. A tag different from `VERSION` fails CI.
- They are passed to `make build-hub` (`HUB_VERSION`, `HUB_CHANNEL`, `HUB_COMMIT`) and to the Player configure (`FRAMEBEAM_VERSION`, `FRAMEBEAM_CHANNEL`, `FRAMEBEAM_COMMIT`, also read by `make check-client` from the environment).
- A milestone PR raises `VERSION` to its own version (for example 0.6.0) when it starts the next milestone without a stable promotion in between; otherwise betas keep the old number.
- `framebeam-hub version [--json]` and `framebeam_player --version-json` print `{"product", "version", "channel", "commit", "protocol_version", "min_protocol_version"}`.
- `protocol_version` is separate from product versions ([protocol reference](reference/protocol.md)).

## Hub package and installer builds

- `make package-hub-deb`: via `packaging/linux/build-deb.sh --binary PATH --arch amd64|arm64 --version X.Y.Z[-pre] --out DIR` (dpkg-deb only; units and maintainer scripts in `packaging/linux/deb/`). `scripts/check.sh packaging` (part of `make check-hub`) checks control, contents, units, runs shellcheck and a smoke test of `install-hub.sh` with `FRAMEBEAM_INSTALL_ROOT`.
- Windows installer (Inno Setup, per-user), built by ISCC in the Windows CI job: [guides/packaging.md](guides/packaging.md).

## Hard rules

No ROMs, BIOS or firmware in the repository or in tests (homebrew ROMs and dummy files only); never overwrite saves silently or send them to another Hub; no secrets in files, profiles or logs.
