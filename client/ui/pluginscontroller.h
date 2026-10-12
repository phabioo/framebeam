#pragma once
// PluginsController: the plugin runtime inside the Player. Owns the PluginManager (<data>/plugins), the PluginHost and
// the memory adapter; starts the plugins that match a game when it runs, and serves Settings -> Plugins and the
// Plugin console tab of the game panel (docs/design/plugins.md 4a, 4d). Plugin failures never block or slow the game:
// they only show in the console and the Player log.
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "plugin_delivery.h"
#include "plugin_host.h"
#include "plugin_manager.h"
#include "pluginhub.h"
#include "pluginruntime.h"

namespace framebeam {
class HubConnection;
class PlayerSettings;
}

namespace framebeam::ui {

class GameSession;

class PluginsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  // Developer mode (persisted in PlayerSettings, applied to the manager).
  Q_PROPERTY(bool developerMode READ developerMode WRITE setDeveloperMode NOTIFY developerModeChanged)
  // Installed plugins: [{id, name, version, origin ("official"|"unverified"|"local"), originText ("✓ Official" | "! Hub upload ·
  //  unverified" | "Local file"), problemsText, hasProblems, enabled, devLinked, hubCopy, canToggle, needsConfirm (unverified: the
  //  warning comes before turning it on), permissionsText, updateVersion ("" when none)}]
  Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
  Q_PROPERTY(QString summary READ summary NOTIFY pluginsChanged)  // settings column line: "None installed" | "2 on · 1 update" | "None on"
  // Plugins the Hub offers (feature plugins_v1): the newest offered version of every plugin, [{id, name, version,
  // description, origin, originText, permissionsText, sizeText, state ("install"|"update"|"installed"|"busy"), installedVersion}].
  Q_PROPERTY(bool hubSupported READ hubSupported NOTIFY hubPluginsChanged)
  Q_PROPERTY(QVariantList hubPlugins READ hubPlugins NOTIFY hubPluginsChanged)
  Q_PROPERTY(QString hubStatus READ hubStatus NOTIFY hubPluginsChanged)  // idle | loading | ready | failed
  Q_PROPERTY(QString hubError READ hubError NOTIFY hubPluginsChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)  // result of the last install / link
  Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
  // Plugin console (in-game tab): only while developer mode is on and plugins run for the game.
  Q_PROPERTY(bool consoleAvailable READ consoleAvailable NOTIFY consoleAvailableChanged)
  // {plugins: [{id, name, status, statusText}], selected, status, statusText, error, watched, events, log, regions,
  //  regionsText}; refreshed at most about 10 times per second.
  Q_PROPERTY(QVariantMap console READ console NOTIFY consoleChanged)
 public:
  // dataDir = ProfileStore::baseDir(); the plugins live in <dataDir>/plugins.
  // `connection` = the active Hub (plugin list and downloads); nullptr = no Hub features (tests).
  PluginsController(PlayerSettings* settings, const QString& dataDir, GameSession* session, HubConnection* connection = nullptr,
                    QObject* parent = nullptr);
  ~PluginsController() override;

  bool developerMode() const;
  void setDeveloperMode(bool on);
  QVariantList plugins() const { return plugins_; }
  QString summary() const { return summary_; }
  QString message() const { return message_; }
  bool messageIsError() const { return messageIsError_; }
  bool consoleAvailable() const { return consoleAvailable_; }
  QVariantMap console() const { return console_; }

  plugins::PluginManager* manager() { return manager_.get(); }
  plugins::PluginHost* host() { return host_.get(); }
  // Hub delivery: ensureHubCopy(id, version, sha256, cb) gives the runs the exact Hub copy of a plugin version.
  plugins::PluginDelivery* delivery() { return delivery_.get(); }
  PluginHubClient* hubClient() { return hubClient_.get(); }
  bool hubSupported() const { return hubClient_ != nullptr && hubClient_->supported(); }
  QVariantList hubPlugins() const { return hubPlugins_; }
  QString hubStatus() const;
  QString hubError() const { return hubClient_ != nullptr ? hubClient_->errorMessage() : QString(); }

