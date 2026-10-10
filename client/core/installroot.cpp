#include "installroot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace framebeam::update {

QString installRootFor(const QString& exeDir) {
  if (exeDir.isEmpty()) return exeDir;
  const QDir d(exeDir);
  if (d.dirName().compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0) {
    QDir parent = d;
    if (parent.cdUp() && parent.exists(QLatin1String(kPlayerExeName))) {
      return parent.absolutePath();
    }
  }
  return d.absolutePath();
}

bool hasUninstaller(const QString& installRoot) {
  return !installRoot.isEmpty() && QFileInfo::exists(QDir(installRoot).filePath(QLatin1String(kUninstallerName)));
}

bool hasMsiMarker(const QString& installRoot) {
  return !installRoot.isEmpty() && QFileInfo::exists(QDir(installRoot).filePath(QLatin1String(kPlayerMsiMarker)));
}

bool canSelfUpdate(const QString& installRoot) { return hasUninstaller(installRoot) || hasMsiMarker(installRoot); }

bool hubInstalledNear(const QString& installRoot) {
  if (installRoot.isEmpty()) return false;
  QDir d(installRoot);
  if (!d.cdUp()) return false;
  return QFileInfo::exists(d.filePath(QStringLiteral("Hub/") + QLatin1String(kHubMsiMarker)));
}

bool isMsiFile(const QString& path) { return path.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive); }

bool directoryWritable(const QString& dir) {
  if (dir.isEmpty() || !QFileInfo(dir).isDir()) return false;
  const QString probe = QDir(dir).filePath(QStringLiteral(".fbwrite-") + QUuid::createUuid().toString(QUuid::Id128));
  QFile f(probe);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.close();
  return f.remove();
}

InstallerCommand installerCommand(const QString& setupPath, bool rootWritable) {
  InstallerCommand c;
  c.program = setupPath;
  c.args = {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/NORESTART"),
            QStringLiteral("/UPDATE"),
            rootWritable ? QStringLiteral("/CURRENTUSER") : QStringLiteral("/ALLUSERS")};
  return c;
}

MsiScope detectMsiScope(bool rootWritable, bool hubInstalled) {
  if (hubInstalled) return MsiScope::MachineWithHub;
  return rootWritable ? MsiScope::User : MsiScope::Machine;
}

QString msiScopeName(MsiScope s) {
  switch (s) {
    case MsiScope::User: return QStringLiteral("user");
    case MsiScope::Machine: return QStringLiteral("machine");
    case MsiScope::MachineWithHub: return QStringLiteral("machine-hub");
    case MsiScope::SetupHub: return QStringLiteral("setup-hub");
  }
  return QStringLiteral("user");
}

bool msiScopeNeedsElevation(MsiScope s) { return s != MsiScope::User; }

// msiexec and the launcher need backslash paths (forward slashes give error 1619); explicit so it also works on Linux.
static QString withBackslashes(QString path) { return path.replace(QLatin1Char('/'), QLatin1Char('\\')); }

QStringList msiexecArguments(const QString& msiPath, MsiScope scope) {
  QStringList a{QStringLiteral("/i"), withBackslashes(msiPath), QStringLiteral("/qn"), QStringLiteral("/norestart")};
  switch (scope) {
    case MsiScope::User:
      a << QStringLiteral("MSIINSTALLPERUSER=1") << QStringLiteral("ALLUSERS=2");
      break;
    case MsiScope::Machine:
      a << QStringLiteral("ALLUSERS=1");
      break;
    case MsiScope::MachineWithHub:
      a << QStringLiteral("ALLUSERS=1") << QStringLiteral("INSTALL_HUB=1");
      break;
    case MsiScope::SetupHub:
      a << QStringLiteral("ALLUSERS=1") << QStringLiteral("INSTALL_HUB=1") << QStringLiteral("NETWORK_SHARING=0");
      break;
  }
  return a;
}

QString copyLauncherToTemp(const QString& installRoot, const QString& tempBase, QString* error) {
  const auto fail = [error](const QString& why) {
    if (error != nullptr) *error = why;
    return QString();
  };
  const QString src = QDir(installRoot).filePath(QLatin1String(kPlayerExeName));
  if (installRoot.isEmpty() || !QFileInfo::exists(src)) return fail(QStringLiteral("launcher not found next to the Player"));
  const QString dir = QDir(tempBase).filePath(QStringLiteral("framebeam-update-") + QUuid::createUuid().toString(QUuid::Id128));
  if (!QDir().mkpath(dir)) return fail(QStringLiteral("temporary folder not creatable"));
  const QString dst = QDir(dir).filePath(QLatin1String(kPlayerExeName));
  if (!QFile::copy(src, dst)) return fail(QStringLiteral("launcher not copyable"));
  return dst;
}

InstallerCommand msiRelaunchCommand(const QString& launcherCopy, const QString& msiPath, MsiScope scope,
                                    const QString& relaunchExe, const QStringList& relaunchArgs) {
  InstallerCommand c;
  c.program = launcherCopy;
  c.args = {QStringLiteral("--apply-msi-update"), withBackslashes(msiPath), msiScopeName(scope), withBackslashes(relaunchExe)};
  c.args += relaunchArgs;
  return c;
}

QString perMachinePlayerExe(const QString& programFilesDir) {
  return QDir(programFilesDir).filePath(QStringLiteral("FrameBeam/Player/") + QLatin1String(kPlayerExeName));
}

}  // namespace framebeam::update
