# packaging/windows

Windows installer of the FrameBeam Player: `framebeam-player.iss` (Inno Setup 6.3+, decision D8). Unsigned in the PoC.

- Built by the Windows CI job (`.github/workflows/ci.yml`, step "Build installer (Inno Setup)") from the assembled package `dist\framebeam-player`, after the package smoke check. Artifact: `framebeam-player-windows-x64-setup` (`framebeam-player-setup.exe`). ISCC comes preinstalled on `windows-latest`; the step falls back to `choco install innosetup`.
- Defines passed to ISCC: `/DAppVersion=<version>` (required), `/DVersionInfoVersion=<x.y.z.n>`, `/DPackageDir=<dir>`, `/DOutputDir=<dir>`.
- Per-user install by default (no admin rights) to `%LOCALAPPDATA%\Programs\FrameBeam Player`, so the portable `<exe dir>\data` stays writable ([ADR 0004](../../docs/adr/0004-player-portable-data.md)). The privileges dialog allows an all-users install; under Program Files the Player uses its AppData fallback.
- Start menu shortcut, optional desktop shortcut. Licence and notice texts of the package (`LICENSE-melonDS-DS.txt`, `THIRD-PARTY-NOTICE.txt`) are installed and `THIRD-PARTY-NOTICE.txt` is shown before installation.
- The uninstaller keeps `data\` (ROM cache, profiles, saves); it removes it only after the prompt "Also remove my data?" is answered with Yes. Silent uninstalls always keep it.
- Local build on Windows (after assembling the package as in CI): `ISCC.exe /DAppVersion=0.0.0-dev packaging\windows\framebeam-player.iss`.
