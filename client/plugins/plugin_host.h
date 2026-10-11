#pragma once

#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>

#include "manifest.h"
#include "plugin_memory.h"
#include "plugin_types.h"

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

  QStringList pluginIds() const;
  ConsoleSnapshot consoleSnapshot(const QString& id) const;

 signals:
  void statusChanged(const QString& id);
  // Console data changed (log, events, watched rows); rate-limited to about 10 per second per plugin.
  void consoleChanged(const QString& id);

 private:
  std::unique_ptr<ConsoleStore> m_store;
  std::atomic<int> m_tickHz{30};
  HostLimits m_limits;
  QThread* m_thread = nullptr;
  HostWorker* m_worker = nullptr;
};

}  // namespace framebeam::plugins
