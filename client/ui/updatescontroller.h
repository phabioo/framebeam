#pragma once
// UpdatesController: QML face of the Player updater (Settings "Updates" section and the update banner).

#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
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

  // Update rollback (ADR 0029 D4; design 6c): EARLIER VERSIONS list, the pause card after going back.
  // earlierVersions: [{version, kind: "skipped" | "running" | "earlier", text, notesUrl}] (the skipped release, the running
  // version, then at most 5 older releases, newest first); empty until a check succeeded.
  Q_PROPERTY(QVariantList earlierVersions READ earlierVersions NOTIFY changed)
  Q_PROPERTY(int earlierCount READ earlierCount NOTIFY changed)
  Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)  // this copy can install an older version itself
  Q_PROPERTY(QString goBackUnavailableText READ goBackUnavailableText NOTIFY changed)  // why not (links only)
  Q_PROPERTY(bool paused READ paused NOTIFY changed)        // automatic updates paused after going back
  Q_PROPERTY(QString pausedTitle READ pausedTitle NOTIFY changed)
  Q_PROPERTY(QString pausedDetail READ pausedDetail NOTIFY changed)
  Q_PROPERTY(QString skippedVersion READ skippedVersion NOTIFY changed)  // went back from this version; "" = none
  Q_PROPERTY(bool busy READ busy NOTIFY changed)             // a game or Session runs: going back is refused

 public:
  struct Options {
    QString baseDir;
    QString indexUrl;  // empty: env / default
    QString installRoot;
    bool forceApplySupport = false;
    int protocolVersion = 0;  // Hub protocol of this Player; a version that does not speak it is not offered
  };
  UpdatesController(const Options& options, PlayerSettings* settings, QObject* parent = nullptr);

  update::UpdateManager* manager() { return &manager_; }
  void setProviders(std::function<std::optional<update::HubProtocol>()> hub, std::function<bool()> busy);
  // Plugins that are on and need a Player newer than a version: {name, min_player} (the dialog warns about them).
  void setPluginNeedsProvider(std::function<QList<QPair<QString, QString>>(const QString& version)> f) { pluginNeeds_ = std::move(f); }

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
  QVariantList earlierVersions() const;
  int earlierCount() const { return static_cast<int>(manager_.rollbackOptions().size()); }
  bool canGoBack() const { return manager_.applySupported(); }
  QString goBackUnavailableText() const;
  bool paused() const { return manager_.updatesPaused(); }
  QString pausedTitle() const;
  QString pausedDetail() const;
  QString skippedVersion() const { return manager_.skippedVersion(); }
  bool busy() const { return manager_.busy(); }

  Q_INVOKABLE void checkNow() { manager_.checkNow(); }
  Q_INVOKABLE void setChannel(const QString& channel);  // "default" | "stable" | "beta"
  Q_INVOKABLE void setAutoInstall(bool on);
  Q_INVOKABLE void install() { manager_.installNow(); }
  Q_INVOKABLE void openNotes();
  Q_INVOKABLE void dismissBanner();
  // Dialog "Go back to <version>?": {version, canGoBack, busy, busyText, pluginLines: ["X needs 0.16.4 and is turned off ..."]}.
  Q_INVOKABLE QVariantMap rollbackInfo(const QString& version) const;
  Q_INVOKABLE void goBack(const QString& version) { manager_.rollbackTo(version); }
  Q_INVOKABLE void resumeUpdates() { manager_.resumeUpdates(); }
  Q_INVOKABLE void updateToSkipped() { manager_.installNow(); }  // "Update to X" (the skip ends)
  Q_INVOKABLE void openRelease(const QString& url);               // https only

 signals:
  void changed();
  void quitRequested();

 private:
  update::UpdateManager manager_;
  PlayerSettings* settings_;
  QString dismissedVersion_;
  std::function<QList<QPair<QString, QString>>(const QString&)> pluginNeeds_;
};

}  // namespace framebeam::ui
