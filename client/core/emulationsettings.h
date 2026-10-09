#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace framebeam {

// Local emulation settings: <data>/settings/emulation.json (never hub-specific, never contains a secret).
//
//   { "global":  { "<key>": "<value>", ... },
//     "systems": { "nds": { "options": { "<key>": "<value>" } } },
//     "games":   { "<game id>": { "<key>": "<value>" } } }
//
// The core choice (ADR 0020 D6) is the FrameBeam key kCoreKey ("framebeam.core") at system level (options) and at game
// level; effective core = game > system > the Hub's default core (see corechoice.h).
//
// Only explicit overrides are stored (partial, no full copies). Effective value =
// game > system/core > global > manifest default > core default; removing a key restores inheritance.
// Keys of the core stay assigned to the core; FrameBeam's own keys start with "framebeam.".
// Unknown content of the file is preserved when saving. A missing or corrupted file yields no overrides.
class EmulationSettings {
 public:
  static constexpr const char* kCoreKey = "framebeam.core";  // core id chosen for a system / a game
  enum class Level { Global, System, Game };
  enum class Source { Game, System, Global, Manifest, Core };

  struct Resolved {
    QString value;
    Source source = Source::Core;
  };

  explicit EmulationSettings(const QString& baseDir);

  QString filePath() const { return path_; }

  // scope: system id (Level::System), game id (Level::Game), ignored for Level::Global.
  bool hasValue(Level level, const QString& scope, const QString& key) const;
  QString value(Level level, const QString& scope, const QString& key) const;  // null QString if not set
  QMap<QString, QString> values(Level level, const QString& scope) const;
  // Persist immediately; false if the file could not be written (the in-memory value is kept).
  bool setValue(Level level, const QString& scope, const QString& key, const QString& value);
  bool removeValue(Level level, const QString& scope, const QString& key);  // reset: inheritance restored

  // Hierarchy: game > system > global > manifestDefault > coreDefault (a null QString = not available).
  Resolved resolve(const QString& key, const QString& systemId, const QString& gameId, const QString& manifestDefault,
                   const QString& coreDefault) const;
  // Explicit overrides only, merged with the more specific level winning (no defaults).
  QMap<QString, QString> mergedOverrides(const QString& systemId, const QString& gameId) const;

  static QString sourceName(Source s);  // "game" | "system" | "global" | "manifest" | "core"

 private:
  const QJsonObject levelValue(Level level, const QString& scope) const;
  bool save() const;

  QString path_;
  QJsonObject raw_;
};

}  // namespace framebeam
