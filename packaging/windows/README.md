# packaging/windows

The Windows installer is one MSI, `framebeam.wxs` (WiX v5, product "FrameBeam", features Player and Hub, scope per user or per machine) (the former Inno shell is gone, ADR 0021). License material in the package: `THIRD-PARTY-NOTICE.txt` (checked in here), `LICENSE.txt` and `licenses\` (`licenses/Qt-LGPL-3.0.txt` here plus the vcpkg port copyrights added by CI); `msi-marker.txt` is the content of the install markers (`framebeam-player.msi-installed`, `framebeam-hub.msi-installed`).

Build (Windows only: the WiX binder needs msi.dll; .NET SDK 6+ required, WiX 5.0.2 and its extensions are installed by the script), after assembling the package and building the Hub as in CI:

```sh
pwsh packaging\windows\build-msi.ps1 -ProductVersion 0.0.0.1 -PackageDir dist\framebeam-player -HubExe server\dist\framebeam-hub-windows-amd64.exe -OutFile dist\msi\framebeam.msi
```

Properties, scope, services, folders, markers and silent defaults: [docs/guides/packaging.md](../../docs/guides/packaging.md).
