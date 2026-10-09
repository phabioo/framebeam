<#
  Builds the FrameBeam MSI (WiX v5, Windows only: the WiX binder needs msi.dll) from the assembled Player package and the Hub exe.
    pwsh packaging\windows\build-msi.ps1 -ProductVersion 0.9.0.123 -PackageDir dist\framebeam-player -HubExe dist\hub\framebeam-hub.exe -OutFile dist\msi\framebeam-0.9.0-windows-x64.msi
  -ProductVersion must be numeric a.b.c[.d] (MSI compares only the first three fields). Installs the pinned WiX dotnet tool and
  its extensions when missing. Assumes the .NET SDK (6+) is on PATH (preinstalled on the GitHub windows runners).
#>
param(
  [Parameter(Mandatory)] [string] $ProductVersion,
  [Parameter(Mandatory)] [string] $PackageDir,
  [Parameter(Mandatory)] [string] $HubExe,
  [Parameter(Mandatory)] [string] $OutFile
)
$ErrorActionPreference = 'Stop'
# Pinned: tool and extensions must be the same version.
$WixVersion = '5.0.2'
$Extensions = 'WixToolset.Util.wixext', 'WixToolset.Firewall.wixext', 'WixToolset.UI.wixext'

if ($ProductVersion -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw "ProductVersion must be numeric a.b.c[.d], got '$ProductVersion'" }
$here = $PSScriptRoot
$PackageDir = (Resolve-Path $PackageDir).Path
$HubExe = (Resolve-Path $HubExe).Path
foreach ($f in 'framebeam_player.exe', 'bin\framebeam_player.exe', 'LICENSE.txt', 'THIRD-PARTY-NOTICE.txt') {
  if (-not (Test-Path (Join-Path $PackageDir $f))) { throw "package is missing $f" }
}
if (Test-Path (Join-Path $PackageDir 'data')) { throw "the package must not contain data\" }

if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
  dotnet tool install --global wix --version $WixVersion
  if ($LASTEXITCODE -ne 0) { throw "dotnet tool install wix failed" }
  $env:PATH += ";$env:USERPROFILE\.dotnet\tools"
}
$have = (wix --version) -replace '\+.*$', ''
if ($have -ne $WixVersion) { throw "wix $have is installed, $WixVersion is pinned (dotnet tool update --global wix --version $WixVersion)" }
foreach ($e in $Extensions) {
  wix extension add --global "$e/$WixVersion"
  if ($LASTEXITCODE -ne 0) { throw "wix extension add $e failed" }
}

# Licence page (RTF): LICENSE.txt and the third-party notice, monospaced. Non-ASCII becomes \uN?.
function ConvertTo-RtfLines([string] $path) {
  foreach ($line in (Get-Content -LiteralPath $path -Encoding UTF8)) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $line.ToCharArray()) {
      $c = [int]$ch
      if ($ch -eq '\' -or $ch -eq '{' -or $ch -eq '}') { [void]$sb.Append('\').Append($ch) }
      elseif ($c -gt 127) { [void]$sb.Append("\u$([int16]$c)?") }
      elseif ($c -ge 32) { [void]$sb.Append($ch) }
    }
    $sb.Append('\par').ToString()
  }
}
$work = Join-Path ([IO.Path]::GetTempPath()) "framebeam-msi-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$rtf = Join-Path $work 'license.rtf'
$body = @('{\rtf1\ansi\deff0{\fonttbl{\f0\fmodern Consolas;}}\f0\fs17') +
  (ConvertTo-RtfLines (Join-Path $PackageDir 'LICENSE.txt')) + @('\par\page') +
  (ConvertTo-RtfLines (Join-Path $PackageDir 'THIRD-PARTY-NOTICE.txt')) + @('}')
[IO.File]::WriteAllLines($rtf, $body, [Text.Encoding]::ASCII)

New-Item -ItemType Directory -Force (Split-Path -Parent $OutFile) | Out-Null
wix build "$here\framebeam.wxs" -arch x64 -o $OutFile `
  -ext WixToolset.Util.wixext -ext WixToolset.Firewall.wixext -ext WixToolset.UI.wixext `
  -d "ProductVersion=$ProductVersion" -d "PackageDir=$PackageDir" -d "HubExe=$HubExe" -d "LicenseRtf=$rtf" `
  -d "IconFile=$here\..\..\client\app\icons\player.ico"
if ($LASTEXITCODE -ne 0) { throw "wix build failed" }
# ICE validation: errors fail the build. Deliberately suppressed:
#  ICE38/ICE64/ICE91  files (the Files harvest uses each file as key path) in the per-user profile of a per-user install.
#  ICE57              components PlayerShell/PlayerDesktopShortcut: shortcuts in Programs/Desktop plus an HKMU key path. In a
#                     perUserOrMachine package that is the intended form (HKMU follows the scope; an HKCU key path would be wrong for
#                     the per-machine case), the check cannot know the scope and reports a mix.
#  ICE17              the Print button of the stock WixUI LicenseAgreementDlg.
# ICE105 (per-user validation of a dual-purpose package) stays a warning: wix msi validate does not fail on warnings.
wix msi validate $OutFile -sice ICE38 -sice ICE64 -sice ICE91 -sice ICE57 -sice ICE17
if ($LASTEXITCODE -ne 0) { throw "MSI validation failed (see the ICE messages above)" }
Remove-Item -Recurse -Force $work
Get-Item $OutFile | Format-Table Name, Length
# The step must not inherit a native exit code from the last command.
$global:LASTEXITCODE = 0
