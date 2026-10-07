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

  // Updates (spec 0.3 S6). Channel override: "" = compiled default, otherwise "stable" | "beta" (an old "test" reads as "beta").
  // Automatic install: unset = default of the effective channel (on for beta).
  QString updateChannel() const { return updateChannel_; }
  bool setUpdateChannel(const QString& channel);  // "", "stable", "beta" ("test" = alias); anything else is rejected (false)
  std::optional<bool> updateAutoInstall() const { return updateAutoInstall_; }
  bool setUpdateAutoInstall(bool on);

  // Diagnostics overlay (0.6 D5): open/closed state of the overlay and of its two sections, shared by the window,
  // multiview and fullscreen. Defaults: overlay closed, Emulation section open, Streaming section open.
  bool diagnosticsOpen() const { return diagOpen_; }
  bool diagnosticsEmulationOpen() const { return diagEmulationOpen_; }
  bool diagnosticsStreamingOpen() const { return diagStreamingOpen_; }
  bool setDiagnosticsOpen(bool open);
  bool setDiagnosticsEmulationOpen(bool open);
  bool setDiagnosticsStreamingOpen(bool open);

  // Chosen save slot per Hub profile and game (ADR 0012 D7); "default" when nothing valid is stored.
  QString saveSlot(const QString& hubId, const QString& gameId) const;
  bool setSaveSlot(const QString& hubId, const QString& gameId, const QString& slot);  // false: invalid name or not writable

 private:
  bool save() const;

  QString path_;
  QJsonObject raw_;
  Appearance appearance_ = Appearance::Dark;
  QString updateChannel_;
  std::optional<bool> updateAutoInstall_;
  bool diagOpen_ = false;
  bool diagEmulationOpen_ = true;
  bool diagStreamingOpen_ = true;
};

}  // namespace framebeam
