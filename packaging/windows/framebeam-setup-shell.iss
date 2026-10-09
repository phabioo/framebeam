; FrameBeam setup shell (Inno Setup 6.3+): a tiny setup.exe around the FrameBeam MSI. It exists so that old Players, whose updater
; only knows Inno installers (/SILENT /UPDATE ...), can migrate to the MSI; new Players use the MSI directly (index kind msi).
; Compile: ISCC /DAppVersion=0.9.0 /DMsiFile=C:\path\framebeam-0.9.0-windows-x64.msi [/DOutputDir=...] [/DOutputBaseName=...] framebeam-setup-shell.iss
; It does not register itself (no AppId, Uninstallable=no, CreateAppDir=no): the MSI is the only installed product. The Inno
; installation it replaces is removed by the MSI itself (user data\ stays).
;   silent (/SILENT, /VERYSILENT)   msiexec /i msi /qn /norestart, per user (/CURRENTUSER or nothing: MSIINSTALLPERUSER=1 ALLUSERS=2)
;                                   or per machine (/ALLUSERS, setup elevates itself: ALLUSERS=1)
;   /UPDATE                         wait for the old Player (mutex), then start the new one as the original user
;   no flags (manual run)           msiexec /i msi with the full MSI wizard

#ifndef AppVersion
  #error AppVersion is not defined: pass /DAppVersion=<version> to ISCC
#endif
#ifndef MsiFile
  #error MsiFile is not defined: pass /DMsiFile=<path of the MSI> to ISCC
#endif
#ifndef OutputBaseName
  #define OutputBaseName "framebeam-player-setup"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist\installer"
#endif

#define AppMutex "FrameBeamPlayer"
#define AppExe "framebeam_player.exe"
#define MsiName "framebeam.msi"

[Setup]
AppName=FrameBeam Setup
AppVersion={#AppVersion}
AppPublisher=FrameBeam
CreateAppDir=no
Uninstallable=no
DisableProgramGroupPage=yes
DisableWelcomePage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
DisableStartupPrompt=yes
ShowLanguageDialog=no
; Like the old installer: per user unless /ALLUSERS is given, which asks for elevation.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\..\client\app\icons\player.ico
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
; The MSI is compressed already.
Compression=none
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#MsiFile}"; DestName: "{#MsiName}"; Flags: dontcopy

[Code]
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

function MsiExitIsSuccess(Code: Integer): Boolean;
begin
  // 3010 / 1641: success, restart required (never forced: /norestart).
  Result := (Code = 0) or (Code = 3010) or (Code = 1641);
end;

function MsiArguments(const Msi: String): String;
begin
  Result := '/i "' + Msi + '" /qn /norestart';
  if IsAdminInstallMode then
    Result := Result + ' ALLUSERS=1'
  else
    Result := Result + ' MSIINSTALLPERUSER=1 ALLUSERS=2';
end;

// Where the new Player lives after the MSI: per user in the former Inno folder, per machine in Program Files.
function InstalledPlayer: String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commonpf64}\FrameBeam\Player\{#AppExe}')
  else
    Result := ExpandConstant('{localappdata}\Programs\FrameBeam Player\{#AppExe}');
end;

// Updates: wait up to 60 s until no Player holds the mutex (the Player quits right after starting the setup).
// Manual runs: hand over to the MSI wizard right here (no Inno wizard pages are shown) and exit afterwards.
function InitializeSetup: Boolean;
var
  Waited, Code: Integer;
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
  if not WizardSilent then
  begin
    ExtractTemporaryFile('{#MsiName}');
    if not Exec(ExpandConstant('{sys}\msiexec.exe'), '/i "' + ExpandConstant('{tmp}\{#MsiName}') + '"', '',
                SW_SHOWNORMAL, ewWaitUntilTerminated, Code) then
      MsgBox('Could not start Windows Installer: ' + SysErrorMessage(Code), mbError, MB_OK);
    // Nothing left for the Inno part to do.
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  Msi, Player: String;
begin
  if CurStep <> ssInstall then Exit;
  ExtractTemporaryFile('{#MsiName}');
  Msi := ExpandConstant('{tmp}\{#MsiName}');
  if not Exec(ExpandConstant('{sys}\msiexec.exe'), MsiArguments(Msi), '', SW_HIDE, ewWaitUntilTerminated, Code) then
    RaiseException('Could not start Windows Installer: ' + SysErrorMessage(Code));
  if not MsiExitIsSuccess(Code) then
    RaiseException('The FrameBeam installation failed (Windows Installer error ' + IntToStr(Code) + ').');
  // Silent update started by the Player: start the new version as the user who ran the setup (an all-users install is
  // elevated, the Player must not be).
  if IsUpdateMode then
  begin
    Player := InstalledPlayer;
    if FileExists(Player) then
      ExecAsOriginalUser(Player, '', ExtractFilePath(Player), SW_SHOWNORMAL, ewNoWait, Code);
  end;
end;
