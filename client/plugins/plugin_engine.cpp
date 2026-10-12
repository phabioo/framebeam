#include "plugin_engine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "lauxlib.h"
#include "lua.h"

namespace framebeam::plugins {

using Clock = std::chrono::steady_clock;

namespace {
constexpr qint64 kMaxEntryBytes = 4 * 1024 * 1024;
constexpr size_t kMaxQueuedEvents = 10000;

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
}  // namespace

// ---------------------------------------------------------------- ConsoleStore

void ConsoleStore::reset(const QStringList& ids, PluginStatus status) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_map.clear();
  m_dirty.clear();
  m_order = ids;
  for (const QString& id : ids) {
    ConsoleSnapshot s;
    s.status = status;
    m_map.insert(id, s);
  }
}

void ConsoleStore::setStatus(const QString& id, PluginStatus status, const QString& error) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ConsoleSnapshot& s = m_map[id];
  s.status = status;
  s.error = error;
  if (status != PluginStatus::Running) s.watched.clear();
  if (!m_order.contains(id)) m_order.append(id);
  m_dirty.insert(id);
}

void ConsoleStore::addLog(const QString& id, LogLevel level, const QString& text) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ConsoleSnapshot& s = m_map[id];
  s.log.append({QDateTime::currentMSecsSinceEpoch(), level, text});
  while (s.log.size() > kLogRing) s.log.removeFirst();
  m_dirty.insert(id);
}

void ConsoleStore::addEvent(const QString& id, const EventLine& line) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ConsoleSnapshot& s = m_map[id];
  s.events.append(line);
  while (s.events.size() > kEventRing) s.events.removeFirst();
  m_dirty.insert(id);
}

bool ConsoleStore::setWatched(const QString& id, const QList<WatchedRow>& rows) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ConsoleSnapshot& s = m_map[id];
  if (s.watched.size() == rows.size()) {
    bool same = true;
    for (qsizetype i = 0; i < rows.size() && same; ++i)
      same = s.watched[i].label == rows[i].label && s.watched[i].address == rows[i].address && s.watched[i].value == rows[i].value;
    if (same) return false;
  }
  s.watched = rows;
  m_dirty.insert(id);
  return true;
}

ConsoleSnapshot ConsoleStore::snapshot(const QString& id) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_map.value(id);
}

QStringList ConsoleStore::ids() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_order;
}

QStringList ConsoleStore::takeDirty() {
  std::lock_guard<std::mutex> lock(m_mutex);
  const QStringList out(m_dirty.begin(), m_dirty.end());
  m_dirty.clear();
  return out;
}

// ---------------------------------------------------------------- PluginEngine

PluginEngine::PluginEngine(ConsoleStore& s, const std::atomic<int>& tickHz, std::function<void(const QString&)> statusChanged)
    : store(s), m_tickHz(tickHz), m_statusChanged(std::move(statusChanged)) {
  started = Clock::now();
}

PluginEngine::~PluginEngine() { stop(); }

Plugin* PluginEngine::find(const QString& id) {
  for (auto& p : m_plugins)
    if (p->id() == id) return p.get();
  return nullptr;
}

void PluginEngine::setStatus(Plugin& p, PluginStatus s, const QString& error) {
  p.status = s;
  store.setStatus(p.id(), s, error);
  if (m_statusChanged) m_statusChanged(p.id());
}

void PluginEngine::log(Plugin& p, LogLevel level, const std::string& text) {
  QString t = qs(text);
  if (t.size() > 4000) t = t.left(4000) + QStringLiteral("...");
  store.addLog(p.id(), level, t);
}

void PluginEngine::emitEvent(Plugin& p, const std::string& name, Value value) {
  EventLine line;
  line.timeMs = QDateTime::currentMSecsSinceEpoch();
  line.plugin = p.id();
  line.name = qs(name);
  line.text = qs(shortText(value));
  store.addEvent(p.id(), line);
  events.push_back({p.id(), name, std::move(value), QString()});
}

