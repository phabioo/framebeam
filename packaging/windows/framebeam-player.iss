; FrameBeam Player installer (Inno Setup 6.3+). Built in CI from the assembled package (dist\framebeam-player).
; Compile: ISCC /DAppVersion=0.0.0+abc1234 [/DVersionInfoVersion=0.0.0.1] [/DPackageDir=...] [/DOutputDir=...] framebeam-player.iss
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
#ifndef OutputDir
  #define OutputDir "..\..\dist\installer"
#endif

#define AppName "FrameBeam Player"
#define AppExe "framebeam_player.exe"

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
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile={#PackageDir}\THIRD-PARTY-NOTICE.txt
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=framebeam-player-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Unsigned in the PoC.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Whole package: exe, Qt and vcpkg DLLs, plugins, qml and the notice text
; (THIRD-PARTY-NOTICE.txt). No emulator cores (downloaded from the Hub), no data\ and no ROMs/BIOS are part of the package.
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
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
