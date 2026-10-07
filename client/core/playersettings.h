#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

namespace framebeam {

// Local Player settings: <data>/settings/player.json (not hub-specific, never contains a secret).
// Unknown keys are preserved when saving.
class PlayerSettings {
 public:
  enum class Appearance { Dark, Light, System };

  // baseDir = data directory (ProfileStore::baseDir()). A missing or corrupted file yields the defaults.
  explicit PlayerSettings(const QString& baseDir);

  static QString appearanceName(Appearance a);  // "dark" | "light" | "system"
  static Appearance parseAppearance(const QString& name, Appearance fallback = Appearance::Dark);

  QString filePath() const { return path_; }
  Appearance appearance() const { return appearance_; }
  bool setAppearance(Appearance a);  // persists immediately; false if the file could not be written

  // Updates (spec 0.3 S6). Channel override: "" = compiled default, otherwise "stable" | "test".
  // Automatic install: unset = default of the effective channel (on for test).
  QString updateChannel() const { return updateChannel_; }
  bool setUpdateChannel(const QString& channel);  // "", "stable", "test"; anything else is rejected (false)
  std::optional<bool> updateAutoInstall() const { return updateAutoInstall_; }
  bool setUpdateAutoInstall(bool on);

 private:
  bool save() const;

  QString path_;
  QJsonObject raw_;
  Appearance appearance_ = Appearance::Dark;
  QString updateChannel_;
  std::optional<bool> updateAutoInstall_;
};

}  // namespace framebeam