  // A game runs (GameSession::started): resolves the plugins for it and starts them. `language` = Player UI language.
  void gameStarted(const QString& system, const QString& romPath, const QString& sha256, const QString& title,
                   const QString& language = QStringLiteral("en"));
  // The game ended or failed: stops all plugins.
  void gameStopped();
  bool pluginsRunning() const { return running_; }

  // Settings -> Plugins. Paths may be file:// URLs (file or folder dialog).
  Q_INVOKABLE void installFromFolder(const QString& pathOrUrl);
  // A .fbplugin file or a plugin folder, whichever the path is. Signed (valid FrameBeam key) = Official, otherwise a local
  // plugin that needs Developer mode.
  Q_INVOKABLE void installFromPath(const QString& pathOrUrl);
  // Asks the Hub for its plugin list (no-op without plugins_v1).
  Q_INVOKABLE void refreshHubPlugins();
  // Downloads, checks (SHA-256 pin, zip rules, signature) and installs an offered version; it stays off until turned on.
  Q_INVOKABLE void installHubPlugin(const QString& id, const QString& version);
  Q_INVOKABLE void linkDevFolder(const QString& pathOrUrl);
  Q_INVOKABLE void uninstall(const QString& id);
  Q_INVOKABLE void setPluginEnabled(const QString& id, bool enabled);
  Q_INVOKABLE void dismissMessage();
  // Console tab.
  Q_INVOKABLE void selectConsolePlugin(const QString& id);
  Q_INVOKABLE void reloadPlugin(const QString& id);   // "↻ Reload" and "Restart plugin"
  Q_INVOKABLE void reloadSelected();                   // Ctrl+Shift+R
  Q_INVOKABLE void clearLog(const QString& id);
  Q_INVOKABLE void copyLog(const QString& id);

  // Tests and screenshots: shows `console` in the Plugin console without a running game; empty map removes it.
  void setPreviewConsole(const QVariantMap& console);

 signals:
  void developerModeChanged();
  void pluginsChanged();
  void hubPluginsChanged();
  void messageChanged();
  void consoleAvailableChanged();
  void consoleChanged();

 private:
  void refreshPlugins();
  void rebuildHubPlugins();
  void onDelivered(const plugins::DeliveryResult& r);
  void reportInstall(const plugins::InstallResult& r, bool fromFolder);
  const HubPluginInfo* newestOffer(const QString& id) const;
  void scheduleConsole();
  void rebuildConsole();
  void onStatus(const QString& id);
  void setMessage(const QString& text, bool isError);
  QString visibleLogText(const QString& id) const;
  void setConsoleAvailable(bool on);
  static QString localPath(const QString& pathOrUrl);

  PlayerSettings* settings_;
  GameSession* session_;
  std::unique_ptr<plugins::PluginManager> manager_;
  std::unique_ptr<plugins::PluginHost> host_;
  std::unique_ptr<PluginHubClient> hubClient_;
  std::unique_ptr<plugins::PluginDelivery> delivery_;
  QVariantList hubPlugins_;
  QString hubBusyId_;
  std::shared_ptr<MemoryWatchAdapter> adapter_;
  QTimer consoleTimer_;
  bool running_ = false;
  bool consoleAvailable_ = false;
  QStringList runningIds_;                    // start order of the running plugins
  QString selected_;
  QHash<QString, qint64> clearedAt_;          // plugin id -> log lines up to this time are hidden
  QHash<QString, int> loggedStatus_;          // plugin id -> last status written to the Player log
  QVariantList plugins_;
  QString summary_;
  QString message_;
  bool messageIsError_ = false;
  QVariantMap console_;
  QVariantMap preview_;
  bool hasPreview_ = false;
};

}  // namespace framebeam::ui
