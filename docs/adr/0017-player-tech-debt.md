# ADR 0017: Player tech debt (0.7.x)

- Status: Accepted (Fabio, via the 0.7.x package request, 2026-10-08)
- Date: 2026-10-08
- Deciders: Fabio

## Context

Three Player items collected during 0.7.x need a recorded decision: the core version rule (ADR 0007 made every mismatch a warning only), the split of session visibility settings over two files, and the size of `PlayerController`.

## Decisions

### D1 Core version conflict

- The Player compares the version of the core it would run with the version the Hub expects for the system, using semver.
- Different MAJOR version: the launch is blocked with an error.
- Same major, other minor or patch: warning, the launch continues.
- Empty expected version on the Hub means any version: no check.
- A version that cannot be parsed (either side) only warns.
- A core chosen explicitly (environment variable or explicit path) is never blocked, only warned.
- The Hub handshake is unchanged: `core_version_mismatch` stays a warning and `compatible` stays `true`, so a Player is never locked out of the Hub. The block happens at launch only.
- Rationale: libretro cores bump the major version on save-state or SRAM format changes and on API breaks; minor and patch releases are usually safe. Versions are the `library_version` the core reports.

### D2 Session visibility in `player.json`

- Session visibility moves from the separate `player-settings.json` into `settings/player.json` (`PlayerSettings`, key `session_visibility`).
- A one-time migration takes over the value of the legacy file and deletes the legacy file after a successful save.

### D3 PlayerController split

See implementation notes.

## Rejected

- Keep the warning-only rule: a major core change can corrupt or reject saves and states, and the user only sees a warning.
- Block on any version difference: minor and patch updates of a core would stop working without a Hub registry update.
- Set `compatible=false` in the handshake on a major mismatch: the Player would be locked out of the Hub for a problem that only affects one system.

## Consequences

- Launching with a core of another major version fails with a clear error until the core or the Hub's expected version is changed.
- Developers can still use any core through the explicit override, with a warning.
- Hub behaviour, API and OpenAPI are unchanged.
- Settings live in one file; no new key is needed beyond `session_visibility`.

## Supersedes in ADR 0007

- D3: "`compatible` stays true (warning, launch allowed)" for `core_version_mismatch` now applies to the handshake only; the launch rule is D1 here.
- The deviation "A core version mismatch is a warning and does not block launch": replaced by D1 here. The rule of `docs/architecture/05-emulation.md` (matching version) now holds for the major version.