void PluginEngine::syncWatches(Plugin& p) {
  if (!provider) return;
  std::vector<WatchRange> ranges;
  ranges.reserve(p.watches.size());
  for (const auto& kv : p.watches) ranges.push_back(kv.second);
  provider->setWatches(p.id().toStdString(), std::move(ranges));
}

void PluginEngine::releaseState(Plugin& p) {
  // Order matters: the state owns the registry refs, so drop the refs' bookkeeping with it.
  p.subs.clear();
  p.provided.clear();
  p.tickRef = LUA_NOREF;
  p.lua.reset();
  p.rom.reset();
  p.watches.clear();
  p.devWatches.clear();
  p.lastRows.clear();
  p.actionRef = LUA_NOREF;
  p.runSubs.clear();
  p.hasBoardPending = false;
  p.uiTrees.clear();
  syncWatches(p);
}

void PluginEngine::pause(Plugin& p, const std::string& error) {
  if (p.status == PluginStatus::Paused) return;
  const std::string first = LuaSandbox::firstLine(error);
  log(p, LogLevel::Error, "Paused: " + first);
  releaseState(p);
  store.ui.markPaused(p.id());
  setStatus(p, PluginStatus::Paused, qs(error));
}

void PluginEngine::callRef(Plugin& p, int ref, const std::function<int(lua_State*)>& pushArgs) {
  if (p.status != PluginStatus::Running || !p.lua) return;
  std::string err;
  const bool ok = p.lua->run(
      [&](lua_State* L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        const int n = pushArgs(L);
        lua_call(L, n, 0);
      },
      err);
  if (!ok) pause(p, err);
}

void PluginEngine::load(Plugin& p) {
  setStatus(p, PluginStatus::Loading);
  SandboxLimits sl;
  sl.memoryBytes = limits.memoryBytes;
  sl.instructions = limits.instructions;
  sl.wallMs = limits.wallMs;
  p.lua = std::make_unique<LuaSandbox>(sl);
  if (!p.lua->valid()) {
    pause(p, "Lua state could not be created");
    return;
  }
  p.lua->setOwner(&p);
  p.lua->setPrintSink([this, &p](LogLevel lv, const std::string& t) { log(p, lv, t); });

  std::string err;
  bool ok = p.lua->run([&](lua_State*) { registerFbApi(*this, p); }, err);
  if (!ok) {
    pause(p, "Could not set up the plugin API: " + err);
    return;
  }
  QFile f(QDir(p.info.path).filePath(p.info.manifest.entry));
  if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxEntryBytes) {
    pause(p, "Cannot read entry file '" + p.info.manifest.entry.toStdString() + "' (missing or larger than 4 MiB)");
    return;
  }
  const std::string code = f.readAll().toStdString();
  f.close();
  if (!p.lua->runChunk(code, "@" + p.info.manifest.entry.toStdString(), err)) {
    pause(p, err);
    return;
  }
  setStatus(p, PluginStatus::Running);
  log(p, LogLevel::Info, "Loaded " + p.info.manifest.name.toStdString() + " " + p.info.manifest.version.toString().toStdString());
}

void PluginEngine::start(const GameContext& c, std::shared_ptr<MemoryProvider> prov, const QList<PluginInfo>& list, const HostLimits& lim) {
  stop();
  ctx = c;
  provider = std::move(prov);
  limits = lim;
  started = Clock::now();
  m_lastGen.reset();
  m_lastTick = Clock::time_point();
  m_lastWatched = Clock::time_point();
  QStringList ids;
  for (const PluginInfo& info : list) ids << info.manifest.id;
  store.reset(ids, PluginStatus::Loading);
  store.ui.reset(ids);
  for (const PluginInfo& info : list) {
    auto p = std::make_unique<Plugin>();
    p->info = info;
    p->engine = this;
    m_plugins.push_back(std::move(p));
  }
  for (auto& p : m_plugins) load(*p);
}

