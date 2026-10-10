<#
  Install tests of the FrameBeam MSI and the Inno setup shell. Windows only, needs an elevated shell (CI runner). No ROMs or
  BIOS involved; only dummy files in data\.
    pwsh scripts/test-windows-msi.ps1 -Scenario user    -Msi dist\msi\framebeam-X-windows-x64.msi
    pwsh scripts/test-windows-msi.ps1 -Scenario machine -Msi ... -Version X.Y.Z
    pwsh scripts/test-windows-msi.ps1 -Scenario migrate -Msi ... -Setup dist\installer\framebeam-player-X-windows-x64-setup.exe
  user:    Player only, per user (defaults): files, markers, Start menu shortcut, --version, uninstall keeps data\.
  machine: per-user Player first, then ALLUSERS=1 INSTALL_HUB=1 NETWORK_SHARING=0 HUB_PORT=18443: per-user product removed, both
           services, local API on loopback only, ACLs, markers, registry, uninstall removes the services.
  migrate: newest released Inno setup (before the MSI existed) installed per user, then the shell setup over it with the old
           updater's flags: Inno gone, MSI product present, data\ kept. Skipped with a warning when no such release is left.
  GH_TOKEN and GH_REPO (or a git checkout of the repository) are needed for the migrate scenario (gh release ...).
#>
param(
  [Parameter(Mandatory)] [ValidateSet('user', 'machine', 'migrate')] [string] $Scenario,
  [Parameter(Mandatory)] [string] $Msi,
  [string] $Setup,
  [string] $Version,
  # machine: an older build of the same MSI (other product code), installed per user first to test the switch to per machine.
  [string] $OldMsi,
  # machine: another build with the same version (new product code) = an upgrade, as the Hub updater runs it (ALLUSERS=1 only).
  [string] $UpgradeMsi
)
$ErrorActionPreference = 'Stop'
$Msi = (Resolve-Path $Msi).Path
$work = Join-Path $env:RUNNER_TEMP "fb-msi-test-$Scenario"
if (-not $env:RUNNER_TEMP) { $work = Join-Path ([IO.Path]::GetTempPath()) "fb-msi-test-$Scenario" }
New-Item -ItemType Directory -Force $work | Out-Null
$userDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameBeam Player'
$machineDir = Join-Path $env:ProgramFiles 'FrameBeam'
$userStartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\FrameBeam Player.lnk'
$innoKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6F0C2B7E-3D1A-4C55-9B8E-F4A1D2C37A60}_is1'
$script:logN = 0

