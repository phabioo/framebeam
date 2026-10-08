# Updates and releases

How the Hub and the Windows Player update themselves, and how a stable release is made. Decisions: [ADR 0011](../adr/0011-automatic-updates.md). Mechanics (index format, install path, root helper): [hub-internals.md](../reference/hub-internals.md), [player-internals.md](../reference/player-internals.md), [update-index.md](../reference/update-index.md).

## Hub

- Checks the signed update index at startup, hourly on the beta channel and daily on stable. Admin Settings, section "Updates": channel and automatic install.
- Beta installs automatically (not during an active Session); stable only on request.
- The install runs through a root helper (systemd path unit) that re-verifies the signature.
- Before a schema migration the Hub backs up its database to `<data>/backups/` (newest 5).
- Needs internet access to github.com. Only the Debian package can update itself; other installs (including `install-hub.sh`) only show the available version.

## Player (Windows installer install)

- Same check (10 s after start, then hourly on beta, daily on stable). Settings, section "Updates": channel and automatic install.
- Stable shows a banner and installs after "Install and restart". Beta downloads in the background and applies at the next start, never during a game.
- Portable zip and dev builds only show availability. The Windows installer is unsigned (SmartScreen warning).
- The Windows Player is a windowless (GUI-subsystem) app; no console opens on start.

## Channels

- `stable` or `beta` (hint in the UI: "Pre-release builds from every change on main. May contain bugs."), switchable in the same Settings sections. `beta` also takes stable releases when newer. Never a downgrade.
- Default is the channel the build was made for: stable builds default to stable, beta builds to beta; `dev` builds do not check until a channel is chosen.
- Beta naming: pre-release builds are versioned `X.Y.Z-beta.<CI run number>`, channel `beta`, published as "beta build" prereleases `vX.Y.Z-beta.N`. Stored `test` settings are read as `beta`. Installs of the former `test.N` builds do not update to beta builds (different channel, and `beta` < `test` in SemVer); reinstall once from a beta or stable release.

## Make a stable release

GitHub Actions > "Promote to stable" > Run workflow (`.github/workflows/promote.yml`). Optional inputs: `beta_version` (default: newest beta) and `next_version` (default: next minor).

The workflow tags `vX.Y.Z` on the commit of that beta build, dispatches CI for the tag (stable build, release, signed index) and opens a PR bumping `VERSION` to the next version; if Actions may not create PRs it warns with a compare link. Merge that PR, otherwise beta builds sort below the release. Pushing the tag `vX.Y.Z` manually (it must equal `VERSION`) still works.

## Rollback

Manual (ADR 0011 D8): `sudo apt install --allow-downgrades ./framebeam-hub_<old>.deb` for the Hub, and if the schema changed, restore the matching database backup from `<data>/backups/`. The Player is reinstalled from an older release.
