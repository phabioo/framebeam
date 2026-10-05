#pragma once
// Datengetriebene System-Manifeste (z. B. manifests/nds.json). Keine konsolenspezifische
// Startlogik ausserhalb dieser Daten: Core-Zuordnung, Endungen, Firmware, Input-/Display-Profil
// und Core-Option-Defaults kommen aus dem Manifest.

#include <QList>
#include <QMap>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

namespace framebeam::emu {

struct ScreenSpec {
  QString id;  // z. B. "top", "bottom"
  int width = 0;
  int height = 0;
  bool touch = false;  // Touch-/Zeigereingabe landet auf diesem Screen
};

// Anordnung der Screens im vom Core gelieferten Gesamtframe.
struct DisplayProfile {
  QString layout;  // "single" | "vertical" (untereinander) | "horizontal" (nebeneinander)
  int gap = 0;     // Pixel zwischen den Screens
  QList<ScreenSpec> screens;

  QSize frameSize() const;
  QRect screenRect(int index) const;  // Pixelrechteck des Screens im Gesamtframe
  int touchScreenIndex() const;       // -1 = kein Touch-Screen
  // Normierte Position (0..1) auf dem Screen -> normierte Position im Gesamtframe
  // (Eingabe fuer EmulatorBackend::setPointer).
  QPointF toFrameNormalized(int screenIndex, QPointF screenNormalized) const;
};

struct FirmwareFile {
  QString name;
  bool required = false;
};

struct FirmwareSpec {
  bool required = false;  // true: ohne Firmware kein Start ("Firmware required/missing")
  QList<FirmwareFile> files;
};

struct SystemManifest {
  QString systemId;
  QString displayName;
  QString coreId;
  QString coreLibraryBasename;
  QStringList extensions;  // klein, mit Punkt
  FirmwareSpec firmware;
  QString inputProfile;
  QString displayProfile;
  DisplayProfile display;
  QMap<QString, QString> coreOptions;  // Defaults fuer Core Options (Schluessel -> Wert)

  bool supportsExtension(const QString& ext) const;  // ".nds" oder "nds", Gross-/Kleinschreibung egal
};

class ManifestRegistry {
 public:
  static std::optional<SystemManifest> parse(const QByteArray& json, QString* error = nullptr);

  // Eingebaute Manifeste (Qt-Ressource :/framebeam/emulation/manifests/*.json).
  bool loadBuiltin(QString* error = nullptr);
  // Zusaetzliche Manifeste aus einem Verzeichnis (*.json).
  bool loadDirectory(const QString& dir, QString* error = nullptr);
  bool add(const SystemManifest& manifest, QString* error = nullptr);

  QList<SystemManifest> all() const { return m_manifests; }
  const SystemManifest* find(const QString& systemId) const;
  const SystemManifest* forExtension(const QString& ext) const;

 private:
  QList<SystemManifest> m_manifests;
};

}  // namespace framebeam::emu
