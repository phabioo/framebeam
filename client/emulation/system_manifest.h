#pragma once
// Data-driven system manifests (manifests/systems/<system>.json) and core profiles (manifests/cores/<core_id>.json),
// ADR 0020 D6. No console-specific launch logic outside this data. A SystemManifest returned by
// ManifestRegistry::find() describes the system only; ManifestRegistry::resolve(system, core) returns the
// "effective" manifest for a core: the system data plus the core fields (coreId, library, option defaults, locks,
// firmware option mapping). A core without profile resolves to an experimental manifest (no defaults or locks,
// raw single-screen display, touch off, firmware only placed into the system directory).

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
  QString coreOption;  // core option that receives `name` (e.g. melonds_firmware_nds_path); empty = none (set by the core profile)
};

// Whether firmware is needed is decided by the Hub per system (mode builtin|native); the manifest only says
// which files the core reads, under which names, and which core option switches between built-in and native files.
struct FirmwareSpec {
  bool required = false;  // static override: true = no start without the files (the Hub mode normally decides)
  QList<FirmwareFile> files;
  QString sysfileOption;                       // e.g. melonds_sysfile_mode; empty = the core has no such switch (core profile)
  QString sysfileNative = QStringLiteral("native");
  QString sysfileBuiltin = QStringLiteral("builtin");

  const FirmwareFile* fileById(const QString& id) const;
};

struct SystemManifest {
  QString systemId;
  QString displayName;
  // Effective core (empty on the system-only manifest returned by find()):
  QString coreId;
  QString coreDisplayName;  // shown when the core was not probed; defaults to coreId
  QString coreLibraryBasename;
  QStringList coreAliases;  // legacy ids of the core (e.g. melonds_ds)
  bool experimental = false;  // core without profile
  // Save handling of the effective core (from its profile); "auto" = experimental core: SAVE_RAM when the core
  // reports it, else whatever single file the core writes (today's behavior).
  QString saveSource = QStringLiteral("auto");
  QString saveExtension = QStringLiteral(".sav");
  QString saveFormat = QStringLiteral("raw");
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

// Per-core data (manifests/cores/<core_id>.json): how FrameBeam drives one libretro core.
struct CoreProfile {
  QString coreId;
  QStringList aliases;  // legacy ids, e.g. "melonds_ds" for "melondsds"
  QString displayName;
  QString libraryBasename;  // e.g. melondsds_libretro (no platform extension)
  QStringList systemIds;
  int order = 100;  // listing order and offline default (lower first), then by core id
  QMap<QString, QString> coreOptions;  // defaults
  QStringList lockedCoreOptions;
  QStringList alwaysShownCoreOptions;
  // Where the cartridge save lives (ADR 0020 D7): "save_ram" = libretro SAVE_RAM, persisted by the Player as
  // <rom basename><extension> (raw); "core_file" = the core manages its own file <rom basename><extension> in the
  // save directory in `saveFormat` ("desmume_dsv"); the Player converts around the core. The Hub stores raw saves only.
  QString saveSource = QStringLiteral("save_ram");
  QString saveExtension = QStringLiteral(".sav");
  QString saveFormat = QStringLiteral("raw");
  QString sysfileOption;  // core option that switches built-in / external BIOS; empty = none
  QString sysfileNative = QStringLiteral("native");
  QString sysfileBuiltin = QStringLiteral("builtin");
  QMap<QString, QString> fileOptions;  // firmware file id -> core option that receives the file name

  bool matches(const QString& id) const { return id == coreId || aliases.contains(id); }
};

class ManifestRegistry {
 public:
  static std::optional<SystemManifest> parse(const QByteArray& json, QString* error = nullptr);
  static std::optional<CoreProfile> parseProfile(const QByteArray& json, QString* error = nullptr);

  // Built-in manifests (Qt resource :/framebeam/emulation/manifests/{systems,cores}/*.json).
  bool loadBuiltin(QString* error = nullptr);
  // Additional manifests from a directory holding systems/*.json and cores/*.json (either may be missing).
  bool loadDirectory(const QString& dir, QString* error = nullptr);
  bool add(const SystemManifest& manifest, QString* error = nullptr);
  bool addProfile(const CoreProfile& profile, QString* error = nullptr);

  QList<SystemManifest> all() const { return m_manifests; }
  const SystemManifest* find(const QString& systemId) const;
  const SystemManifest* forExtension(const QString& ext) const;

  // Core profile by core id or legacy alias; nullptr = unknown core (experimental).
  const CoreProfile* profile(const QString& coreIdOrAlias) const;
  QList<CoreProfile> profilesForSystem(const QString& systemId) const;
  // Canonical core id for an id or alias (unknown ids are returned unchanged).
  QString canonicalCoreId(const QString& coreIdOrAlias) const;
  // Effective manifest of `systemId` for `coreId` (id or alias). Unknown core = experimental. nullopt = unknown system.
  std::optional<SystemManifest> resolve(const QString& systemId, const QString& coreId) const;

 private:
  QList<SystemManifest> m_manifests;
  QList<CoreProfile> m_profiles;
};

}  // namespace framebeam::emu
