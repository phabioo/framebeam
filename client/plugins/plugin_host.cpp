#include "plugin_host.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <chrono>

#include "plugin_engine.h"

namespace framebeam::plugins {

// Lives on the plugin thread; owns the engine and drives it from a timer.
class HostWorker : public QObject {
  Q_OBJECT
 public:
  HostWorker(ConsoleStore& store, const std::atomic<int>& tickHz) : m_store(store), m_tickHz(tickHz) {}
  ~HostWorker() override = default;

  // Called on the plugin thread.
  void initOnThread() {
    m_engine = std::make_unique<PluginEngine>(m_store, m_tickHz, [this](const QString& id) { emit statusChanged(id); });
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(4);
    connect(m_timer, &QTimer::timeout, this, &HostWorker::onTimer);
  }
  void shutdownOnThread() {
    if (m_timer) m_timer->stop();
    m_engine.reset();
  }
  void start(const GameContext& ctx, const std::shared_ptr<MemoryProvider>& mem, const QList<PluginInfo>& list, const HostLimits& lim) {
    m_engine->start(ctx, mem, list, lim);
    m_timer->start();
    flushConsole(true);
  }
  void stop() {
    m_timer->stop();
    m_engine->stop();
    flushConsole(true);
  }
  void reload(const QString& id) {
    m_engine->reload(id);
    flushConsole(true);
  }

 signals:
  void statusChanged(const QString& id);
  void consoleChanged(const QString& id);

 private:
  void onTimer() {
    m_engine->iterate();
    flushConsole(false);
  }
  void flushConsole(bool force) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - m_lastFlush < std::chrono::milliseconds(100)) return;
    m_lastFlush = now;
    for (const QString& id : m_store.takeDirty()) emit consoleChanged(id);
  }

  ConsoleStore& m_store;
  const std::atomic<int>& m_tickHz;
  std::unique_ptr<PluginEngine> m_engine;
  QTimer* m_timer = nullptr;
  std::chrono::steady_clock::time_point m_lastFlush;
};

PluginHost::PluginHost(QObject* parent) : QObject(parent), m_store(std::make_unique<ConsoleStore>()) {
  m_thread = new QThread(this);
  m_thread->setObjectName(QStringLiteral("framebeam-plugins"));
  m_worker = new HostWorker(*m_store, m_tickHz);
  m_worker->moveToThread(m_thread);
  connect(m_worker, &HostWorker::statusChanged, this, &PluginHost::statusChanged);
  connect(m_worker, &HostWorker::consoleChanged, this, &PluginHost::consoleChanged);
  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  m_thread->start();
  QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->initOnThread(); }, Qt::BlockingQueuedConnection);
}

PluginHost::~PluginHost() {
  QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->shutdownOnThread(); }, Qt::BlockingQueuedConnection);
  m_thread->quit();
  m_thread->wait();
}

void PluginHost::start(const GameContext& ctx, std::shared_ptr<MemoryProvider> memory, const QList<PluginInfo>& plugins) {
  QStringList ids;
  for (const PluginInfo& p : plugins) ids << p.manifest.id;
  m_store->reset(ids, PluginStatus::Loading);
  const HostLimits lim = m_limits;
  QMetaObject::invokeMethod(m_worker, [w = m_worker, ctx, memory = std::move(memory), plugins, lim] { w->start(ctx, memory, plugins, lim); },
                            Qt::QueuedConnection);
}

void PluginHost::stop() {
  QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->stop(); }, Qt::BlockingQueuedConnection);
}

void PluginHost::reload(const QString& id) {
  QMetaObject::invokeMethod(m_worker, [w = m_worker, id] { w->reload(id); }, Qt::QueuedConnection);
}

void PluginHost::setTickHz(int hz) { m_tickHz = std::clamp(hz, 1, 240); }

void PluginHost::setLimits(const HostLimits& limits) { m_limits = limits; }

QStringList PluginHost::pluginIds() const { return m_store->ids(); }

ConsoleSnapshot PluginHost::consoleSnapshot(const QString& id) const { return m_store->snapshot(id); }

}  // namespace framebeam::plugins

#include "plugin_host.moc"
