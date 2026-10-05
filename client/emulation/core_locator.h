#pragma once
// CoreLocator: findet die Core-Bibliothek zu einem Manifest.
// Reihenfolge: 1. explizit gesetzter Pfad (setExplicitPath), 2. Umgebungsvariable
// FRAMEBEAM_<CORE_ID>_CORE (z. B. FRAMEBEAM_MELONDS_DS_CORE), 3. <app-dir>/cores/<basename>.<so|dll|dylib>.
// Einzige Stelle mit Plattform-Endungen.

#include <QMap>
#include <QString>
#include <QStringList>

#include "system_manifest.h"

namespace framebeam::emu {

struct CoreLocation {
  QString path;    // leer = nicht gefunden
  QString source;  // "explicit" | "env" | "app-dir"
  QStringList tried;  // geprueft, aber nicht vorhanden (fuer Diagnostics)
  bool found() const { return !path.isEmpty(); }
};

class CoreLocator {
 public:
  // appDir leer = QCoreApplication::applicationDirPath().
  explicit CoreLocator(QString appDir = QString());

  void setExplicitPath(const QString& coreId, const QString& path);
  CoreLocation locate(const SystemManifest& manifest) const;

  static QString environmentVariableFor(const QString& coreId);
  static QString libraryFileName(const QString& basename);  // basename + plattformspezifische Endung

 private:
  QString m_appDir;
  QMap<QString, QString> m_explicit;
};

}  // namespace framebeam::emu
