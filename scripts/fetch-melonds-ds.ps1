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
$headers = @{ 'User-Agent' = 'framebeam-ci'; 'Accept' = 'application/vnd.github+json' }
if ($env:GITHUB_TOKEN) { $headers['Authorization'] = "Bearer $($env:GITHUB_TOKEN)" }
$rel = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/JesseTG/melonds-ds/releases/tags/$($pin.MELONDS_DS_TAG)"
$names = @($rel.assets | ForEach-Object { $_.name })
$hits = @($rel.assets | Where-Object { $_.name -match $pin.MELONDS_DS_WIN_ASSET -or $_.name -eq $pin.MELONDS_DS_WIN_ASSET })
if ($hits.Count -gt 1) {
  $rel_hits = @($hits | Where-Object { $_.name -match 'Release' -and $_.name -notmatch 'RelWithDebInfo|Debug' })
  if ($rel_hits.Count -ge 1) { $hits = $rel_hits }
}
if ($hits.Count -ne 1) {
  throw "Windows-Asset nicht eindeutig ($($hits.Count) Treffer fuer '$($pin.MELONDS_DS_WIN_ASSET)'). Assets von $($pin.MELONDS_DS_TAG): $($names -join ', ')"
}
$asset = $hits[0]
Write-Host "Asset: $($asset.name)"
$zip = Join-Path $out $asset.name
Invoke-WebRequest -Headers @{ 'User-Agent' = 'framebeam-ci' } -Uri $asset.browser_download_url -OutFile $zip
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if (-not $pin.MELONDS_DS_WIN_SHA256) {
  Remove-Item $zip
  throw "MELONDS_DS_WIN_SHA256 in scripts/melonds-ds.pin ist leer. Asset $($asset.name), berechneter SHA-256: $actual"
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
