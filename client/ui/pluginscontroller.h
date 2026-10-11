#pragma once
// PluginsController: the plugin runtime inside the Player. Owns the PluginManager (<data>/plugins), the PluginHost and
// the memory adapter; starts the plugins that match a game when it runs, and serves Settings -> Plugins and the
// Plugin console tab of the game panel (docs/design/plugins.md 4a, 4d). Plugin failures never block or slow the game:
// they only show in the console and the Player log.
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "plugin_host.h"
#include "plugin_manager.h"
#include "pluginimageprovider.h"
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

  // Plugin UI v1 (fb.ui). Models are rebuilt on the UI thread when the plugin thread delivers changes (coalesced to
  // about 30 updates per second and slot); the trees themselves are fetched with slotTree() on slotChanged().
  // [{pluginId, title, icon, origin, dot, paused}]: in-game panel tabs of the running plugins that declare ui.panel.
  Q_PROPERTY(QVariantList panelTabs READ panelTabs NOTIFY panelTabsChanged)
  // [{pluginId, corner, hidden, tree, origin}]: HUD widgets (ui.hud) that have a tree; corner = top-left | top-right |
  // bottom-left | bottom-right (default top-right).
  Q_PROPERTY(QVariantList hudWidgets READ hudWidgets NOTIFY hudWidgetsChanged)
  // [{toastId, pluginId, pluginName, tone, title, text, actions: [{label, action, value}], origin}], oldest first, at most 4.
  Q_PROPERTY(QVariantList toasts READ toasts NOTIFY toastsChanged)
  // [{pluginId, title, icon, badge, origin}]: sidebar entries under Library (ui.screen) of the running plugins.
  Q_PROPERTY(QVariantList sidebarEntries READ sidebarEntries NOTIFY sidebarEntriesChanged)
  // [{pluginId, name, tree}]: sections of the game detail (ui.detail) that have a tree.
  Q_PROPERTY(QVariantList detailSections READ detailSections NOTIFY detailSectionsChanged)
  // [{pluginId, id, label, key}]: hotkeys declared by the running plugins.
  Q_PROPERTY(QVariantList hotkeys READ hotkeys NOTIFY hotkeysChanged)
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

  QVariantList panelTabs() const { return panelTabs_; }
  QVariantList hudWidgets() const { return hudWidgets_; }
  QVariantList toasts() const { return toasts_; }
  QVariantList sidebarEntries() const { return sidebarEntries_; }
  QVariantList detailSections() const { return detailSections_; }
  QVariantList hotkeys() const { return hotkeys_; }
  // Registry behind image://fbplugin (see pluginimageprovider.h).
  std::shared_ptr<PluginImageSources> imageSources() const { return imageSources_; }
  // Plugins running in the shell context (no game), in start order.
  QStringList shellPluginIds() const { return shellIds_; }
  bool shellRunning() const { return shellRunning_; }

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

  // Plugin UI.
  Q_INVOKABLE QVariantMap slotTree(const QString& pluginId, const QString& slot) const;
  Q_INVOKABLE QVariantList tileStrips(const QString& tileId) const;  // [{pluginId, tree}]
  // Forwards a UI action (button, toggle, select, list row, map marker, toast action slot "toast", hotkey slot "hotkey")
  // to the plugin's fb.ui.on_action handler.
  Q_INVOKABLE void sendAction(const QString& pluginId, const QString& slot, const QString& action, const QVariant& value = QVariant());
  // The game detail pane shows this game (empty system = none): clears the "detail" slots and sends fb/detail.game.
  Q_INVOKABLE void setDetailGame(const QString& system, const QString& code, const QString& sha256, const QString& title);
  // Visible remote Multiview tiles [{id, player, game}] -> fb/tiles.
  Q_INVOKABLE void setTiles(const QVariantList& tiles);
  Q_INVOKABLE void setHudCorner(const QString& pluginId, const QString& corner);
  Q_INVOKABLE void hideHud(const QString& pluginId);
  Q_INVOKABLE void showHud(const QString& pluginId);
  Q_INVOKABLE void dismissToast(int toastId);
  // mode: "session" (until the game ends), "always" (until Player restart) or "off".
  Q_INVOKABLE void muteToasts(const QString& pluginId, const QString& mode);
  Q_INVOKABLE void triggerHotkey(const QString& pluginId, const QString& hotkeyId);

  // Tests and screenshots: shows `console` in the Plugin console without a running game; empty map removes it.
  void setPreviewConsole(const QVariantMap& console);

 signals:
  void developerModeChanged();
  void pluginsChanged();
  void messageChanged();
  void consoleAvailableChanged();
  void consoleChanged();
  void panelTabsChanged();
  void hudWidgetsChanged();
  void toastsChanged();
  void sidebarEntriesChanged();
  void detailSectionsChanged();
  void hotkeysChanged();
  void tileStripsChanged();
  // The tree of a slot was set, patched or cleared (fetch it with slotTree()).
  void slotChanged(const QString& pluginId, const QString& slot);

 private:
  void updateShell();
  void stopGame(bool restartShell);
  void postInitialEvents();
  void onUiChanged(const QString& id, const QString& what);
  void rebuildUiModels();
  void rebuildPanelTabs();
  void rebuildHud();
  void rebuildSidebar();
  void rebuildDetail();
  void rebuildHotkeys();
  void collectToasts();
  QStringList activeIds() const;
  void updateImageSources();
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
  bool running_ = false;                      // plugins of the running game are started
  bool inGame_ = false;                       // a game runs (with or without plugins)
  bool shellRunning_ = false;
  QStringList shellIds_;
  QString shellSignature_;
  QString language_ = QStringLiteral("en");
  QVariantList panelTabs_, hudWidgets_, toasts_, sidebarEntries_, detailSections_, hotkeys_;
  QHash<QString, QString> hudCorner_;
  QSet<QString> hudHidden_;
  QSet<QString> mutedSession_, mutedAlways_;
  QVariantMap detailGame_;                    // empty = none
  QVariantList tiles_;
  std::shared_ptr<PluginImageSources> imageSources_ = std::make_shared<PluginImageSources>();
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
