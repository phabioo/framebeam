#pragma once
// CoreLocator: finds the core library for a manifest.
// Order: 1. explicitly set path (setExplicitPath), 2. environment variable
// FRAMEBEAM_<CORE_ID>_CORE (e.g. FRAMEBEAM_MELONDS_DS_CORE), 3. <app-dir>/cores/<basename>.<so|dll|dylib>.
// The only place with platform-specific extensions.

#include <QMap>
#include <QString>
#include <QStringList>

#include "system_manifest.h"

namespace framebeam::emu {

struct CoreLocation {
  QString path;    // empty = not found
  QString source;  // "explicit" | "env" | "app-dir"
  QStringList tried;  // checked but not present (for diagnostics)
  bool found() const { return !path.isEmpty(); }
};

class CoreLocator {
 public:
  // empty appDir = QCoreApplication::applicationDirPath().
  explicit CoreLocator(QString appDir = QString());

  void setExplicitPath(const QString& coreId, const QString& path);
  CoreLocation locate(const SystemManifest& manifest) const;

  static QString environmentVariableFor(const QString& coreId);
  static QString libraryFileName(const QString& basename);  // basename + platform-specific extension

 private:
  QString m_appDir;
  QMap<QString, QString> m_explicit;
};

}  // namespace framebeam::emu