void PluginEngine::stop() {
  for (auto& p : m_plugins) {
    releaseState(*p);
    store.ui.clearPlugin(p->id());
    if (p->status != PluginStatus::Stopped) setStatus(*p, PluginStatus::Stopped, p->status == PluginStatus::Paused ? store.snapshot(p->id()).error : QString());
  }
  m_plugins.clear();
  events.clear();
  provider.reset();
}

void PluginEngine::reload(const QString& id) {
  Plugin* p = find(id);
  if (!p) return;
  releaseState(*p);
  store.ui.clearPlugin(p->id());
  p->status = PluginStatus::Loading;
  p->i18n.clear();
  const ManifestResult m = loadManifest(p->info.path);
  if (!m.ok) {
    pause(*p, "Manifest: " + m.error.toStdString());
    return;
  }
  p->info.manifest = m.manifest;
  log(*p, LogLevel::Info, "Reloading");
  load(*p);
}

void PluginEngine::deliverEvents() {
  size_t n = (std::min<size_t>)(events.size(), 1000);
  while (n-- > 0 && !events.empty()) {
    QueuedEvent ev = std::move(events.front());
    events.pop_front();
    for (auto& pp : m_plugins) {
      Plugin& p = *pp;
      if (p.status != PluginStatus::Running) continue;
      std::vector<int> refs;
      for (const EventSub& s : p.subs)
        if ((ev.target.isEmpty() || ev.target == p.id()) && qs(ev.name) == s.name && (s.plugin == QLatin1String("*") || s.plugin == ev.from)) refs.push_back(s.ref);
      for (int ref : refs) {
        callRef(p, ref, [&](lua_State* L) {
          pushValue(L, ev.value);
          lua_pushstring(L, ev.from.toUtf8().constData());
          lua_pushlstring(L, ev.name.data(), ev.name.size());
          return 3;
        });
        if (p.status != PluginStatus::Running) break;
      }
    }
  }
}

void PluginEngine::deliverAction(const QString& pluginId, const QString& slot, const QString& action, const Value& value) {
  Plugin* p = find(pluginId);
  if (!p || p->status != PluginStatus::Running || p->actionRef == LUA_NOREF) return;
  const QByteArray a = action.toUtf8();
  const QByteArray sl = slot.toUtf8();
  callRef(*p, p->actionRef, [&](lua_State* L) {
    lua_pushlstring(L, a.constData(), static_cast<size_t>(a.size()));
    pushValue(L, value);
    lua_pushlstring(L, sl.constData(), static_cast<size_t>(sl.size()));
    return 3;
  });
}

void PluginEngine::postHostEvent(const QString& name, Value value, const QString& target) {
  if (events.size() >= kMaxQueuedEvents) return;
  events.push_back({QStringLiteral("fb"), name.toStdString(), std::move(value), target});
}

void PluginEngine::clearSlotEverywhere(const QString& slot) {
  for (auto& p : m_plugins) {
    p->uiTrees.erase(slot);
    store.ui.clearSlot(p->id(), slot);
  }
}

void PluginEngine::tick() {
  if (!provider) return;
  const std::optional<uint64_t> frame = provider->frame();
  if (!frame) return;
  const uint64_t gen = provider->generation();
  if (m_lastGen && *m_lastGen == gen) return;
  const int hz = (std::max)(1, m_tickHz.load());
  const auto now = Clock::now();
  if (m_lastTick != Clock::time_point() && now - m_lastTick < std::chrono::microseconds(1000000 / hz)) return;
  m_lastGen = gen;
  m_lastTick = now;
  for (auto& pp : m_plugins) {
    Plugin& p = *pp;
    if (p.status != PluginStatus::Running || p.tickRef == LUA_NOREF) continue;
    callRef(p, p.tickRef, [&](lua_State* L) {
      lua_pushinteger(L, static_cast<lua_Integer>(*frame));
      return 1;
    });
  }
}

