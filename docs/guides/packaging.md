# Packaging

Release artifacts and how they are built (PoC platforms: [07-poc-scope.md](../architecture/07-poc-scope.md); other platforms are planned, see [08-repo-and-open-points.md](../architecture/08-repo-and-open-points.md)): the Windows Player installer (below), the Hub `.deb` (ships `/usr/share/doc/framebeam-hub/copyright` and `THIRD-PARTY-NOTICES.txt`; the raw Hub binaries on a release come with `LICENSE` and `THIRD-PARTY-NOTICES-hub.txt`; regenerate with `make notices`) and the systemd script ([hub-install.md](hub-install.md)), and the macOS placeholder `packaging/macos/` (empty, later; see [roadmap](../roadmap.md) 0.12).

## Windows installer

Windows installer of the FrameBeam Player: `framebeam-player.iss` (Inno Setup 6.3+, decision D8). Unsigned in the PoC.

- Built by the Windows CI job (`.github/workflows/ci.yml`, step "Build installer (Inno Setup)") from the assembled package `dist\framebeam-player`, after the package smoke check. Artifact: `framebeam-player-windows-x64-setup` (`framebeam-player-setup.exe`). ISCC comes preinstalled on `windows-latest`; the step falls back to `choco install innosetup`.
- Defines passed to ISCC: `/DAppVersion=<version>` (required), `/DVersionInfoVersion=<x.y.z.n>`, `/DPackageDir=<dir>`, `/DOutputDir=<dir>`.
- Per-user install by default (no admin rights) to `%LOCALAPPDATA%\Programs\FrameBeam Player`, so the portable `<exe dir>\data` stays writable ([ADR 0004](../adr/0004-player-portable-data.md)). The privileges dialog allows an all-users install; under Program Files the Player uses its AppData fallback.
- Start menu shortcut, optional desktop shortcut. The package contains no emulator cores (the Player downloads them from the Hub). The notice text `THIRD-PARTY-NOTICE.txt` is installed and shown before installation.
- The uninstaller keeps `data\` (ROM cache, profiles, saves); it removes it only after the prompt "Also remove my data?" is answered with Yes. Silent uninstalls always keep it.
- Local build on Windows (after assembling the package as in CI): `ISCC.exe /DAppVersion=0.0.0-dev packaging\windows\framebeam-player.iss`.

### Layout, upgrades and updates (0.3)

- Package layout: the launcher `framebeam_player.exe`, `THIRD-PARTY-NOTICE.txt` (source `packaging/windows/THIRD-PARTY-NOTICE.txt`), `LICENSE.txt` (GPL-3.0-or-later, shown by the installer) and `licenses\` (vcpkg port copyrights as `<port>.txt` plus `Qt-LGPL-3.0.txt` from `packaging/windows/licenses/`) at the top, everything else (real
  `framebeam_player.exe`, DLLs, Qt plugins, qml) in `bin\`; data stays in `data\` at the top (ADR 0004). CI also
  publishes the portable zip `framebeam-player-<version>-windows-x64.zip` and the installer
  `framebeam-player-<version>-windows-x64-setup.exe` (artifacts `framebeam-player-windows-x64[-setup]`).
- Upgrading over an old flat install deletes the old top-level DLLs and Qt directories (`[InstallDelete]`), never `data\`.
  The CI job installs silently, plants an old layout plus `data\marker`, reinstalls and checks the result.
- `/UPDATE` (used by the Player's updater, together with `/SILENT /SUPPRESSMSGBOXES /NORESTART`): waits up to 60 s for the
  mutex `FrameBeamPlayer`, relaunches the Player after a silent install as the original user. Shortcuts carry the
  AppUserModelID `FrameBeam.Player`.
- Extra ISCC define: `/DOutputBaseName=<name>` (default `framebeam-player-setup`).

