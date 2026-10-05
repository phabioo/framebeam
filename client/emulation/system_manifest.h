#pragma once
// Data-driven system manifests (e.g. manifests/nds.json). No console-specific
// launch logic outside this data: core mapping, extensions, firmware, input/display profile
// and core option defaults come from the manifest.

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
  QString id;  // e.g. "top", "bottom"
  int width = 0;
  int height = 0;
  bool touch = false;  // touch/pointer input goes to this screen
};

// Arrangement of the screens in the full frame delivered by the core.
struct DisplayProfile {
  QString layout;  // "single" | "vertical" (stacked) | "horizontal" (side by side)
  int gap = 0;     // pixels between the screens
  QList<ScreenSpec> screens;

  QSize frameSize() const;
  QRect screenRect(int index) const;  // pixel rectangle of the screen in the full frame
  int touchScreenIndex() const;       // -1 = no touch screen
  // Normalized position (0..1) on the screen -> normalized position in the full frame
  // (input for EmulatorBackend::setPointer).
  QPointF toFrameNormalized(int screenIndex, QPointF screenNormalized) const;
};

struct FirmwareFile {
  QString name;
  bool required = false;
};

struct FirmwareSpec {
  bool required = false;  // true: no start without firmware ("Firmware required/missing")
  QList<FirmwareFile> files;
};

struct SystemManifest {
  QString systemId;
  QString displayName;
  QString coreId;
  QString coreLibraryBasename;
  QStringList extensions;  // lowercase, with dot
  FirmwareSpec firmware;
  QString inputProfile;
  QString displayProfile;
  DisplayProfile display;
  QMap<QString, QString> coreOptions;  // defaults for core options (key -> value)

  bool supportsExtension(const QString& ext) const;  // ".nds" or "nds", case-insensitive
};

class ManifestRegistry {
 public:
  static std::optional<SystemManifest> parse(const QByteArray& json, QString* error = nullptr);

  // Built-in manifests (Qt resource :/framebeam/emulation/manifests/*.json).
  bool loadBuiltin(QString* error = nullptr);
  // Additional manifests from a directory (*.json).
  bool loadDirectory(const QString& dir, QString* error = nullptr);
  bool add(const SystemManifest& manifest, QString* error = nullptr);

  QList<SystemManifest> all() const { return m_manifests; }
  const SystemManifest* find(const QString& systemId) const;
  const SystemManifest* forExtension(const QString& ext) const;

 private:
  QList<SystemManifest> m_manifests;
};

}  // namespace framebeam::emu
