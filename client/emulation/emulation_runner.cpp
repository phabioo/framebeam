#include "emulation_runner.h"

#include <QThread>

#include <chrono>
#include <cmath>

namespace framebeam::emu {

class EmulationRunner::Worker : public QThread {
 public:
  Worker(EmulationRunner* owner, EmulatorBackend* backend, StartRequest req)
      : m_owner(owner), m_backend(backend), m_req(std::move(req)) {}

  void requestStop() {
    QMutexLocker l(&m_mutex);
    m_stop = true;
    m_cond.wakeAll();
  }
  void setPaused(bool p) {
    QMutexLocker l(&m_mutex);
    m_paused = p;
    m_cond.wakeAll();
  }
  void requestReset() {
    QMutexLocker l(&m_mutex);
    m_reset = true;
    m_cond.wakeAll();
  }

 protected:
  void run() override {
    QString err;
    EmulatorBackend& be = *m_backend;
    // Directories before loadCore (cores already read them in retro_set_environment).
    be.setSystemDirectory(m_req.systemDir);
    be.setSaveDirectory(m_req.saveDir);
    bool ok = be.loadCore(m_req.corePath, &err);
    if (ok) {
      for (auto it = m_req.coreOptions.cbegin(); it != m_req.coreOptions.cend(); ++it) be.setCoreOption(it.key(), it.value());
      ok = be.loadGame(m_req.gamePath, &err);
    }
    if (!ok) {
      be.unloadCore();
      emit m_owner->startFailed(err);
      return;
    }
    const AvInfo av = be.avInfo();
    const int rate = static_cast<int>(std::lround(av.sampleRate));
    const double fps = av.fps > 1.0 ? av.fps : 60.0;
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / fps));
    emit m_owner->started(av, be.coreInfo());
    m_owner->setState(State::Running);

    auto next = std::chrono::steady_clock::now();
    bool wasPaused = false;
    while (true) {
      {
        QMutexLocker l(&m_mutex);
        while (m_paused && !m_stop) {
          if (!wasPaused) { wasPaused = true; m_owner->setState(State::Paused); }
          m_cond.wait(&m_mutex);
        }
        if (m_stop) break;
        if (wasPaused) { wasPaused = false; next = std::chrono::steady_clock::now(); m_owner->setState(State::Running); }
        if (m_reset) { m_reset = false; be.reset(); }
      }
      if (!be.runFrame()) {
        emit m_owner->errorOccurred(QStringLiteral("Core stopped execution"));
        break;
      }
      emit m_owner->frameReady(be.videoFrame(), be.frameCount());
      const QByteArray pcm = be.takeAudio();
      if (!pcm.isEmpty()) emit m_owner->audioReady(pcm, rate);

      next += period;
      const auto now = std::chrono::steady_clock::now();
      if (next < now - 5 * period) next = now;  // too far behind: resynchronize
      QMutexLocker l(&m_mutex);
      while (!m_stop && !m_paused && !m_reset) {
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(next - std::chrono::steady_clock::now());
        if (wait.count() <= 0) break;
        m_cond.wait(&m_mutex, static_cast<unsigned long>(wait.count()));
      }
    }
    be.unloadGame();
    be.unloadCore();
  }

 private:
  EmulationRunner* m_owner;
  EmulatorBackend* m_backend;
  StartRequest m_req;
  QMutex m_mutex;
  QWaitCondition m_cond;
  bool m_stop = false;
  bool m_paused = false;
  bool m_reset = false;
};

EmulationRunner::EmulationRunner(std::unique_ptr<EmulatorBackend> backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)) {
  qRegisterMetaType<framebeam::emu::AvInfo>();
  qRegisterMetaType<framebeam::emu::CoreInfo>();
  qRegisterMetaType<framebeam::emu::EmulationRunner::State>();
}

EmulationRunner::~EmulationRunner() { stop(); }

void EmulationRunner::setState(State s) {
  if (m_state.exchange(s) != s) emit stateChanged(s);
}

void EmulationRunner::start(const StartRequest& request) {
  if (m_worker && m_worker->isFinished()) m_worker.reset();  // finished by itself (error/core exit)
  if (m_worker) {
    emit startFailed(QStringLiteral("Emulation is already running"));
    return;
  }
  m_worker = std::make_unique<Worker>(this, m_backend.get(), request);
  setState(State::Starting);
  // If the thread finishes by itself (start failure/core exit), reset the state.
  QObject::connect(m_worker.get(), &QThread::finished, this, [this] {
    if (m_worker && m_worker->isFinished()) setState(State::Idle);
  });
  m_worker->start();
}

void EmulationRunner::pause() { if (m_worker) m_worker->setPaused(true); }
void EmulationRunner::resume() { if (m_worker) m_worker->setPaused(false); }
void EmulationRunner::reset() { if (m_worker) m_worker->requestReset(); }

void EmulationRunner::stop() {
  if (!m_worker) return;
  m_worker->requestStop();
  m_worker->wait();
  m_worker.reset();
  setState(State::Idle);
  emit stopped();
}

void EmulationRunner::setJoypadState(unsigned port, quint32 mask) { m_backend->setJoypadState(port, mask); }
void EmulationRunner::setPointer(double x, double y, bool pressed) { m_backend->setPointer(x, y, pressed); }
bool EmulationRunner::setCoreOption(const QString& key, const QString& value) { return m_backend->setCoreOption(key, value); }
QList<CoreOption> EmulationRunner::coreOptions() const { return m_backend->coreOptions(); }
QList<CoreOptionCategory> EmulationRunner::coreOptionCategories() const { return m_backend->coreOptionCategories(); }

}  // namespace framebeam::emu
