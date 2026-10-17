#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <atomic>
#include <memory>

#include "manifest.h"
#include "plugin_memory.h"
#include "plugin_types.h"
#include "plugin_ui.h"

class QThread;

namespace framebeam::plugins {

class ConsoleStore;
class PluginEngine;
class HostWorker;

// Runs plugins on one dedicated plugin thread (never the UI or emulation thread): one Lua state and budget per plugin.
// A Lua error, budget or memory overrun pauses only that plugin; the game and the other plugins continue.
// All public methods are called from the owner's (UI) thread, consoleSnapshot() from any thread.
class PluginHost : public QObject {
  Q_OBJECT
 public:
  explicit PluginHost(QObject* parent = nullptr);
  ~PluginHost() override;

  // Starts `plugins` (already in dependency order, see PluginManager::resolveForGame) for a game. Replaces a running
  // set. Statuses are Loading immediately and become Running/Paused on the plugin thread.
  void start(const GameContext& ctx, std::shared_ptr<MemoryProvider> memory, const QList<PluginInfo>& plugins);
  // Stops all plugins (blocks until the plugin thread is done), unwatches their ranges.
  void stop();
  // Re-reads the manifest and entry file of one plugin and restarts its Lua state.
  void reload(const QString& id);
  // Maximum on_tick rate per plugin, 1..240, default 30.
  void setTickHz(int hz);
  // Budgets for plugins started or reloaded afterwards.
  void setLimits(const HostLimits& limits);

  // --- plugin UI (fb.ui), see plugin_ui.h ---
  // An action of the UI (button, toggle, list row, toast action, hotkey ...): calls the plugin's fb.ui.on_action handler
  // on the plugin thread with (action, value, slot). Ignored when the plugin has no handler or is not running.
  void sendAction(const QString& pluginId, const QString& slot, const QString& action, const QVariant& value = QVariant());
  // Host event with source plugin id "fb" (fb.events.on("fb", name, fn)); `targetPlugin` empty = all subscribers.
  void postHostEvent(const QString& name, const QVariant& value, const QString& targetPlugin = QString());
  // Removes the tree of `slot` from every plugin (the host clears "detail" when the shown game changes).
  void clearSlot(const QString& slot);
  UiPluginState uiState(const QString& pluginId) const;
  QVariantMap uiTree(const QString& pluginId, const QString& slot) const;
  QList<QVariantMap> takeToasts();
  QList<QPair<QString, QString>> takeOpenRequests();  // fb.ui.open requests (plugin id, slot)

  QStringList pluginIds() const;
  ConsoleSnapshot consoleSnapshot(const QString& id) const;

 signals:
  void statusChanged(const QString& id);
  // Console data changed (log, events, watched rows); rate-limited to about 10 per second per plugin.
  void consoleChanged(const QString& id);
  // UI data changed: `what` is a slot name, "@meta" (title, badge, dot, paused), "@toast" or "@open". Coalesced: at most one
  // emission per slot about every 33 ms (30 updates per second).
  void uiChanged(const QString& id, const QString& what);

 private:
  std::unique_ptr<ConsoleStore> m_store;
  std::atomic<int> m_tickHz{30};
  HostLimits m_limits;
  QThread* m_thread = nullptr;
  HostWorker* m_worker = nullptr;
};

}  // namespace framebeam::plugins
