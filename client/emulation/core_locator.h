#pragma once
// CoreLocator: finds the core library for a manifest.
// Order: 1. explicitly set path (setExplicitPath), 2. environment variable
// FRAMEBEAM_<CORE_ID>_CORE (e.g. FRAMEBEAM_MELONDS_DS_CORE), 3. core cache <root>/<core_id>/<version>/<platform>/
// (package.json + library; preferred version if cached, otherwise the newest cached version), 4. <app-dir>/cores/
// <basename>.<so|dll|dylib> (legacy/dev). The cache check is light (package.json, library present with the size
// from package.json); SHA-256 validation is done by CoreCache when the package is stored and before a game start.
// The only place with platform-specific extensions.

#include <QMap>
#include <QString>
#include <QStringList>

#include "system_manifest.h"

namespace framebeam::emu {

struct CoreLocation {
  QString path;    // empty = not found
  QString source;  // "explicit" | "env" | "cache" | "app-dir"
  QString version;    // cache source only: version of the cached package
  QString cacheCoreId;  // cache source only: core id of the cache directory (the core's id or one of its aliases)
  QStringList tried;  // checked but not present (for diagnostics)
  bool found() const { return !path.isEmpty(); }
};

class CoreLocator {
 public:
  // empty appDir = QCoreApplication::applicationDirPath().
  explicit CoreLocator(QString appDir = QString());

  void setExplicitPath(const QString& coreId, const QString& path);
  // Core cache (plain parameters): root = <data>/cache/cores, platform e.g. "linux-x64". Empty = no cache lookup.
  void setCache(const QString& root, const QString& platform);
  // preferredVersion: version the Hub serves (empty = newest cached).
  CoreLocation locate(const SystemManifest& manifest, const QString& preferredVersion = QString()) const;

  static QString environmentVariableFor(const QString& coreId);
  static QString libraryFileName(const QString& basename);  // basename + platform-specific extension

 private:
  QString m_appDir;
  QString m_cacheRoot;
  QString m_platform;
  QMap<QString, QString> m_explicit;
};

}  // namespace framebeam::emu
