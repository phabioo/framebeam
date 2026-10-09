# Updates and releases

How the Hub and the Windows Player update themselves, and how a beta is promoted to a release. Decisions: [ADR 0011](../adr/0011-automatic-updates.md). Mechanics (index format, install path, root helper): [hub-internals.md](../reference/hub-internals.md), [player-internals.md](../reference/player-internals.md), [update-index.md](../reference/update-index.md).

## Hub

- Checks the signed update index at startup, hourly on the beta channel and daily on stable. Admin Settings, section "Updates": channel and automatic install.
- Beta installs automatically (not during an active Session); stable only on request.
- The install runs through a root helper (systemd path unit) that re-verifies the signature.
- Before a schema migration the Hub backs up its database to `<data>/backups/` (newest 5).
- Needs internet access to github.com. Only the Debian package can update itself; other installs (including `install-hub.sh`) only show the available version.

### Hub on Windows

- The unprivileged service `FrameBeamHub` only downloads and stages the `msi` and writes a request file to `<data>\update-request`. It never installs.
- The service `FrameBeamHubUpdater` (LocalSystem) runs `framebeam-hub update watch` (polls every 5 s), re-verifies the signed index, copies the MSI to a private directory (`%ProgramData%\FrameBeam\HubUpdater`) and runs `msiexec /i <copy> /qn /norestart /l*v <log>`. The log is copied to `<data>\updates\msiexec.log`.
- "Packaged" means the marker file `framebeam-hub.msi-installed` next to the executable. The MSI that installs the services, ACLs and marker arrives in the next 0.9 package; the standalone `.exe` cannot update itself and only shows the available version.

## Player (Windows installer install)

- Same check (10 s after start, then hourly on beta, daily on stable). Settings, section "Updates": channel and automatic install.
- Stable shows a banner and installs after "Install and restart". Beta downloads in the background and applies at the next start, never during a game.
- Portable zip and dev builds only show availability. The Windows installer is unsigned (SmartScreen warning).
- The Windows Player is a windowless (GUI-subsystem) app; no console opens on start.

## Channels

- `stable` or `beta` (hint in the UI: "Pre-release builds from every change on main. May contain bugs."), switchable in the same Settings sections. `beta` also takes stable releases when newer. Never a downgrade.
- Release builds are compiled with channel `beta`. With no channel selected, the first successful index load stores `update_channel_default` (Hub: `settings` table; Player: `player.json`): `stable` if the index lists the running version as stable, else `beta`. It is never re-resolved, so a beta install stays on beta after its version is promoted, and a fresh install of a stable release follows stable. An explicit selection always wins; `dev` builds do not check until a channel is chosen. Automatic install defaults to on for beta, off for stable.
- Beta naming ([ADR 0016](../adr/0016-release-numbering-and-promotion.md)): every push to `main` is a beta with a plain version `X.Y.Z`, published as prerelease `vX.Y.Z`; the number can have gaps. Older `vX.Y.Z-beta.N` builds still update normally but cannot be promoted. Stored `test` settings are read as `beta`. Installs of the former `test.N` builds do not update to beta builds (different channel, and `beta` < `test` in SemVer); reinstall once from a beta or release.

## Promote a beta to a release

GitHub Actions > "Promote to release" > Run workflow (`.github/workflows/promote.yml`). Optional input `version` (default: newest beta prerelease).

The workflow does not rebuild and creates no tag or `VERSION` PR. It adds the beta's artifacts to the signed `updates-index` under channel `stable` (from the `index-hub.json` / `index-player.json` assets on the beta release) and turns the GitHub release from prerelease into the latest stable release. Stable numbers therefore skip (0.7.1, 0.8.3, 0.8.6). Betas built as `vX.Y.Z-beta.N` cannot be promoted; `v0.7.1` is the last stable made the old way. Promoted releases are never pruned.

## Rollback

Manual (ADR 0011 D8): `sudo apt install --allow-downgrades ./framebeam-hub_<old>.deb` for the Hub, and if the schema changed, restore the matching database backup from `<data>/backups/`. The Player is reinstalled from an older release.
