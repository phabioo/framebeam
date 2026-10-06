# Working with Claude Code

## Hybrid mode

- Development happens in Claude Code cloud sessions.
- Windows build and tests run via GitHub Actions.
- Fabio tests milestones locally (real Windows, GPU, audio, gamepads, P2P between two machines, Raspberry Pi 5 as Hub) or via a Remote Control session on his machine.

## What can be verified in the cloud

Verifiable:
- Hub in full: Go server, SQLite, API, WSS signaling, pairing, tokens, TLS, save conflicts, firmware path, web interface; Linux builds x86-64 and ARM64 (cross-compile).
- Protocol: OpenAPI, JSON schemas, handshake, contract tests.
- Player core without GUI on Linux (Hub client, caches, save sync, Hub profiles, settings hierarchy, manifests).
- Libretro backend headless with melonDS DS and a homebrew ROM (frame hashes).
- WebRTC between two processes on one machine (loopback).
- QML under Xvfb with software rendering (Qt 6.4 from apt).
- Windows build and tests via GitHub Actions.

Not verifiable (only locally by Fabio):
- Windows Credential Manager, NVENC/QSV/AMF, installer, interactive use.
- GPU, audio, gamepads, game feel, latency.
- Real P2P (NAT, firewall, two devices on the LAN).
- Running on Raspberry Pi 5 (cloud: ARM64 build or QEMU smoke test only).
- Real ROMs/BIOS: must not enter the repository or the cloud.
- Containers are short-lived: dependencies need a setup script or caching.

## Roles

**Opus is exclusively the orchestrator** and writes no product code. It bundles tasks into sensible packages, delegates via brief (template below) to the Sonnet 5.5 agents in `.claude/agents/`, reviews the result and either accepts it or sends it back to the same agent with concrete corrections.

Flow:

1. Break the task into 2-3 sensible packages.
2. Write the brief (template below).
3. The subagent implements and reports in the report format; it does not commit.
4. Opus reviews via `git diff --stat`, a targeted diff and the test result.
5. Acceptance, or correction via `SendMessage` to the same agent.
6. Commit/PR by Opus.

| Agent | Model | Responsibility |
|---|---|---|
| `hub-implementer` | Sonnet | Go Hub (`server/`) |
| `player-implementer` | Sonnet | C++/Qt Player (`client/`) |
| `protocol-implementer` | Sonnet | `protocol/` |
| `build-ci-implementer` | Sonnet | CMake/vcpkg, Go build, GitHub Actions, `packaging/` |
| `docs-writer` | Sonnet | `docs/`, ADRs |
| `scout` | Haiku | read-only: search, read logs/CI output, summarize |

## Brief template

```text
Goal: <one sentence>
Context: <docs/architecture/NN-file.md, section X; optionally a short quote>
Files/paths: create/modify: <...>; touch nothing else
Acceptance criteria:
- <...>
Check command: <command, output trimmed>
Report: changed files; commands + result, one line each; open points.
Do not return full logs or file contents. Do not commit.
```

## Token-saving rules

- Do not split tasks too finely: prefer 2-3 sensible packages over many mini-assignments, since every agent starts cold.
- Agents do not explore freely; they get paths and the spec location.
- Read only the relevant architecture file (index: `docs/architecture/README.md`), never all of them.
- Opus reviews via `git diff --stat`, a targeted diff and the test result, not by re-reading whole files.
- Always filter/trim command output (`| tail -n 30`, errors only). Long logs are read and summarized by `scout`.
- For corrections, continue the agent via `SendMessage` instead of starting a new one; the context is preserved.
- One work package per thread; new threads instead of long histories.
- Since phase 0: quiet check scripts (`make check` or similar, errors + summary only), dependency cache in the SessionStart hook, codegen from OpenAPI.

## Packages and PRs

- One PR per package with green CI, one topic per PR.
- At every milestone (end of a phase), the phase PR updates and extends `README.md`: status, features, build/run.

## Phase plan

- **Phase 0 – Foundation:** monorepo structure, CLAUDE.md, architecture under `docs/`; CI (Linux, Windows, ARM64 cross-build of the Hub); SessionStart hook for dependencies.
- **Phase 1 – Protocol and Hub basics:** OpenAPI `/api/v1`, WSS messages, `protocol_version`, handshake, error codes; Hub with SQLite schema, admin setup, TLS, info endpoint, pairing, tokens, revoke, library, ROM upload/download.
- **Phase 2 – Playable vertical slice:** Player core (Hub profile, TOFU, pairing, library, ROM cache); `LibretroBackend` with melonDS DS; minimal Qt UI. First local Windows test by Fabio.
- **Phase 3 – Saves:** start/auto/final sync, current checkpoint vs. history, `base_version`, conflict model, pending sync; Hub saves page, Player conflict dialog.
- **Phase 4 – Session sharing and multiview:** presence, signaling, visibility/ACL, WebRTC with software H.264/Opus, PiP/side-by-side, diagnostics; afterwards hardware encoders (testable locally only).
- **Phase 5 – Remainder and polish:** firmware path, users/invites, systems & cores, emulation and controllers pages, SDL3 gamepads, dark/light, remaining Hub pages, packaging (Windows installer, systemd unit).
- Status (2026-10-06): phases 0-5 are done and merged; the PoC is complete. Hardware encoders (NVENC/QSV/AMF) are not in the shipped Windows FFmpeg build (`client/vcpkg.json` enables only `avcodec`, `swscale`, `openh264`); the Player probes them at runtime but always falls back to software H.264.
- **After the PoC – Distribution (planned, not PoC scope):** installers for Player and Hub on all target platforms (Windows, Linux, macOS; Hub additionally as Raspberry Pi/ARM64 package) and an integrated updater for Hub and Player, targeted for the first versions after the PoC. See `docs/architecture/08-repo-and-open-points.md`.

The mapping of screens to phases is in `docs/design/README.md`.

## Rules

- The repository language is English (code, UI, docs, commits, PRs).
- No real ROMs, BIOS or firmware in the repository or in tests; only homebrew ROMs or dummy files.
- melonDS DS is GPL-3.0; this affects the later distribution of the Player.
