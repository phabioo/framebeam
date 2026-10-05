# Laedt den libretro-Core melonDS DS (gepinnter Tag, offizielles Windows-Release-Asset, SHA-256-geprueft)
# nach $env:LOCALAPPDATA\framebeam\cores\melondsds\<tag>\windows-x64\ (CI: FRAMEBEAM_CACHE_DIR) und gibt
# den Pfad der DLL aus. Idempotent. Grund fuer Asset statt Quellbau: deren Build nutzt MSYS2/MinGW, kein MSVC.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pin = @{}
Get-Content "$root/scripts/melonds-ds.pin" | Where-Object { $_ -match '^[A-Z_0-9]+=' } | ForEach-Object {
  $k, $v = $_ -split '=', 2; $pin[$k] = $v.Trim()
}
$cache = if ($env:FRAMEBEAM_CACHE_DIR) { $env:FRAMEBEAM_CACHE_DIR } else { Join-Path $env:LOCALAPPDATA 'framebeam' }
$out = Join-Path $cache "cores/melondsds/$($pin.MELONDS_DS_TAG)/windows-x64"
$dll = Join-Path $out 'melondsds_libretro.dll'
if (Test-Path $dll) { Write-Output $dll; exit 0 }

New-Item -ItemType Directory -Force $out | Out-Null
$zip = Join-Path $out $pin.MELONDS_DS_WIN_ASSET
$url = "https://github.com/JesseTG/melonds-ds/releases/download/$($pin.MELONDS_DS_TAG)/$($pin.MELONDS_DS_WIN_ASSET)"
Invoke-WebRequest -Uri $url -OutFile $zip
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if (-not $pin.MELONDS_DS_WIN_SHA256) {
  Remove-Item $zip
  throw "MELONDS_DS_WIN_SHA256 in scripts/melonds-ds.pin ist leer. Berechneter Hash von $($pin.MELONDS_DS_WIN_ASSET): $actual"
}
if ($actual -ne $pin.MELONDS_DS_WIN_SHA256.ToLower()) {
  Remove-Item $zip
  throw "SHA-256 stimmt nicht: erwartet $($pin.MELONDS_DS_WIN_SHA256), erhalten $actual"
}
$tmp = Join-Path $out 'unpack'
Expand-Archive $zip -DestinationPath $tmp -Force
$found = Get-ChildItem $tmp -Recurse -Filter 'melondsds_libretro.dll' | Select-Object -First 1
if (-not $found) { throw 'melondsds_libretro.dll nicht im Asset gefunden' }
Copy-Item $found.FullName $dll
Remove-Item $tmp, $zip -Recurse -Force
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/JesseTG/melonds-ds/$($pin.MELONDS_DS_COMMIT)/LICENSE" `
  -OutFile (Join-Path $out 'LICENSE-melonDS-DS.txt')
Write-Output $dll