std::optional<QString> formatWatched(const MemoryProvider& mem, const DevWatch& w) {
  unsigned char buf[64] = {};
  if (w.size == 0 || w.size > sizeof buf || !mem.read(w.addr, buf, w.size)) return std::nullopt;
  uint64_t u = 0;
  for (uint32_t i = 0; i < w.size && i < 8; ++i) u |= static_cast<uint64_t>(buf[i]) << (8 * i);
  if (w.kind == QLatin1String("s32")) return QString::number(static_cast<int32_t>(u));
  if (w.kind == QLatin1String("hex")) {
    QString s;
    for (uint32_t i = 0; i < w.size; ++i) s += QString::number(buf[i], 16).toUpper().rightJustified(2, QLatin1Char('0')) + (i + 1 < w.size ? QStringLiteral(" ") : QString());
    return s;
  }
  return QString::number(u);
}

void PluginEngine::updateWatched() {
  if (!provider) return;
  const auto now = Clock::now();
  if (now - m_lastWatched < std::chrono::milliseconds(100)) return;
  m_lastWatched = now;
  for (auto& pp : m_plugins) {
    Plugin& p = *pp;
    if (p.status != PluginStatus::Running && p.devWatches.empty()) continue;
    QList<WatchedRow> rows;
    for (const DevWatch& w : p.devWatches) {
      const std::optional<QString> v = formatWatched(*provider, w);
      const QString addr = QStringLiteral("0x") + QString::number(w.addr, 16).toUpper().rightJustified(w.addr > 0xFFFFFFFFull ? 16 : 8, QLatin1Char('0'));
      rows.append({w.label, addr, v ? *v : QStringLiteral("-")});
    }
    store.setWatched(p.id(), rows);
  }
}

// fb.run: hands what happened in the run to the subscribed plugins, and sends boards that waited for the 1/s limit.
void PluginEngine::pollRun() {
  if (!ctx.run) return;
  const auto now = Clock::now();
  for (auto& pp : m_plugins) {
    Plugin& p = *pp;
    if (p.hasBoardPending && now - p.boardLast >= std::chrono::milliseconds(1000)) {
      p.hasBoardPending = false;
      p.boardLast = now;
      const QString e = ctx.run->setBoard(p.id(), p.boardPending);
      if (!e.isEmpty()) log(p, LogLevel::Warn, "fb.run.setBoard: " + e.toStdString());
    }
  }
  for (const RunNotification& n : ctx.run->takeNotifications()) {
    const std::string kind = n.kind.toStdString();
    const Value data = valueFromJson(n.data);
    for (auto& pp : m_plugins) {
      Plugin& p = *pp;
      if (p.status != PluginStatus::Running) continue;
      std::vector<int> refs;
      for (const RunSub& s : p.runSubs)
        if (s.kind == kind) refs.push_back(s.ref);
      for (int ref : refs) {
        callRef(p, ref, [&](lua_State* L) {
          // event: (event); state: (user_id, state, plugin_id); status: (status, previous); participants: (list)
          const auto field = [&](const char* name) {
            for (size_t i = 0; i < data.keys.size(); ++i)
              if (data.keys[i].type == Value::Type::String && data.keys[i].s == name) return data.vals[i];
            return Value();
          };
          if (kind == "state") {
            pushValue(L, field("user_id"));
            pushValue(L, field("state"));
            pushValue(L, field("plugin_id"));
            return 3;
          }
          if (kind == "status") {
            pushValue(L, field("status"));
            pushValue(L, field("previous"));
            return 2;
          }
          if (kind == "participants") {
            pushValue(L, field("participants"));
            return 1;
          }
          pushValue(L, data);
          return 1;
        });
        if (p.status != PluginStatus::Running) break;
      }
    }
  }
}

void PluginEngine::iterate() {
  pollRun();
  deliverEvents();
  tick();
  updateWatched();
}

}  // namespace framebeam::plugins
