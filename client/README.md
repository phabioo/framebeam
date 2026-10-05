# client – FrameBeam Player

C++/Qt Player: emulates locally, synchronizes saves, shares Sessions. Status of phase 2: Hub profile, pairing, library, ROM cache, emulation (melonDS DS) and minimal UI. Decisions: [ADR 0003](../docs/adr/0003-player-phase2.md) (accepted). Rules for agents: `CLAUDE.md`.

## Structure

- `core/`: profiles, ROM cache, credential store, version (`framebeam_core`)
- `network/`: Hub connection (HTTP/TLS with fingerprint pinning), library, ROM download
- `emulation/`: `EmulatorBackend`/`LibretroBackend`, system manifests (`manifests/`), core locator
- `ui/`: QML module `FrameBeam.Player` as static lib `framebeam_ui`
- `app/`: executable `framebeam_player`
- `media/`: `framebeam_media_deps` (libdatachannel, FFmpeg, Opus) and the `media_deps` smoke test; media path follows in phase 4

## Dependencies

- Qt >= 6.4 (Core, Network, Gui, Quick, QuickControls2, Multimedia, WebSockets, Test); not via vcpkg. Linux: apt, Windows: install-qt-action (see `.github/workflows/ci.yml`).
- CMake >= 3.25, Ninja; vcpkg (`scripts/bootstrap-vcpkg.sh`) for further packages.
- Media (phase 4, [ADR 0006](../docs/adr/0006-sessions-phase4.md)): Linux apt `libavcodec-dev libswscale-dev libopus-dev qt6-websockets-dev libssl-dev` plus libdatachannel from `scripts/fetch-libdatachannel.sh` (pinned, `scripts/libdatachannel.pin`); Windows vcpkg (`client/vcpkg.json`, platform windows).
- melonDS DS core (pinned, `scripts/melonds-ds.pin`): `scripts/fetch-melonds-ds.sh` (Linux) or `.ps1` (Windows). No core in the repository.

## Build and test

```sh
make check-client                      # preset via CLIENT_PRESET, default linux-debug
cmake -DFRAMEBEAM_MELONDS_DS_CORE=<path-to-core> ...   # enables the core tests
```

Without `FRAMEBEAM_MELONDS_DS_CORE` all targets build; tests with `NEEDS_CORE` are skipped by ctest. Tests run headless (`QT_QPA_PLATFORM=offscreen`) against a fake Hub and a homebrew test ROM generated at build time.

## Data storage

Portable by default: `<directory of the executable>/data` (ROM cache, `profiles.json`, `device.json`, `hubs/<id>/` incl. saves, `system/`, `probe/`), provided a real write test succeeds there. Otherwise it falls back to AppData (`QStandardPaths::AppDataLocation`) with a log note (`framebeam.profiles`). If the portable folder has no `profiles.json` yet, existing AppData content is copied once (the source remains unchanged, nothing is overwritten, `device_id` is preserved, the ROM cache is re-downloaded). Credentials stay in the OS credential store. `--data-dir` and `FRAMEBEAM_DATA_DIR` take precedence.
