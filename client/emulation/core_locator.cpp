#include "core_locator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace framebeam::emu {

CoreLocator::CoreLocator(QString appDir) : m_appDir(std::move(appDir)) {
  if (m_appDir.isEmpty()) m_appDir = QCoreApplication::applicationDirPath();
}

void CoreLocator::setExplicitPath(const QString& coreId, const QString& path) { m_explicit.insert(coreId, path); }

QString CoreLocator::environmentVariableFor(const QString& coreId) {
  return QStringLiteral("FRAMEBEAM_") + coreId.toUpper().replace(QLatin1Char('-'), QLatin1Char('_')) + QStringLiteral("_CORE");
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

void CoreLocator::setCache(const QString& root, const QString& platform) {
  m_cacheRoot = root;
  m_platform = platform;
}

namespace {
int compareVersions(const QString& a, const QString& b) {
  const QStringList sa = a.split(QLatin1Char('.'));
  const QStringList sb = b.split(QLatin1Char('.'));
  const qsizetype n = std::min(sa.size(), sb.size());
  for (qsizetype i = 0; i < n; ++i) {
    bool oa = false;
    bool ob = false;
    const qlonglong na = sa.at(i).toLongLong(&oa);
    const qlonglong nb = sb.at(i).toLongLong(&ob);
    if (oa && ob) {
      if (na != nb) return na < nb ? -1 : 1;
    } else if (sa.at(i) != sb.at(i)) {
      return sa.at(i) < sb.at(i) ? -1 : 1;
    }
  }
  if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
  return a == b ? 0 : (a < b ? -1 : 1);
}

// Library of a cached package: package.json names the library file; it must exist with the recorded size.
QString cachedLibrary(const QDir& pkgDir) {
  QFile f(pkgDir.filePath(QStringLiteral("package.json")));
  if (!f.open(QIODevice::ReadOnly)) return {};
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray()) {
    const QJsonObject fo = v.toObject();
    if (fo.value(QStringLiteral("role")).toString() != QLatin1String("library")) continue;
    const QString name = fo.value(QStringLiteral("name")).toString();
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1String(".."))) return {};
    const QFileInfo fi(pkgDir.filePath(name));
    return fi.isFile() && fi.size() == fo.value(QStringLiteral("size")).toVariant().toLongLong() ? fi.absoluteFilePath() : QString();
  }
  return {};
}
}  // namespace

CoreLocation CoreLocator::locate(const SystemManifest& manifest, const QString& preferredVersion) const {
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
  // Environment: FRAMEBEAM_<CORE_ID>_CORE, also under the core's other ids (FRAMEBEAM_MELONDS_DS_CORE for melondsds).
  for (const QString& id : QStringList(manifest.coreId) + manifest.coreAliases) {
    if (probe(qEnvironmentVariable(environmentVariableFor(id).toUtf8().constData()), "env")) return loc;
  }
  if (!m_cacheRoot.isEmpty() && !m_platform.isEmpty()) {
    // The core's own cache directory first, then those of its other ids (a core cached under a legacy id still works).
    for (const QString& id : QStringList(manifest.coreId) + manifest.coreAliases) {
      const QDir coreDir(QDir(m_cacheRoot).filePath(id));
      QStringList versions = coreDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) { return compareVersions(a, b) > 0; });
      if (!preferredVersion.isEmpty() && versions.removeOne(preferredVersion)) {
        versions.prepend(preferredVersion);  // preferred first, the others as a fallback (newest first)
      }
      for (const QString& v : versions) {
        const QString lib = cachedLibrary(QDir(coreDir.filePath(v + QLatin1Char('/') + m_platform)));
        if (!lib.isEmpty()) {
          loc.path = QDir::cleanPath(lib);
          loc.source = QStringLiteral("cache");
          loc.version = v;
          loc.cacheCoreId = id;
          return loc;
        }
      }
    }
    loc.tried.append(QDir(m_cacheRoot).filePath(manifest.coreId + QStringLiteral("/<version>/") + m_platform));
  }
  probe(QDir(m_appDir).filePath(QStringLiteral("cores/") + libraryFileName(manifest.coreLibraryBasename)), "app-dir");
  return loc;
}

}  // namespace framebeam::emu
