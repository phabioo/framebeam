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

// The root contains the installer's uninstaller (only then the Player may update itself).
bool hasUninstaller(const QString& installRoot);
// A file can really be created and removed in the directory (a permission check is not enough).
bool directoryWritable(const QString& dir);

struct InstallerCommand {
  QString program;
  QStringList args;
};
// <setup> /SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE  plus /ALLUSERS (root not writable, UAC) or /CURRENTUSER.
InstallerCommand installerCommand(const QString& setupPath, bool rootWritable);

}  // namespace framebeam::update
