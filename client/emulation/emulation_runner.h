#pragma once
// EmulationRunner: runs an EmulatorBackend in its own thread and paces runFrame()
// at the FPS reported by the core. No audio output and no rendering (the UI does that).
//
// The runner lives in the caller's thread (UI); all backend calls (load core, frames, unload)
// run in the emulation thread. Signals are emitted from the emulation thread; receivers in
// other threads get them via queued connections (auto connection).

#include <QImage>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

#include <atomic>
#include <memory>

#include "emulator_backend.h"

namespace framebeam::emu {

class EmulationRunner : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Starting, Running, Paused };
  Q_ENUM(State)

  struct StartRequest {
    QString corePath;
    QString gamePath;
    QString systemDir;
    QString saveDir;
    QMap<QString, QString> coreOptions;  // e.g. manifest defaults, set before the game starts
  };

  explicit EmulationRunner(std::unique_ptr<EmulatorBackend> backend, QObject* parent = nullptr);
  ~EmulationRunner() override;

  State state() const { return m_state; }

  // Asynchronous: load core, load game, start the clock. Result: started() or startFailed().
  void start(const StartRequest& request);
  void pause();
  void resume();
  void reset();
  // Blocks until the emulation thread has finished and the core is unloaded; then stopped().
  void stop();

  // thread-safe, take effect on the next frame
  void setJoypadState(unsigned port, quint32 buttonMask);
  void setPointer(double x, double y, bool pressed);
  bool setCoreOption(const QString& key, const QString& value);
  QList<CoreOption> coreOptions() const;
  QList<CoreOptionCategory> coreOptionCategories() const;

 signals:
  void started(const framebeam::emu::AvInfo& av, const framebeam::emu::CoreInfo& core);
  void startFailed(const QString& error);
  // Frame in QImage::Format_RGB32 (XRGB8888); implicitly shared, do not modify.
  void frameReady(const QImage& frame, quint64 frameNumber);
  // Interleaved stereo int16, native endian; sampleRate in Hz (from the core).
  void audioReady(const QByteArray& pcm, int sampleRate);
  void stateChanged(framebeam::emu::EmulationRunner::State state);
  // The core shut itself down or runFrame failed; the thread winds down (then stop()).
  void errorOccurred(const QString& message);
  void stopped();

 private:
  class Worker;
  void setState(State s);

  std::unique_ptr<EmulatorBackend> m_backend;
  std::unique_ptr<Worker> m_worker;
  std::atomic<State> m_state{State::Idle};
};

}  // namespace framebeam::emu

Q_DECLARE_METATYPE(framebeam::emu::AvInfo)
Q_DECLARE_METATYPE(framebeam::emu::CoreInfo)
Q_DECLARE_METATYPE(framebeam::emu::EmulationRunner::State)
