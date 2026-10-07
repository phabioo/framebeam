; FrameBeam Player installer (Inno Setup 6.3+). Built in CI from the assembled package (dist\framebeam-player).
; Compile: ISCC /DAppVersion=0.3.0-test.5 [/DVersionInfoVersion=0.3.0.5] [/DPackageDir=...] [/DOutputDir=...] [/DOutputBaseName=...] framebeam-player.iss
; Package layout (spec 0.3 S6): launcher framebeam_player.exe and THIRD-PARTY-NOTICE.txt at the top, everything else
; (real framebeam_player.exe, Qt/vcpkg DLLs, plugins, qml) in bin\.
; Per-user by default (no admin rights, %LOCALAPPDATA%\Programs\FrameBeam Player) so the portable <exe-dir>\data
; stays writable (ADR 0004). The setup offers an all-users install via the privileges dialog; under Program Files
; the Player falls back to AppData (ADR 0004). The uninstaller keeps data\ unless the user agrees to remove it.

#ifndef AppVersion
  #error AppVersion is not defined: pass /DAppVersion=<version> to ISCC
#endif
#ifndef VersionInfoVersion
  #define VersionInfoVersion "0.0.0.0"
#endif
#ifndef PackageDir
  #define PackageDir "..\..\dist\framebeam-player"
#endif
#ifndef OutputBaseName
  #define OutputBaseName "framebeam-player-setup"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist\installer"
#endif

#define AppName "FrameBeam Player"
#define AppExe "framebeam_player.exe"
; Same AppUserModelID as the executables (taskbar grouping, notifications); the Player creates the mutex below at start.
#define AppUserModelID "FrameBeam.Player"
#define AppMutex "FrameBeamPlayer"

[Setup]
; Fixed identity of the product: never change, upgrades and the uninstaller depend on it.
AppId={{6F0C2B7E-3D1A-4C55-9B8E-F4A1D2C37A60}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=FrameBeam
VersionInfoVersion={#VersionInfoVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Interactive installs ask the user to close a running Player; "/UPDATE" waits for it (see [Code]).
AppMutex={#AppMutex}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile={#PackageDir}\THIRD-PARTY-NOTICE.txt
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Unsigned in the PoC.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Whole package: launcher and notice text at the top, everything else in bin\ (see header). No emulator cores
; (downloaded from the Hub), no data\ and no ROMs/BIOS are part of the package.
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "data\*"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; Upgrade from the old flat layout (before 0.3): the DLLs and the windeployqt directories were next to the exe.
; Explicit list, so data\ (ADR 0004) and anything the user put into the folder is never touched. The new layout
; keeps no DLLs at the top level, so every top-level DLL is stale.
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\qt.conf"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\multimedia"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\translations"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\position"
Type: filesandordirs; Name: "{app}\sensors"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; AppUserModelID: "{#AppUserModelID}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; AppUserModelID: "{#AppUserModelID}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
; Silent update started by the Player (/UPDATE): start the new version again, as the user who runs the installer
; (an all-users install is elevated, the Player must not be).
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Flags: nowait runasoriginaluser skipifnotsilent; Check: IsUpdateMode

[Code]
// "/UPDATE" is passed by the Player's updater (spec 0.3 S6): wait for the old Player to exit, relaunch afterwards.
function IsUpdateMode: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/UPDATE') = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

// Wait up to 60 s until no Player process holds the mutex (the Player quits right after starting the installer).
function InitializeSetup: Boolean;
var
  Waited: Integer;
begin
  Result := True;
  if IsUpdateMode then
  begin
    Waited := 0;
    while CheckForMutexes('{#AppMutex}') and (Waited < 60000) do
    begin
      Sleep(500);
      Waited := Waited + 500;
    end;
  end;
end;

// Player data (ROM cache, profiles, saves) lives in {app}\data and is not tracked by the installer.
// Default: keep it. Removed only on explicit confirmation; silent uninstalls always keep it.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{app}\data');
    if DirExists(DataDir) and (not UninstallSilent) then
    begin
      if MsgBox('Also remove my data?' + #13#10#13#10 +
                'This deletes the ROM cache, profiles and local saves in:' + #13#10 + DataDir + #13#10#13#10 +
                'Choose No to keep them (for example to reinstall later).',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(DataDir, True, True, True);
    end;
  end;
end;
