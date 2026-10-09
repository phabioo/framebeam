# Downloads a libretro core from the nightly buildbot (ADR 0020 D10, CI test dependency) and prints the DLL path.
#   scripts/fetch-buildbot-core.ps1 <core>      e.g. desmume  ->  desmume_libretro.dll
# Source: $env:FRAMEBEAM_BUILDBOT_URL (default https://buildbot.libretro.com/nightly)/windows/x86_64/latest/<core>_libretro.dll.zip
# CRC32 of the extracted library (not of the zip) is verified against ".index-extended" of the same directory; exactly one library is extracted to
# <cache>\cores\buildbot\<core>\<date>-<crc>\ (cache = FRAMEBEAM_CACHE_DIR or $env:LOCALAPPDATA\framebeam).
# No pin on purpose: CI tests against the current nightly; date and CRC are logged. Idempotent (same date+crc = reuse),
# retries with backoff, timeouts. http:// is accepted only for 127.0.0.1 (tests).
# Env: FRAMEBEAM_FETCH_RETRIES (default 4), FRAMEBEAM_FETCH_BACKOFF seconds (default 2, doubled per attempt).
param([Parameter(Mandatory = $true, Position = 0)][string]$Core)
$ErrorActionPreference = 'Stop'
if ($Core -notmatch '^[a-z0-9_]+$') { throw "usage: fetch-buildbot-core.ps1 <core>  (lower-case core name, e.g. desmume)" }

$base = if ($env:FRAMEBEAM_BUILDBOT_URL) { $env:FRAMEBEAM_BUILDBOT_URL } else { 'https://buildbot.libretro.com/nightly' }
$base = $base.TrimEnd('/')
if ($base -notmatch '^https://' -and $base -notmatch '^http://127\.0\.0\.1(:\d+)?(/|$)') {
  throw "buildbot core: refusing URL '$base' (https required; http only for 127.0.0.1)"
}
$dirUrl = "$base/windows/x86_64/latest"
$lib = "${Core}_libretro.dll"
$zipName = "$lib.zip"
$retries = if ($env:FRAMEBEAM_FETCH_RETRIES) { [int]$env:FRAMEBEAM_FETCH_RETRIES } else { 4 }
$backoff = if ($env:FRAMEBEAM_FETCH_BACKOFF) { [int]$env:FRAMEBEAM_FETCH_BACKOFF } else { 2 }
$cache = if ($env:FRAMEBEAM_CACHE_DIR) { $env:FRAMEBEAM_CACHE_DIR } else { Join-Path $env:LOCALAPPDATA 'framebeam' }

if (-not ('FbCrc32' -as [type])) {
  Add-Type -TypeDefinition @'
public static class FbCrc32 {
  public static string OfFile(string path) {
    uint[] t = new uint[256];
    for (uint i = 0; i < 256; i++) { uint c = i; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
    uint crc = 0xFFFFFFFFu;
    using (var s = System.IO.File.OpenRead(path)) {
      byte[] buf = new byte[65536]; int n;
      while ((n = s.Read(buf, 0, buf.Length)) > 0) for (int i = 0; i < n; i++) crc = t[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return (~crc).ToString("x8");
  }
}
'@
}

function Invoke-Retry([string]$What, [scriptblock]$Action) {
  $wait = $backoff
  for ($n = 1; $n -le $retries; $n++) {
    try { & $Action; return } catch {
      if ($n -ge $retries) { throw "buildbot core: $What failed after $n attempts: $($_.Exception.Message)" }
      Write-Host "buildbot core: $What failed (attempt $n/$retries): $($_.Exception.Message); retrying in ${wait}s"
      Start-Sleep -Seconds $wait; $wait *= 2
    }
  }
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("fbcore-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
  $indexFile = Join-Path $tmp 'index'
  Invoke-Retry 'download of .index-extended' {
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 120 -Headers @{ 'User-Agent' = 'framebeam-ci' } -Uri "$dirUrl/.index-extended" -OutFile $indexFile
  }
  # Line format: "<date> [<time>] <crc32 hex> [<size>] <file name>" (3 fields: crc is field 2, else field 3).
  $entry = $null
  foreach ($line in Get-Content $indexFile) {
    $f = $line -split '\s+'
    if ($f.Count -ge 3 -and $f[-1] -eq $zipName) { $entry = $f; break }
  }
  $crcField = if ($entry) { if ($entry.Count -eq 3) { $entry[1] } else { $entry[2] } } else { '' }
  if (-not $entry -or $entry[0] -notmatch '^\d{4}-\d{2}-\d{2}$' -or $crcField -notmatch '^[0-9a-fA-F]{8}$') {
    throw "buildbot core: no valid entry for $zipName in $dirUrl/.index-extended"
  }
  $date = $entry[0]; $crc = $crcField.ToLower()
  Write-Host "buildbot core: $Core nightly date=$date crc32=$crc"

  $out = Join-Path $cache "cores/buildbot/$Core/$date-$crc"
  $dll = Join-Path $out $lib
  if (Test-Path $dll) { Write-Host "buildbot core: cached $dll"; Write-Output $dll; exit 0 }

  $zip = Join-Path $tmp $zipName
  $stage = Join-Path $tmp $lib
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  # The index CRC32 is over the uncompressed library (like RetroArch's core updater), not over the zip.
  Invoke-Retry "download/verification of $zipName" {
    Invoke-WebRequest -UseBasicParsing -TimeoutSec 300 -Headers @{ 'User-Agent' = 'framebeam-ci' } -Uri "$dirUrl/$zipName" -OutFile $zip
    $arc = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
      $names = @($arc.Entries | ForEach-Object { $_.FullName })
      if ($names.Count -ne 1 -or $names[0] -ne $lib) { throw "unexpected zip content (expected only ${lib}): $($names -join ', ')" }
      [IO.Compression.ZipFileExtensions]::ExtractToFile($arc.Entries[0], $stage, $true)
    } finally { $arc.Dispose() }
    if ((Get-Item $stage).Length -eq 0) { throw "extracted $lib is empty" }
    $actual = [FbCrc32]::OfFile($stage)
    if ($actual -ne $crc) { Remove-Item $stage -Force; throw "CRC32 mismatch for ${lib}: index says $crc, extracted library is $actual" }
  }
  New-Item -ItemType Directory -Force $out | Out-Null
  Move-Item -Force $stage $dll
  Write-Host "buildbot core: installed $dll"
  Write-Output $dll
} finally {
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
