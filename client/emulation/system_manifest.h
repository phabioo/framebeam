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
  QString id;          // id of the file in the Hub registry (e.g. "bios7"); defaults to the file name without extension
  QString name;        // file name in the core's system directory (the Player materializes it there)
  bool required = false;
  QString coreOption;  // core option that receives `name` (e.g. melonds_firmware_nds_path); empty = none
};

// Whether firmware is needed is decided by the Hub per system (mode builtin|native); the manifest only says
// which files the core reads, under which names, and which core option switches between built-in and native files.
struct FirmwareSpec {
  bool required = false;  // static override: true = no start without the files (the Hub mode normally decides)
  QList<FirmwareFile> files;
  QString sysfileOption;                       // e.g. melonds_sysfile_mode; empty = the core has no such switch
  QString sysfileNative = QStringLiteral("native");
  QString sysfileBuiltin = QStringLiteral("builtin");

  const FirmwareFile* fileById(const QString& id) const;
};

struct SystemManifest {
  QString systemId;
  QString displayName;
  QString coreId;
  QString coreDisplayName;  // optional ("core_display_name"), shown when the core was not probed; defaults to coreId
  QString coreLibraryBasename;
  QStringList extensions;  // lowercase, with dot
  FirmwareSpec firmware;
  QString inputProfile;
  QString displayProfile;
  // Short UI labels from the manifest ("labels" object): input column header and the pointer/touch input.
  QString inputLabel;  // e.g. "NDS"; defaults to displayName
  QString touchLabel;  // e.g. "DS touch"; defaults to "Touch"
  DisplayProfile display;
  QMap<QString, QString> coreOptions;  // defaults for core options (key -> value)
  QStringList lockedCoreOptions;       // keys of coreOptions that FrameBeam controls: not editable by the user
  QStringList alwaysShownCoreOptions;  // listed on the Emulation page even if the core hides them in its default state

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
