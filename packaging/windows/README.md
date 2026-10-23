# packaging/windows

The Windows installer is one MSI, `uzume.wxs` (WiX v5, product "Uzume", features Player (the app) and Hub (the server), scope per user or per machine, three options in the UI: Uzume, Uzume with a server on this PC, Only the server) (the former Inno shell is gone, ADR 0021). License material in the package: `THIRD-PARTY-NOTICE.txt` (checked in here), `LICENSE.txt` and `licenses\` (`licenses/Qt-LGPL-3.0.txt` here plus the vcpkg port copyrights added by CI); `msi-marker.txt` is the content of the install markers (`uzume-app.msi-installed`, `uzume-server.msi-installed`).

Installer bitmaps: `bitmaps/dialog.bmp` (WixUIDialogBmp 493 x 312) and `bitmaps/banner.bmp` (WixUIBannerBmp 493 x 58), generated with the app icon by `python3 -I scripts/make-icons.py` (design 8l-0).

Build (Windows only: the WiX binder needs msi.dll; .NET SDK 6+ required, WiX 5.0.2 and its extensions are installed by the script), after assembling the package and building the server as in CI (the package must contain `bin\uzume-server.exe`, a copy of the server exe):

```sh
pwsh packaging\windows\build-msi.ps1 -ProductVersion 0.0.0.1 -PackageDir dist\uzume-app -HubExe server\dist\uzume-server-windows-amd64.exe -OutFile dist\msi\uzume.msi
```

Properties, scope, services, folders, markers and silent defaults: [docs/guides/packaging.md](../../docs/guides/packaging.md).
