#include "core_locator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace framebeam::emu {

CoreLocator::CoreLocator(QString appDir) : m_appDir(std::move(appDir)) {
  if (m_appDir.isEmpty()) m_appDir = QCoreApplication::applicationDirPath();
}

void CoreLocator::setExplicitPath(const QString& coreId, const QString& path) { m_explicit.insert(coreId, path); }

QString CoreLocator::environmentVariableFor(const QString& coreId) {
  return QStringLiteral("FRAMEBEAM_") + coreId.toUpper() + QStringLiteral("_CORE");
}

QString CoreLocator::libraryFileName(const QString& basename) {
#if defined(Q_OS_WIN)
  return basename + QStringLiteral(".dll");
#elif defined(Q_OS_MACOS)
  return basename + QStringLiteral(".dylib");
#else
  return basename + QStringLiteral(".so");
#endif
}

CoreLocation CoreLocator::locate(const SystemManifest& manifest) const {
  CoreLocation loc;
  auto probe = [&](const QString& path, const char* source) {
    if (path.isEmpty()) return false;
    if (QFileInfo(path).isFile()) {
      loc.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
      loc.source = QLatin1String(source);
      return true;
    }
    loc.tried.append(path);
    return false;
  };
  if (probe(m_explicit.value(manifest.coreId), "explicit")) return loc;
  if (probe(qEnvironmentVariable(environmentVariableFor(manifest.coreId).toUtf8().constData()), "env")) return loc;
  probe(QDir(m_appDir).filePath(QStringLiteral("cores/") + libraryFileName(manifest.coreLibraryBasename)), "app-dir");
  return loc;
}

}  // namespace framebeam::emu
