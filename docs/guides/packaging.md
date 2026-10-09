# Packaging

Release artifacts and how they are built (PoC platforms: [07-poc-scope.md](../architecture/07-poc-scope.md); other platforms are planned, see [08-repo-and-open-points.md](../architecture/08-repo-and-open-points.md)): the Windows MSI for Player and Hub (below), the Hub `.deb` (ships `/usr/share/doc/framebeam-hub/copyright` and `THIRD-PARTY-NOTICES.txt`; the raw Hub binaries on a release come with `LICENSE` and `THIRD-PARTY-NOTICES-hub.txt`; regenerate with `make notices`) and the systemd script ([hub-install.md](hub-install.md)), and the macOS placeholder `packaging/macos/` (empty, later; see [roadmap](../roadmap.md) 0.12).

## Windows installer

One MSI for Player, Hub or both: `packaging/windows/framebeam.wxs` (WiX v5, [ADR 0021](../adr/0021-one-windows-installer.md), proposed). It replaces the Inno Setup script `framebeam-player.iss` (ADR 0007 D8, removed). A small Inno "shell" `framebeam-setup-shell.iss` still exists, see below. Unsigned (SmartScreen warning).

- Built by the Windows CI job (`.github/workflows/ci.yml`) from the assembled package `dist\framebeam-player` and the Windows Hub binary. Artifacts: `framebeam-windows-x64-msi` (the MSI) and `framebeam-player-windows-x64-setup` (the shell `.exe`), plus the portable zip.
- Local build (Windows only: the WiX binder needs msi.dll; .NET SDK 6+; the script installs WiX 5.0.2 and its extensions): `pwsh packaging\windows\build-msi.ps1 -ProductVersion 0.0.0.1 -PackageDir dist\framebeam-player -HubExe server\dist\framebeam-hub-windows-amd64.exe -OutFile dist\msi\framebeam.msi`, then the shell: `ISCC.exe /DAppVersion=0.0.0-dev /DMsiFile=%CD%\dist\msi\framebeam.msi packaging\windows\framebeam-setup-shell.iss`. Details: [packaging/windows/README.md](../../packaging/windows/README.md).

### Features, scope and properties

- Features `Player` and `Hub`. A silent install without properties is Player only, per user, in `%LOCALAPPDATA%\Programs\FrameBeam Player` (portable `data\` stays writable, [ADR 0004](../adr/0004-player-portable-data.md)).
- `INSTALL_HUB=1` installs per machine (`ALLUSERS=1`): Player in `%ProgramFiles%\FrameBeam\Player`, Hub in `%ProgramFiles%\FrameBeam\Hub`. `INSTALL_PLAYER=0` with `INSTALL_HUB=1` is Hub only. Other properties: `HUB_PORT` (default 8443), `NETWORK_SHARING` (1 Hub only, 0 Player plus Hub), `DESKTOP_SHORTCUT`.
- Example: `msiexec /i framebeam.msi /qn ALLUSERS=1 INSTALL_HUB=1 HUB_PORT=8443 NETWORK_SHARING=1`.
- Hub: services `FrameBeamHub` (`NT SERVICE\FrameBeamHub`) and `FrameBeamHubUpdater` (LocalSystem), data `%ProgramData%\FrameBeam\Hub`, updater folder `%ProgramData%\FrameBeam\HubUpdater` (SYSTEM and Administrators only), two firewall rules for `framebeam-hub.exe`, registry `HKLM\Software\FrameBeam\Hub` (`Port`, `InstallDir`).
- Markers next to the executables: `framebeam-hub.msi-installed`, `framebeam-player.msi-installed` (self-update allowed). Package layout, notices and licenses as before: launcher `framebeam_player.exe`, `THIRD-PARTY-NOTICE.txt`, `LICENSE.txt`, `licenses\` at the top, the real Player, DLLs, plugins and qml in `bin\`, data in `data\`.
- Uninstall keeps `data\` (ROM cache, profiles, saves) and the Hub data in ProgramData.

### Migration and upgrades

- The shell `framebeam-player-<version>-windows-x64-setup.exe` (kind `installer`) only runs the MSI silently and registers nothing, so Players that only know Inno setups (`/UPDATE`, `/SILENT`) migrate normally.
- The MSI removes an Inno install (`{6F0C2B7E-3D1A-4C55-9B8E-F4A1D2C37A60}_is1`, HKCU and HKLM) silently before installing, keeping `data\`. A per-machine install also removes the per-user MSI product (`HKCU\Software\FrameBeam\Player\ProductCode`) asynchronously, after about 15 s. MajorUpgrade handles later versions.
- The CI job installs silently, plants an old layout plus `data\marker`, upgrades and checks the result. Known limitations: [ADR 0021](../adr/0021-one-windows-installer.md).
