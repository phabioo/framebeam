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

#include "plugin_host.h"
#include "plugin_manager.h"
#include "pluginruntime.h"

namespace framebeam {
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
  // Installed plugins: [{id, name, version, origin, originText, problemsText, hasProblems, enabled, devLinked, canToggle}]
  Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
  Q_PROPERTY(QString summary READ summary NOTIFY pluginsChanged)  // settings column line: "None installed" | "2 on" | "No plugin on"
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)  // result of the last install / link
  Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
  // Plugin console (in-game tab): only while developer mode is on and plugins run for the game.
  Q_PROPERTY(bool consoleAvailable READ consoleAvailable NOTIFY consoleAvailableChanged)
  // {plugins: [{id, name, status, statusText}], selected, status, statusText, error, watched, events, log, regions,
  //  regionsText}; refreshed at most about 10 times per second.
  Q_PROPERTY(QVariantMap console READ console NOTIFY consoleChanged)
 public:
  // dataDir = ProfileStore::baseDir(); the plugins live in <dataDir>/plugins.
  PluginsController(PlayerSettings* settings, const QString& dataDir, GameSession* session, QObject* parent = nullptr);
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

  // A game runs (GameSession::started): resolves the plugins for it and starts them. `language` = Player UI language.
  void gameStarted(const QString& system, const QString& romPath, const QString& sha256, const QString& title,
                   const QString& language = QStringLiteral("en"));
  // The game ended or failed: stops all plugins.
  void gameStopped();
  bool pluginsRunning() const { return running_; }

  // Settings -> Plugins. Paths may be file:// URLs (folder dialog).
  Q_INVOKABLE void installFromFolder(const QString& pathOrUrl);
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
  void messageChanged();
  void consoleAvailableChanged();
  void consoleChanged();

 private:
  void refreshPlugins();
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
