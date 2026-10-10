#pragma once
// Install layout of the Windows Player (spec 0.3 S6):
//   {app}\framebeam_player.exe        tiny launcher (launcher/), starts bin\framebeam_player.exe
//   {app}\bin\framebeam_player.exe    the real Player (+ DLLs, Qt plugins, qml)
//   {app}\data\                       portable data (ADR 0004), {app}\unins000.exe = installed via the installer

#include <QString>
#include <QStringList>

namespace framebeam::update {

inline constexpr const char* kPlayerExeName = "framebeam_player.exe";
inline constexpr const char* kUninstallerName = "unins000.exe";

// When the executable's directory is named "bin" and its parent contains framebeam_player.exe, the install
// root is the parent; otherwise the executable's directory itself (flat/portable layout, Linux, dev builds).
QString installRootFor(const QString& exeDir);

inline constexpr const char* kPlayerMsiMarker = "framebeam-player.msi-installed";  // next to the launcher (MSI install)
inline constexpr const char* kHubMsiMarker = "framebeam-hub.msi-installed";        // next to framebeam-hub.exe

// The root contains the installer's uninstaller (Inno install).
bool hasUninstaller(const QString& installRoot);
// The root contains the MSI marker of the Player feature.
bool hasMsiMarker(const QString& installRoot);
// Only an installed copy (Inno uninstaller or MSI marker) may update itself.
bool canSelfUpdate(const QString& installRoot);
// The per-machine MSI installs the Hub as a sibling of the Player directory (<ProgramFiles>\FrameBeam\{Player,Hub}).
bool hubInstalledNear(const QString& installRoot);
bool isMsiFile(const QString& path);
// A file can really be created and removed in the directory (a permission check is not enough).
bool directoryWritable(const QString& dir);

struct InstallerCommand {
  QString program;
  QStringList args;
};
// <setup> /SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE  plus /ALLUSERS (root not writable, UAC) or /CURRENTUSER.
InstallerCommand installerCommand(const QString& setupPath, bool rootWritable);

// ---- MSI updates (0.9). msiexec arguments are built here (and mirrored in launcher/msiupdate.h, which has no Qt).
enum class MsiScope {
  User,           // per-user Player, install root writable: no elevation
  Machine,        // per-machine Player (UAC)
  MachineWithHub, // per-machine Player + Hub feature installed (UAC), INSTALL_HUB=1
  SetupHub,       // "Set up a Hub on this PC": ALLUSERS=1 INSTALL_HUB=1 NETWORK_SHARING=0 (UAC)
};
MsiScope detectMsiScope(bool rootWritable, bool hubInstalled);
QString msiScopeName(MsiScope s);  // user | machine | machine-hub | setup-hub
bool msiScopeNeedsElevation(MsiScope s);
// /i <msi> /qn /norestart + properties of the scope.
QStringList msiexecArguments(const QString& msiPath, MsiScope scope);
// Copies <root>\framebeam_player.exe to a fresh folder below tempBase; returns the copy ("" + error on failure).
QString copyLauncherToTemp(const QString& installRoot, const QString& tempBase, QString* error = nullptr);
// <launcherCopy> --apply-msi-update [--sha256 <hex> --size <n>] <msi> <scope> <relaunchExe> [relaunchArgs...]: the copy
// waits until no Player runs, re-verifies size and SHA-256 of the MSI (refuses on mismatch, mandatory for elevated
// scopes), runs msiexec (elevated for non-user scopes), starts relaunchExe with relaunchArgs and removes itself.
// `sha256`/`size` are the values the Player verified against the signed update index (empty / < 0 = not passed).
InstallerCommand msiRelaunchCommand(const QString& launcherCopy, const QString& msiPath, MsiScope scope,
                                    const QString& relaunchExe, const QStringList& relaunchArgs = {},
                                    const QString& sha256 = {}, qint64 size = -1);
// Where the per-machine Player lives after "Set up a Hub on this PC" (<programFiles>\FrameBeam\Player).
QString perMachinePlayerExe(const QString& programFilesDir);

}  // namespace framebeam::update
