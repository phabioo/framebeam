#pragma once
// UpdatesController: QML face of the Player updater (Settings "Updates" section and the update banner).

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "playersettings.h"
#include "updater.h"

namespace framebeam::ui {

class UpdatesController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by PlayerController")

  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(QString compiledChannel READ compiledChannel CONSTANT)
  Q_PROPERTY(QString channelSetting READ channelSetting NOTIFY changed)  // default | stable | beta
  Q_PROPERTY(QString effectiveChannel READ effectiveChannel NOTIFY changed)  // off | stable | beta
  Q_PROPERTY(bool autoInstall READ autoInstall NOTIFY changed)           // effective for the channel
  Q_PROPERTY(bool autoInstallAvailable READ autoInstallAvailable NOTIFY changed)  // only on the beta channel
  Q_PROPERTY(QString state READ state NOTIFY changed)  // idle|disabled|checking|up_to_date|available|incompatible|downloading|ready|applying|error
  Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
  Q_PROPERTY(QString lastCheckText READ lastCheckText NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY changed)
  Q_PROPERTY(QString notesUrl READ notesUrl NOTIFY changed)
  Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
  // Sidebar dot on Settings (3p): a newer Player is known (available, downloading or ready to install).
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
  // "checks every hour" (Beta) / "checks every 24 hours" (Stable) while the Player runs, empty when updates are off.
  Q_PROPERTY(QString cadenceText READ cadenceText NOTIFY changed)
  // Banner (S6): text empty = hidden. Action: install ("Install and restart") | restart ("Restart to update") | link | none.
  Q_PROPERTY(QString bannerText READ bannerText NOTIFY changed)
  Q_PROPERTY(QString bannerAction READ bannerAction NOTIFY changed)
  Q_PROPERTY(QString bannerActionLabel READ bannerActionLabel NOTIFY changed)

 public:
  struct Options {
    QString baseDir;
    QString indexUrl;  // empty: env / default
    QString installRoot;
    bool forceApplySupport = false;
  };
  UpdatesController(const Options& options, PlayerSettings* settings, QObject* parent = nullptr);

  update::UpdateManager* manager() { return &manager_; }
  void setProviders(std::function<std::optional<update::HubProtocol>()> hub, std::function<bool()> busy);

  QString currentVersion() const { return manager_.currentVersion(); }
  QString compiledChannel() const;
  QString channelSetting() const;
  QString effectiveChannel() const;
  bool autoInstall() const { return manager_.autoInstallSetting(); }
  bool autoInstallAvailable() const { return manager_.effectiveChannel() == update::Channel::Beta; }
  QString state() const;
  QString statusText() const;
  QString lastCheckText() const;
  double progress() const { return manager_.progress(); }
  QString availableVersion() const { return manager_.availableVersion(); }
  QString notesUrl() const { return manager_.notesUrl(); }
  bool canInstall() const;
  bool checking() const { return manager_.state() == update::UpdateManager::State::Checking; }
  bool updateAvailable() const;
  QString cadenceText() const;
  QString bannerText() const;
  QString bannerAction() const;
  QString bannerActionLabel() const;

  Q_INVOKABLE void checkNow() { manager_.checkNow(); }
  Q_INVOKABLE void setChannel(const QString& channel);  // "default" | "stable" | "beta"
  Q_INVOKABLE void setAutoInstall(bool on);
  Q_INVOKABLE void install() { manager_.installNow(); }
  Q_INVOKABLE void openNotes();
  Q_INVOKABLE void dismissBanner();

 signals:
  void changed();
  void quitRequested();

 private:
  update::UpdateManager manager_;
  PlayerSettings* settings_;
  QString dismissedVersion_;
};

}  // namespace framebeam::ui
