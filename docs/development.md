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
make check-hub        # Hub only: gofmt, vet, staticcheck, tests, codegen freshness, Hub third-party notices, packaging checks
make check-client     # Player only (preset via CLIENT_PRESET, default linux-debug)
make build-hub        # server/dist/framebeam-hub-linux-{amd64,arm64} and framebeam-hub-windows-amd64.exe (HUB_VERSION, HUB_CHANNEL, HUB_COMMIT)
make package-hub-deb HUB_VERSION=...   # Hub .deb for amd64/arm64 in server/dist/ (after build-hub)
make generate         # regenerate Go code from OpenAPI (oapi-codegen)
make notices          # regenerate server/THIRD-PARTY-NOTICES.txt (needed after Go dependency bumps; make check-hub fails when stale)
make fetch-core       # build melonDS DS (pinned) and print the .so path
make fetch-desmume    # download DeSmuME from the libretro buildbot (nightly, CRC32-checked) and print the .so path
make fetch-deps       # build libdatachannel (pinned) and print the prefix
make fetch-sdl3       # build SDL3 (pinned) and print the prefix
```

`make check-client` runs the dependency scripts itself and passes their results to CMake (`-DFRAMEBEAM_MELONDS_DS_CORE=...`, `-DCMAKE_PREFIX_PATH=...`, `-DSDL3_ROOT=...`); `CMAKE_BUILD_PARALLEL_LEVEL` defaults to 3. Without a core, tests with `NEEDS_CORE` are skipped. Tests run headless (`QT_QPA_PLATFORM=offscreen`) against a fake Hub and a homebrew test ROM generated at build time. Run the Player after a build: `client/build/linux-debug/app/framebeam_player [--data-dir <path>] [--dev-allow-http]`.

## Dependency scripts

| Script | What it does |
|---|---|
| `scripts/bootstrap-vcpkg.sh` | vcpkg (pinned) into `$HOME/.cache/framebeam/vcpkg` |
| `scripts/fetch-buildbot-core.sh <core>` / `.ps1` | Downloads a core from the libretro buildbot nightly (`FRAMEBEAM_BUILDBOT_URL` overrides the base URL), checks the CRC32 against `.index-extended`, extracts one library into `$HOME/.cache/framebeam/cores/buildbot/`, prints the path. Not pinned on purpose: CI tests against the current nightly ([ADR 0020](adr/0020-cores-from-the-libretro-buildbot.md) D10). `FRAMEBEAM_REQUIRE_CORES=1` (CI) makes a failed fetch or a skipped core test fail the check |
| `scripts/fetch-melonds-ds.sh` / `.ps1` | Builds the melonDS DS core (pin: `scripts/melonds-ds.pin`) into `$HOME/.cache/framebeam/cores/`, idempotent, prints the path. No core in the repository |
| `scripts/fetch-libdatachannel.sh` | Builds libdatachannel (pin: `scripts/libdatachannel.pin`, media on, no own WebSocket) into `$HOME/.cache/framebeam/deps/libdatachannel/`, idempotent, prints the prefix (mandatory for the Player build) |
| `scripts/fetch-sdl3.sh` | Builds SDL3 (pin: `scripts/sdl3.pin`, gamepad only, no video/audio) into `$HOME/.cache/framebeam/deps/sdl3/`, idempotent, prints the prefix. Windows: vcpkg port `sdl3` |
| `scripts/check.sh <step>` | The quiet check steps behind the make targets |
| `scripts/check-trusted-keys.sh`, `scripts/check-core-signing-key.sh` | Hub and Player embed the same release key; part of `make check-hub` |

Windows release builds use the `x64-windows-release` overlay triplet (Release-only dependency builds); the debug preset uses the standard `x64-windows` triplet.

## End-to-end and UI tests

- `scripts/e2e-player-hub.sh`: Player CLI against a locally built Hub, including a save round trip and a core round trip (dummy library zipped in the buildbot layout, `import-cores`, `fetch-core`).
- `scripts/e2e-session.sh`: two CLI processes share and watch a Session against a local Hub; a second round forces the relay against a Hub with TURN on loopback.
- `scripts/e2e-hub-update.sh`: real install on a systemd host (`install-hub.sh`, `.deb` migration, signed update round trip); skips without root/systemd.
- All three run in the Linux CI job.
- Screenshot tests (`ctest -R screenshots`) render all Player screens at 1440x900 into the directory named by `FRAMEBEAM_SCREENSHOT_DIR`.
- OpenGL tests need a GL 3.3 context (Linux CI uses Mesa llvmpipe under xvfb) and are skipped without one (ADR 0013). `FRAMEBEAM_DISABLE_HW_RENDER=1` forces the cores' software path.
- Windows test package: unpack the CI artifact `framebeam-player-windows-x64` from the Windows job and start `framebeam_player.exe` (it ships no core; the Hub provides it on first game start).

## CI and releases

- `.github/workflows/ci.yml`: Linux on every push; Windows on PRs against `main`, manually, and on pushes to `main` (primes the Windows vcpkg binary cache after merges, [ADR 0008](adr/0008-windows-ci-cache.md); cache misses still need a cold dependency build). The Windows job builds the layout launcher plus `bin\`, the zip and the installer and tests silent install and upgrade.
- Every workflow job has `timeout-minutes` (Windows client and core builds 300, so a cold vcpkg cache still fits).
- Linux apt steps go through `scripts/ci-apt-install.sh` (update limited to 120 s and install to 240 s per attempt, apt retries and network timeouts, 3 attempts) plus a 20 minute step `timeout-minutes`, so a hanging Ubuntu mirror cannot block CI for long.
- Job `hub-windows` (windows-latest): `go vet`, `go test` and a version check of the Windows Hub binary; artifact `framebeam-hub-windows`. The release adds `framebeam-hub-<version>-windows-amd64.exe` to the index as platform `windows-amd64`, kind `binary`.
- Changes that touch only `docs/*` or `*.md` files are detected as docs-only on branches and PRs; the Hub, Player and Windows jobs are skipped for them.
- `.github/workflows/release.yml`: after a successful CI run on `main` it publishes the CI artifacts as GitHub prerelease `vX.Y.Z` (newest 5 beta prereleases kept, promoted releases never pruned) and adds Hub and Player to the signed `updates-index` release under channel `beta` (`framebeam-sign release-add`, secret `FRAMEBEAM_SIGNING_KEY`). The per-product index entries are stored as `index-hub.json` / `index-player.json` assets on the release. Pushing a `v*` tag builds nothing.
- `.github/workflows/promote.yml` ("Promote to release", manual): see [guides/updates.md](guides/updates.md).
- `.github/workflows/cores.yml` was deleted in 0.8 ([ADR 0020](adr/0020-cores-from-the-libretro-buildbot.md) D10): cores are no longer built or signed by FrameBeam; the existing `cores-index` and `core-*` releases stay for older Hubs. CI fetches DeSmuME from the libretro buildbot for the core tests (`scripts/fetch-buildbot-core.sh|ps1`, base URL override `FRAMEBEAM_BUILDBOT_URL`) and sets `FRAMEBEAM_REQUIRE_CORES=1`, so a skipped core test or a failed fetch fails the check. Tooling: `server/cmd/framebeam-sign` (keygen / add / sign / verify / pubkey / release-add) now serves the updates index only. Offline Hub: `framebeam-hub import-cores <dir>` or `install-hub.sh import-cores <dir>`.
- The SessionStart hook `.claude/hooks/session-start.sh` only prepares cloud sessions (vcpkg, Go modules, Qt apt packages); the core build runs only via `make fetch-core`.

## Versions and channels

- The root file `VERSION` (`X.Y.Z`) is shared by Hub and Player and is the floor for the next beta ([ADR 0016](adr/0016-release-numbering-and-promotion.md)). CI computes version and channel once: a push to `main` gives the plain `X.Y.Z` on channel `beta` (the larger of `VERSION` and the highest existing tag `vX.Y.*` plus 1; the version job creates the tag on the commit before building, so a failed or re-run build can leave gaps); anything else `X.Y.Z-dev.<run>`. Main CI runs do not cancel each other.
- They are passed to `make build-hub` (`HUB_VERSION`, `HUB_CHANNEL`, `HUB_COMMIT`) and to the Player configure (`FRAMEBEAM_VERSION`, `FRAMEBEAM_CHANNEL`, `FRAMEBEAM_COMMIT`, also read by `make check-client` from the environment).
- Only a milestone PR that starts a new minor line raises `VERSION` (to `X.(Y+1).0`, for example 0.8.0); otherwise nobody touches it. Stable releases are promoted betas and keep the beta number.
- `framebeam-hub version [--json]` and `framebeam_player --version-json` print `{"product", "version", "channel", "commit", "protocol_version", "min_protocol_version"}`.
- `protocol_version` is separate from product versions ([protocol reference](reference/protocol.md)).

## Hub package and installer builds

- `make package-hub-deb`: via `packaging/linux/build-deb.sh --binary PATH --arch amd64|arm64 --version X.Y.Z[-pre] --out DIR` (dpkg-deb only; units and maintainer scripts in `packaging/linux/deb/`). `scripts/check.sh packaging` (part of `make check-hub`) checks control, contents, units, runs shellcheck (the packaging scripts fully, all `scripts/*.sh` and the SessionStart hook at warning level) and a smoke test of `install-hub.sh` with `FRAMEBEAM_INSTALL_ROOT`.
- Windows installer (Inno Setup, per-user), built by ISCC in the Windows CI job: [guides/packaging.md](guides/packaging.md).

## Documentation checks

`scripts/check-doc-links.py [repo-root]` (stdlib only) checks relative links and heading anchors in all Markdown files and exits 1 on broken ones. It is not part of `make check` or CI.

## Contributions

Contributions require agreeing to the CLA; see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Hard rules

No ROMs, BIOS or firmware in the repository or in tests (homebrew ROMs and dummy files only); never overwrite saves silently or send them to another Hub; no secrets in files, profiles or logs.