$script:lastLog = $null
# Every failure shows what the last msiexec did (scope, folders, status, skipped/disallowed actions) and where the Player files
# actually are, so that a failed check is never blind.
function Show-Diagnostics {
  if ($script:lastLog -and (Test-Path $script:lastLog)) {
    Write-Host "::group::key lines of $($script:lastLog)"
    Select-String -Path $script:lastLog -Pattern 'ALLUSERS|MSIINSTALLPERUSER|APPLICATIONFOLDER|INSTALLFOLDER|PLAYERDIR|HUBDIR|Product: |Installation success or error status|Disallowing|Skipping action|LaunchCondition|Feature: |Action ended|Return value 3' |
      Select-Object -First 150 | ForEach-Object { $_.Line.Trim() } | Out-Host
    Write-Host '::endgroup::'
    Write-Host "::group::tail of $($script:lastLog)"; Get-Content $script:lastLog -Tail 80 | Out-Host; Write-Host '::endgroup::'
  }
  # The detached removal of the per-user product logs to ProgramData; copy it next to the other logs (uploaded by CI, *.log).
  foreach ($f in (Get-ChildItem "$env:ProgramData\FrameBeam-remove-per-user.log*" -ErrorAction SilentlyContinue)) {
    $name = if ($f.Name -like '*.status.txt') { 'FrameBeam-remove-per-user.status.log' } else { $f.Name }
    Copy-Item $f.FullName (Join-Path $work $name) -Force -ErrorAction SilentlyContinue
    Write-Host "::group::$($f.FullName)"; Get-Content $f.FullName -Tail 60 | Out-Host; Write-Host '::endgroup::'
  }
  Write-Host '::group::framebeam_player.exe on this machine'
  foreach ($root in (Join-Path $env:LOCALAPPDATA 'Programs'), $env:ProgramFiles) {
    Get-ChildItem -Path $root -Recurse -Filter framebeam_player.exe -ErrorAction SilentlyContinue | ForEach-Object FullName | Out-Host
  }
  Write-Host '::endgroup::'
}
function Fail([string] $m) { Show-Diagnostics; throw $m }
function Show-Log([string] $log) {
  if (Test-Path $log) { Write-Host "::group::$log (tail)"; Get-Content $log -Tail 80 | Out-Host; Write-Host '::endgroup::' }
}
function Invoke-Msiexec([string[]] $msiArgs) {
  $script:logN++
  $log = Join-Path $work "msiexec-$($script:logN).log"
  $script:lastLog = $log
  $p = Start-Process msiexec.exe -ArgumentList (($msiArgs + @('/qn', '/norestart', '/l*v', "`"$log`"")) -join ' ') -Wait -PassThru
  if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { Show-Log $log; Fail "msiexec $($msiArgs -join ' ') exited with $($p.ExitCode)" }
}
function Install-Msi([string[]] $props = @()) { Invoke-Msiexec (@('/i', "`"$Msi`"") + $props) }
function Uninstall-Msi { Invoke-Msiexec @('/x', "`"$Msi`"") }
function Assert-Path([string] $p, [string] $what) { if (-not (Test-Path -LiteralPath $p)) { Fail "$what is missing: $p" } }
function Assert-NoPath([string] $p, [string] $what) { if (Test-Path -LiteralPath $p) { Fail "$what is still there: $p" } }
function Assert-PlayerFiles([string] $dir) {
  foreach ($f in 'framebeam_player.exe', 'bin\framebeam_player.exe', 'THIRD-PARTY-NOTICE.txt', 'LICENSE.txt', 'framebeam-player.msi-installed') {
    Assert-Path (Join-Path $dir $f) $f
  }
  if (-not (Get-ChildItem (Join-Path $dir 'licenses') -File -ErrorAction SilentlyContinue)) { Fail "licenses\ is missing or empty in $dir" }
}
function Assert-PlayerRuns([string] $dir) {
  $env:QT_QPA_PLATFORM = 'offscreen'
  $p = Start-Process (Join-Path $dir 'framebeam_player.exe') -ArgumentList '--version' -WorkingDirectory $dir -PassThru -NoNewWindow
  $null = $p.Handle
  if (-not $p.WaitForExit(60000)) { $p.Kill(); Fail "$dir launcher --version did not exit within 60 s" }
  if ($p.ExitCode -ne 0) { Fail "$dir launcher --version exited with $($p.ExitCode)" }
}
function Get-UserProductCode { (Get-ItemProperty 'HKCU:\Software\FrameBeam\Player' -Name ProductCode -ErrorAction SilentlyContinue).ProductCode }
function Wait-Until([scriptblock] $cond, [int] $seconds, [string] $what) {
  $end = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $end) { if (& $cond) { return }; Start-Sleep -Seconds 2 }
  Fail "timeout waiting for $what"
}

switch ($Scenario) {
  'user' {
    Install-Msi
    Assert-PlayerFiles $userDir
    Assert-Path $userStartMenu 'Start menu shortcut'
    if (-not (Get-UserProductCode)) { Fail 'HKCU\Software\FrameBeam\Player ProductCode is missing' }
    Assert-NoPath (Join-Path $env:ProgramFiles 'FrameBeam\Hub') 'Hub folder (Player-only install)'
    if (Get-Service FrameBeamHub -ErrorAction SilentlyContinue) { Fail 'Hub service exists after a Player-only install' }
    Assert-PlayerRuns $userDir
    # data\ belongs to the user: never installed, never removed.
    New-Item -ItemType Directory -Force (Join-Path $userDir 'data') | Out-Null
    Set-Content (Join-Path $userDir 'data\keep-me') -Value 'keep-me'
    Uninstall-Msi
    Assert-NoPath (Join-Path $userDir 'framebeam_player.exe') 'Player launcher after uninstall'
    Assert-NoPath $userStartMenu 'Start menu shortcut after uninstall'
    Assert-Path (Join-Path $userDir 'data\keep-me') 'data\keep-me'
    Remove-Item -Recurse -Force $userDir
  }

  'machine' {
    # An earlier per-user install with data: the per-machine install replaces the product, data\ stays where it is. Needs
    # a build with another product code (the removal never touches our own code).
    # A stale log of an earlier step must not satisfy the waits below.
    Remove-Item "$env:ProgramData\FrameBeam-remove-per-user.log*" -Force -ErrorAction SilentlyContinue
    if ($OldMsi) {
      Invoke-Msiexec @('/i', "`"$((Resolve-Path $OldMsi).Path)`"")
      if (-not (Get-UserProductCode)) { Fail 'per-user install did not record its product code' }
      New-Item -ItemType Directory -Force (Join-Path $userDir 'data') | Out-Null
      Set-Content (Join-Path $userDir 'data\keep-me') -Value 'keep-me'
    }
    Install-Msi @('ALLUSERS=1', 'INSTALL_HUB=1', 'NETWORK_SHARING=0', 'HUB_PORT=18443')
    $playerDir = Join-Path $machineDir 'Player'
    $hubDir = Join-Path $machineDir 'Hub'
    Assert-PlayerFiles $playerDir
    Assert-Path (Join-Path $hubDir 'framebeam-hub.exe') 'framebeam-hub.exe'
    Assert-Path (Join-Path $hubDir 'framebeam-hub.msi-installed') 'Hub marker'
    Assert-PlayerRuns $playerDir
    $port = (Get-ItemProperty 'HKLM:\Software\FrameBeam\Hub' -Name Port).Port
    if ($port -ne 18443) { Fail "HKLM Port is $port" }
    if ((Get-ItemProperty 'HKLM:\Software\FrameBeam\Hub' -Name InstallDir).InstallDir.TrimEnd('\') -ne $hubDir) { Fail 'HKLM InstallDir differs' }
    # Services.
    foreach ($s in 'FrameBeamHub', 'FrameBeamHubUpdater') {
      if (-not (Get-Service $s -ErrorAction SilentlyContinue)) { Fail "service $s does not exist" }
    }
    $svc = Get-CimInstance Win32_Service -Filter "Name='FrameBeamHub'"
    if ($svc.StartName -ne 'NT SERVICE\FrameBeamHub') { Fail "FrameBeamHub runs as '$($svc.StartName)'" }
    if ($svc.StartMode -ne 'Auto') { Fail "FrameBeamHub start mode is $($svc.StartMode)" }
    if ($svc.PathName -notmatch '-listen :18443' -or $svc.PathName -notmatch '-network-sharing=false') { Fail "unexpected Hub command line: $($svc.PathName)" }
    try {
      Wait-Until { (Get-Service FrameBeamHub).Status -eq 'Running' } 60 'FrameBeamHub to be Running'
    } catch {
      sc.exe qc FrameBeamHub | Out-Host
      Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Service Control Manager' } -MaxEvents 8 -ErrorAction SilentlyContinue | Format-List TimeCreated, Message | Out-Host
      throw
    }
    # Local API (loopback, any cert). TODO(local_setup_v1): the endpoint comes with the Hub change of milestone 0.9; until
    # every branch has it a 404 is only a warning.
    $body = Join-Path $work 'status.json'
    $code = $null
    Wait-Until {
      $script:code = (& curl.exe -k -sS -m 10 -o $body -w '%{http_code}' https://127.0.0.1:18443/api/v1/local/status 2>$null)
      $script:code -eq '200' -or $script:code -eq '404'
    } 60 'the Hub to answer on https://127.0.0.1:18443'
    if ($script:code -eq '404') {
      Write-Host '::warning::/api/v1/local/status answered 404 (local_setup_v1 not in this build yet)'
    } else {
      $st = Get-Content $body -Raw | ConvertFrom-Json
      if ($st.network_sharing -ne $false) { Fail "local status: network_sharing is '$($st.network_sharing)', expected false" }
      if ($Version -and $st.hub_version -ne $Version) { Fail "local status: hub_version '$($st.hub_version)' != $Version" }
    }
    # Sharing off: nothing reachable on a non-loopback address.
    $lan = @(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notmatch '^(127\.|169\.254\.)' } | ForEach-Object IPAddress)
    if ($lan.Count -eq 0) { Write-Host '::warning::no non-loopback IPv4 address on the runner, sharing-off check skipped' }
    else {
      $c = New-Object Net.Sockets.TcpClient
      try { $ok = $c.ConnectAsync($lan[0], 18443).Wait(4000) -and $c.Connected } catch { $ok = $false } finally { $c.Dispose() }
      if ($ok) { Fail "the Hub is reachable on $($lan[0]):18443 although network sharing is off" }
    }
    # Folders and ACLs.
    $hubData = Join-Path $env:ProgramData 'FrameBeam\Hub'
    Assert-Path (Join-Path $hubData 'update-request') 'update-request folder'
    $acl = Get-Acl $hubData
    if (-not ($acl.Access | Where-Object { $_.IdentityReference.Value -eq 'NT SERVICE\FrameBeamHub' })) { Fail "$hubData has no ACE for NT SERVICE\FrameBeamHub" }
    if ($acl.Access | Where-Object { $_.IdentityReference.Value -match 'Users$|Everyone' }) { Fail "$hubData is readable by ordinary users" }
    $upd = Get-Acl (Join-Path $env:ProgramData 'FrameBeam\HubUpdater')
    if ($upd.Access | Where-Object { $_.IdentityReference.Value -notmatch 'SYSTEM$|Administrators$' }) { Fail 'HubUpdater folder has ACEs besides SYSTEM and Administrators' }
    # The per-user product is removed in the background after this installation (nested msiexec is not possible).
    if ($OldMsi) {
      Wait-Until { -not (Get-UserProductCode) -and -not (Test-Path (Join-Path $userDir 'framebeam_player.exe')) } 120 'the per-user product to be removed'
      # The product is gone from the registry before its uninstall has finished: wait for the detached removal to report success
      # and for Windows Installer to be idle (otherwise the next msiexec fails with 1618).
      $removeStatus = "$env:ProgramData\FrameBeam-remove-per-user.log.status.txt"
      Wait-Until {
        (Test-Path $removeStatus) -and ((Get-Content $removeStatus -Tail 1) -match 'exit (0|3010|1605|1614)\s*$')
      } 120 'the detached removal of the per-user product to report success'
      Wait-Until {
        $m = $null
        if (-not [Threading.Mutex]::TryOpenExisting('Global\_MSIExecute', [ref]$m)) { return $true }
        try { if ($m.WaitOne(0)) { $m.ReleaseMutex(); return $true } else { return $false } } catch { return $false } finally { $m.Dispose() }
      } 120 'Windows Installer to be idle (Global\_MSIExecute free)'
      Assert-Path (Join-Path $userDir 'data\keep-me') 'per-user data\keep-me'
    }
    # Upgrade with only ALLUSERS=1 (what the Hub updater passes): the installed feature set, port and sharing are kept.
    if ($UpgradeMsi) {
      $upg = (Resolve-Path $UpgradeMsi).Path
      function Assert-HubCommandLine([string] $port, [string] $sharing) {
        $pn = (Get-CimInstance Win32_Service -Filter "Name='FrameBeamHub'").PathName
        if ($pn -notmatch "-listen :$port" -or $pn -notmatch "-network-sharing=$sharing") { Fail "Hub command line after the upgrade: $pn" }
      }
      Invoke-Msiexec @('/i', "`"$upg`"", 'ALLUSERS=1')
      foreach ($s in 'FrameBeamHub', 'FrameBeamHubUpdater') {
        if (-not (Get-Service $s -ErrorAction SilentlyContinue)) { Fail "service $s is gone after the upgrade" }
      }
      Assert-PlayerFiles $playerDir
      Assert-Path (Join-Path $hubDir 'framebeam-hub.exe') 'framebeam-hub.exe after the upgrade'
      Assert-HubCommandLine '18443' 'false'
      Wait-Until { (Get-Service FrameBeamHub).Status -eq 'Running' } 60 'FrameBeamHub to be Running after the upgrade'
      Invoke-Msiexec @('/x', "`"$upg`"")
      # Hub only, then the same upgrade: the Player must not appear, port and sharing (on for Hub only) stay.
      Install-Msi @('ALLUSERS=1', 'INSTALL_HUB=1', 'INSTALL_PLAYER=0', 'HUB_PORT=18444')
      Assert-NoPath (Join-Path $playerDir 'framebeam_player.exe') 'Player after a Hub-only install'
      Assert-HubCommandLine '18444' 'true'
      Invoke-Msiexec @('/i', "`"$upg`"", 'ALLUSERS=1')
      Assert-NoPath (Join-Path $playerDir 'framebeam_player.exe') 'Player after a Hub-only upgrade'
      Assert-Path (Join-Path $hubDir 'framebeam-hub.exe') 'framebeam-hub.exe after the Hub-only upgrade'
      Assert-HubCommandLine '18444' 'true'
      if (-not (Get-Service FrameBeamHubUpdater -ErrorAction SilentlyContinue)) { Fail 'updater service gone after the Hub-only upgrade' }
      $Msi = $upg   # the installed product now comes from the upgrade package
    }
    # Uninstall.
    Uninstall-Msi
    foreach ($s in 'FrameBeamHub', 'FrameBeamHubUpdater') {
      if (Get-Service $s -ErrorAction SilentlyContinue) { Fail "service $s survived the uninstall" }
    }
    Assert-NoPath (Join-Path $hubDir 'framebeam-hub.exe') 'framebeam-hub.exe after uninstall'
    Assert-NoPath (Join-Path $playerDir 'framebeam_player.exe') 'Player after uninstall'
    Remove-Item -Recurse -Force $userDir -ErrorAction SilentlyContinue
  }

  'migrate' {
    if (-not $Setup) { Fail '-Setup (the shell setup.exe) is required' }
    $Setup = (Resolve-Path $Setup).Path
    # Newest release that still ships the Inno setup: no MSI among its assets.
    $old = $null
    foreach ($r in (gh release list --limit 50 --exclude-drafts --json tagName | ConvertFrom-Json)) {
      $assets = (gh release view $r.tagName --json assets | ConvertFrom-Json).assets.name
      if ($assets -like 'framebeam-*-windows-x64.msi') { continue }
      $name = $assets | Where-Object { $_ -like 'framebeam-player-*-windows-x64-setup.exe' } | Select-Object -First 1
      if ($name) { $old = @{ tag = $r.tagName; name = $name }; break }
    }
    if (-not $old) { Write-Host '::warning::no release with an Inno Player setup left (all pruned), migration test skipped'; return }
    Write-Host "Old Inno setup: $($old.name) of $($old.tag)"
    gh release download $old.tag --pattern $old.name --dir $work --clobber
    if ($LASTEXITCODE -ne 0) { Fail "download of $($old.name) failed" }
    $oldSetup = Join-Path $work $old.name
    $p = Start-Process $oldSetup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER' -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "old setup exited with $($p.ExitCode)" }
    Assert-Path $innoKey 'Inno uninstall key (old install)'
    Assert-Path (Join-Path $userDir 'unins000.exe') 'unins000.exe'
    New-Item -ItemType Directory -Force (Join-Path $userDir 'data') | Out-Null
    Set-Content (Join-Path $userDir 'data\keep-me') -Value 'keep-me'
    # Exactly what the old Player's updater passes (without /UPDATE, which would start the GUI Player afterwards).
    $p = Start-Process $Setup -ArgumentList '/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER' -Wait -PassThru
    if ($p.ExitCode -ne 0) { Fail "shell setup exited with $($p.ExitCode)" }
    Assert-NoPath $innoKey 'Inno uninstall key'
    Assert-NoPath (Join-Path $userDir 'unins000.exe') 'unins000.exe'
    $code = Get-UserProductCode
    if (-not $code) { Fail 'MSI product (HKCU ProductCode) not present after the shell setup' }
    Assert-PlayerFiles $userDir
    Assert-Path (Join-Path $userDir 'data\keep-me') 'data\keep-me'
    Assert-PlayerRuns $userDir
    Invoke-Msiexec @('/x', $code)
    Assert-Path (Join-Path $userDir 'data\keep-me') 'data\keep-me after uninstall'
    Remove-Item -Recurse -Force $userDir
  }
}
Write-Host "Scenario '$Scenario' passed"
