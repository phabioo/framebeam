#pragma once

// Internal: the single-threaded plugin engine. All methods except ConsoleStore's run on the plugin thread.
#include <QFile>
#include <QHash>
#include <QMap>
#include <QSet>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "manifest.h"
#include "plugin_memory.h"
#include "plugin_types.h"
#include "plugin_value.h"

namespace framebeam::plugins {

// Thread-safe console data shared with the UI thread.
class ConsoleStore {
 public:
  void reset(const QStringList& ids, PluginStatus status);
  void setStatus(const QString& id, PluginStatus status, const QString& error);
  void addLog(const QString& id, LogLevel level, const QString& text);
  void addEvent(const QString& id, const EventLine& line);
  bool setWatched(const QString& id, const QList<WatchedRow>& rows);  // true when changed
  ConsoleSnapshot snapshot(const QString& id) const;
  QStringList ids() const;
  QStringList takeDirty();

 private:
  mutable std::mutex m_mutex;
  QMap<QString, ConsoleSnapshot> m_map;
  QStringList m_order;
  QSet<QString> m_dirty;
};

class PluginEngine;

struct DevWatch {
  QString label;
  uint64_t addr = 0;
  QString kind;  // u8 u16 u32 s32 hex
  uint32_t size = 0;
};

struct EventSub {
  QString plugin;  // "*" = any
  QString name;
  int ref = 0;
};

struct Plugin {
  PluginInfo info;
  PluginEngine* engine = nullptr;
  std::unique_ptr<LuaSandbox> lua;
  PluginStatus status = PluginStatus::Loading;
  int tickRef = -2;  // LUA_NOREF
  std::vector<EventSub> subs;
  std::map<std::string, int> provided;  // interface name -> registry ref of its table
  std::map<std::string, WatchRange> watches;
  std::vector<DevWatch> devWatches;
  std::unique_ptr<QFile> rom;
  QHash<QString, std::optional<QHash<QString, QString>>> i18n;  // language -> strings (nullopt = no such file)
  qint64 cacheBytes = -1;
  QList<WatchedRow> lastRows;

  QString id() const { return info.manifest.id; }
};

struct QueuedEvent {
  QString from;
  std::string name;
  Value value;
};

class PluginEngine {
 public:
  PluginEngine(ConsoleStore& store, const std::atomic<int>& tickHz, std::function<void(const QString&)> statusChanged);
  ~PluginEngine();

  void start(const GameContext& ctx, std::shared_ptr<MemoryProvider> provider, const QList<PluginInfo>& list, const HostLimits& limits);
  void stop();
  void reload(const QString& id);
  void iterate();  // one host iteration: events, tick, watched rows

  // --- used by plugin_api.cpp ---
  Plugin* find(const QString& id);
  void log(Plugin& p, LogLevel level, const std::string& text);
  void emitEvent(Plugin& p, const std::string& name, Value value);
  void syncWatches(Plugin& p);
  void pause(Plugin& p, const std::string& error);
  // Runs a callback (registry ref) of `p` with arguments pushed by `pushArgs`; pauses `p` on failure.
  void callRef(Plugin& p, int ref, const std::function<int(lua_State*)>& pushArgs);
  void releaseState(Plugin& p);

  ConsoleStore& store;
  GameContext ctx;
  std::shared_ptr<MemoryProvider> provider;
  HostLimits limits;
  std::deque<QueuedEvent> events;
  std::chrono::steady_clock::time_point started;

 private:
  void load(Plugin& p);
  void setStatus(Plugin& p, PluginStatus s, const QString& error = QString());
  void deliverEvents();
  void tick();
  void updateWatched();

  const std::atomic<int>& m_tickHz;
  std::function<void(const QString&)> m_statusChanged;
  std::vector<std::unique_ptr<Plugin>> m_plugins;
  std::optional<uint64_t> m_lastGen;
  std::chrono::steady_clock::time_point m_lastTick;
  std::chrono::steady_clock::time_point m_lastWatched;
};

// plugin_api.cpp: creates the `fb` global in p.lua.
void registerFbApi(PluginEngine& engine, Plugin& p);
// Formats one watched value ("u8", "u16", "u32", "s32", "hex"); nullopt when not in the snapshot.
std::optional<QString> formatWatched(const MemoryProvider& mem, const DevWatch& w);

}  // namespace framebeam::plugins
