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

}  // namespace framebeam::update
