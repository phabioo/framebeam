# ADR 0021: One Windows installer (0.9)

- Status: Proposed (decision pending Fabio's acceptance with the merge of the 0.9 PR)
- Date: 2026-10-09
- Decided by: Fabio (milestone plan of 2026-10-09); implementation proposal by the orchestrator
- Supersedes: the Inno Setup parts of [ADR 0007](0007-phase5.md) (installer script, D8 of the PoC) and [ADR 0011](0011-automatic-updates.md) (Player update by an Inno `setup.exe` with `/UPDATE`), see D9. The updater mechanics of ADR 0011 (signed index, size and SHA-256, root helper) stay.

## Context

The Player installer was an Inno Setup script. 0.9 adds the Hub on Windows (its own Windows service), "Set up a Hub on this PC" and a standalone mode like RetroArch. That needs one installer with a Player part and a Hub part, services, folder ACLs, firewall rules and an update path that can run as the machine administrator. Existing Inno installs and old Players (whose updater only knows Inno setups) must migrate without a bridge release.

## Decisions

### D1 WiX v5 MSI replaces Inno

- One MSI `packaging/windows/framebeam.wxs` (WiX v5, product "FrameBeam", fixed UpgradeCode, MajorUpgrade) with the features `Player` and `Hub`. Built by `packaging/windows/build-msi.ps1` (Windows only) and in the Windows CI job.
- Rejected: MSIX. It needs a paid, trusted signing certificate, virtualizes the install directory so the portable `<exe-dir>/data` cannot work, limits services, and its own update mechanism conflicts with the FrameBeam updater (roadmap 0.9).
- The Inno script stays only as a small "shell" (D5).

### D2 Scope and silent defaults

- Scope `perUserOrMachine`. A silent install without properties installs the Player only, per user, into `%LOCALAPPDATA%\Programs\FrameBeam Player` (same directory as the Inno install, so the portable `data\` of ADR 0004 stays writable).
- `INSTALL_HUB=1` (or ADDLOCAL with Hub) installs per machine: Player into `%ProgramFiles%\FrameBeam\Player`, Hub into `%ProgramFiles%\FrameBeam\Hub`. `INSTALL_PLAYER=0` with `INSTALL_HUB=1` installs the Hub only. Hub requires `ALLUSERS=1`.
- Properties: `HUB_PORT` (default 8443), `NETWORK_SHARING` (1 for Hub only, 0 for Player plus Hub), `DESKTOP_SHORTCUT`.
- Under Program Files the Player uses its AppData fallback for data (ADR 0004).

### D3 Hub as Windows services

- `FrameBeamHub`: account `NT SERVICE\FrameBeamHub` (virtual account), automatic start, arguments `-listen :<HUB_PORT> -network-sharing=<true|false>`, recovery action restart.
- `FrameBeamHubUpdater`: LocalSystem, automatic start, `framebeam-hub update watch`. It mirrors the Linux root helper of ADR 0011: the unprivileged Hub service stages the `msi` and writes a request file; the updater re-verifies the signed index and runs `msiexec /i <copy> /qn /norestart /l*v <log> ALLUSERS=1` from a private directory.
- Data and ACLs: `%ProgramData%\FrameBeam\Hub` (SYSTEM and Administrators full, `NT SERVICE\FrameBeamHub` modify) with `update-request\`; `%ProgramData%\FrameBeam\HubUpdater` (SYSTEM and Administrators only). ACLs are set by deferred custom actions (`icacls`), not PermissionEx.
- Firewall: two program-based rules for `framebeam-hub.exe` (TCP; UDP for TURN), so the Hub port and the TURN relay ports are covered. While network sharing is off the Hub binds only loopback, which the rules do not change.
- Markers (next to the executable): `framebeam-hub.msi-installed` (Hub self-update allowed) and `framebeam-player.msi-installed` (Player self-update allowed).
- Registry for the Player to find the local Hub: `HKLM\Software\FrameBeam\Hub` values `Port` (DWORD) and `InstallDir`. A per-user install writes `HKCU\Software\FrameBeam\Player\ProductCode`.
- Credentials stay in the Windows Credential Manager. The Linux Hub (.deb, Raspberry Pi) is unaffected.

### D4 Updates

- Player index kind `msi` (platform `windows-x64`) is preferred, `installer` is the fallback. Hub: platform `windows-amd64`, kind `msi`.
- Player self-update is allowed when `unins000.exe` or `framebeam-player.msi-installed` exists in the install root. Per-user: `msiexec /i <msi> /qn /norestart MSIINSTALLPERUSER=1 ALLUSERS=2`. Per machine: `ALLUSERS=1` (plus `INSTALL_HUB=1` when the Hub feature is installed), elevated.
- The Player copies its launcher to a temp directory and starts the copy with `--apply-msi-update <msi> <scope>`; the copy waits until no Player runs, runs msiexec, starts the installed Player and removes itself later. The Hub updater always passes `ALLUSERS=1`.

### D5 Migration from Inno without a bridge release

- The release keeps publishing a small Inno `framebeam-player-<version>-windows-x64-setup.exe` under kind `installer`. It only installs the MSI silently and registers nothing, so older Players update normally (their `/UPDATE` flags work).
- The MSI finds the Inno AppId `{6F0C2B7E-3D1A-4C55-9B8E-F4A1D2C37A60}` (`_is1` in HKCU and HKLM) and runs its uninstaller silently before files are installed. `data\` stays (silent Inno uninstalls always keep it).
- Switching a per-user Player to per machine (Player plus Hub): the per-machine install reads `HKCU\...\ProductCode` and removes the per-user product with a detached `msiexec /x <code> /qn` (impersonated, asynchronous), `data\` stays.
- Player data migration: under Program Files, on first start the Player copies the old portable data once from the first old portable source that exists (`%LOCALAPPDATA%\Programs\FrameBeam Player\data`, else `%ProgramFiles%\FrameBeam Player\data`) into its base directory. Never overwrite existing files, marker `.migrated-from-portable`, ROM cache skipped, like the existing migration.

### D6 Local setup API

- Protocol 1.10.0, handshake feature `local_setup_v1`: `GET /api/v1/local/status`, `POST /api/v1/local/setup` (first admin plus pairing of the calling Player, 409 when an admin exists), `POST /api/v1/local/pair` (existing enabled admin credentials, 401 otherwise), `PUT /api/v1/local/settings` (`network_sharing`, `import_dir`; admin device token; 400 `import_dir_unreadable`).
- Loopback only (other callers get 403), rate-limited like pairing. Admin credentials are required; nothing is created or paired without them.
- The Player trusts the Hub's certificate automatically only for `127.0.0.1` with the port registered in `HKLM\Software\FrameBeam\Hub`; anything else keeps the confirmed fingerprint flow of ADR 0009.
- "Set up a Hub on this PC" without a local Hub: the Player downloads the MSI of its own version (kind `msi`, size and SHA-256 checked), runs it elevated with `ALLUSERS=1 INSTALL_HUB=1 NETWORK_SHARING=0` and continues with `--setup-local-hub`.

### D7 Standalone mode

- Hub flag `-network-sharing=true|false` (env `FRAMEBEAM_NETWORK_SHARING`, default true, so Linux behaviour is unchanged) is only the initial value; afterwards the stored setting wins. Off = the Hub binds 127.0.0.1 and ::1 only and TURN is off. Changing it restarts the server in-process. The import folder is a stored setting too (`-library-import-dir` is the initial value).
- Both are editable on the Hub web Settings page and through `PUT /api/v1/local/settings`. A local ROM folder outside the Hub's reach is granted with `framebeam-hub grant-folder <path>` (Windows, admin: read and list for `NT SERVICE\FrameBeamHub`, inherited).
- A Player that wants cores directly from the internet uses this local Hub (ADR 0020 D1).

## Known limitations

- Removing the per-user product after a switch to per machine waits about 15 s (a nested msiexec fails with 1618 while the installation holds the Windows Installer mutex), so the old entry disappears shortly after the install finishes; its result is not checked.
- UAC with a different administrator account elevates under that account, so HKCU (the per-user `ProductCode`) and `%LOCALAPPDATA%` are the administrator's: the per-user product and the old portable data are not found and stay.
- Five wrong passwords on the local pairing endpoints lock local pairing for one minute.
- `FrameBeamHubUpdater` has `ServiceControl Stop="both"`, so an MSI upgrade stops the updater service that started that very msiexec. The installation itself runs in the Windows Installer service and goes on; but the updater process ends and does not wait for the result or copy the log to `<data>\updates\msiexec.log`. The new updater starts after the install. The success is visible by the Hub version after the restart. Verified only locally.
- The MSI and the installer shell are unsigned (SmartScreen warning). Installer, upgrades and services are verified only locally by Fabio; CI tests silent install and upgrade.

## Rejected

- MSIX (D1).
- A bridge release with a Player that updates to the MSI by itself: the Inno shell under kind `installer` does the same with no change in old Players.
- The Hub inside the Player process: the Hub must run without a logged-in user and with fewer rights.
- A Hub per user: a service needs a machine install.

## Consequences

- Windows CI builds the MSI and the shell and tests silent install, upgrade, markers and the Inno migration; the release publishes `msi`, `installer` and `zip`.
- `framebeam-player.iss` is gone; `framebeam-setup-shell.iss` stays until old Players are no longer supported (open: no date).
- Open: signing the MSI; a Hub-only installation UI beyond the MSI dialogs.
