# packaging/windows

`framebeam-player.iss` is the Inno Setup (6.3+) script of the FrameBeam Player installer: per-user install, launcher plus `bin\` layout, upgrades keep `data\`. License material in the package: `THIRD-PARTY-NOTICE.txt` (checked in here), `LICENSE.txt` and `licenses\` (`licenses/Qt-LGPL-3.0.txt` here plus the vcpkg port copyrights added by CI). The Windows CI job builds it with ISCC from the assembled package (artifact `framebeam-player-windows-x64-setup`).

Local build on Windows (after assembling the package as in CI):

```sh
ISCC.exe /DAppVersion=0.0.0-dev packaging\windows\framebeam-player.iss
```

Defines, install behaviour, layout, upgrade and update flags: [docs/guides/packaging.md](../../docs/guides/packaging.md).
