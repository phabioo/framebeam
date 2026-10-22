#pragma once
// Install layout of the Windows Player (spec 0.3 S6):
//   {app}\Uzume.exe                  tiny launcher (launcher/), starts bin\Uzume.exe
//   {app}\bin\Uzume.exe              the real app (+ DLLs, Qt plugins, qml)
//   {app}\data\                       portable data (ADR 0004), {app}\uzume-app.msi-installed = installed via the MSI

#include <QString>
#include <QStringList>

namespace uzume::update {

inline constexpr const char* kPlayerExeName = "Uzume.exe";

// File name of the launcher in `installRoot` (kPlayerExeName); empty if it does not exist.
QString launcherFileName(const QString& installRoot);

// When the executable's directory is named "bin" and its parent contains the launcher, the install
// root is the parent; otherwise the executable's directory itself (flat/portable layout, Linux, dev builds).
QString installRootFor(const QString& exeDir);

inline constexpr const char* kPlayerMsiMarker = "uzume-app.msi-installed";  // next to the launcher (MSI install)
inline constexpr const char* kHubMsiMarker = "uzume-server.msi-installed";      // next to uzume-server.exe

// The root contains the MSI marker of the Player feature.
bool hasMsiMarker(const QString& installRoot);
// Only an installed copy (MSI marker) may update itself.
bool canSelfUpdate(const QString& installRoot);
// The per-machine MSI installs the Hub as a sibling of the Player directory (<ProgramFiles>\Uzume\{App,Server}).
bool hubInstalledNear(const QString& installRoot);
bool isMsiFile(const QString& path);
// A file can really be created and removed in the directory (a permission check is not enough).
bool directoryWritable(const QString& dir);

struct InstallerCommand {
  QString program;
  QStringList args;
};

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
// Copies <root>\Uzume.exe to a fresh folder below tempBase; returns the copy ("" + error on failure).
QString copyLauncherToTemp(const QString& installRoot, const QString& tempBase, QString* error = nullptr);
// <launcherCopy> --apply-msi-update [--sha256 <hex> --size <n>] <msi> <scope> <relaunchExe> [relaunchArgs...]: the copy
// waits until no Player runs, re-verifies size and SHA-256 of the MSI (refuses on mismatch, mandatory for elevated
// scopes), runs msiexec (elevated for non-user scopes), starts relaunchExe with relaunchArgs and removes itself.
// `sha256`/`size` are the values the Player verified against the signed update index (empty / < 0 = not passed).
InstallerCommand msiRelaunchCommand(const QString& launcherCopy, const QString& msiPath, MsiScope scope,
                                    const QString& relaunchExe, const QStringList& relaunchArgs = {},
                                    const QString& sha256 = {}, qint64 size = -1);
// Where the per-machine Player lives after "Set up a Hub on this PC" (<programFiles>\Uzume\App).
QString perMachinePlayerExe(const QString& programFilesDir);

}  // namespace uzume::update
