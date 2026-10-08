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
#include "frame_stats.h"

namespace framebeam::emu {

class EmulationRunner : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Starting, Running, Paused };
  Q_ENUM(State)

  static constexpr double kDefaultSpeedUpRatio = 2.0;
  // Speeds the user can choose: 1.5, 2, 3, 4, 6, 8.
  static QList<double> speedUpRatios() { return {1.5, 2.0, 3.0, 4.0, 6.0, 8.0}; }
  static double normalizeSpeedUpRatio(double ratio);  // nearest allowed value
  // Speed-up audio: linear-interpolating decimation of interleaved stereo int16 by `ratio` to real time (pitch rises).
  // `phase` carries the fractional read position across chunks (start with 0).
  static QByteArray resampleForSpeedUp(const QByteArray& pcm, double ratio, double* phase);

  struct StartRequest {
    QString corePath;
    QString gamePath;
    QString systemDir;
    QString saveDir;
    QMap<QString, QString> coreOptions;  // e.g. manifest defaults, set before the game starts
    // Speed-up (fast-forward) settings: chosen speed, start sped up (if the core allows it), audio during speed-up.
    double speedUpRatio = kDefaultSpeedUpRatio;
    bool speedUpOnStart = false;
    bool speedUpAudio = true;
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

  // Speed-up (thread-safe): the clock runs at fps * fastForwardRatio(); the UI still gets frames at the base fps and
  // audio is resampled to real time (or dropped when speedUpAudio is off). Only possible if the core allows it (known
  // after started()). The user's ratio wins over a ratio in the core's override.
  void setFastForward(bool on);
  bool fastForward() const { return m_fastForward.load(); }
  bool supportsFastForward() const { return m_backend->supportsFastForward(); }
  void setFastForwardRatio(double ratio) { m_ratio.store(normalizeSpeedUpRatio(ratio)); }
  double fastForwardRatio() const { return m_ratio.load(); }
  void setSpeedUpAudio(bool on) { m_speedUpAudio.store(on); }
  bool speedUpAudio() const { return m_speedUpAudio.load(); }

  // Diagnostics (thread-safe, cheap): measured frame timing of the emulation thread and how the core renders.
  FrameTimingStats::Snapshot timing() const;
  RenderInfo renderInfo() const { return m_backend->renderInfo(); }
  // Tests only: round every pacing sleep up to a multiple of this many ms (0 = off), simulating coarse OS timers.
  static std::atomic<int> sleepQuantumMsForTest;
  static qint64 monotonicMs();  // the clock of timing()

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
  FrameTimingStats m_timing;
  std::atomic<bool> m_fastForward{false};
  std::atomic<double> m_ratio{kDefaultSpeedUpRatio};
  std::atomic<bool> m_speedUpAudio{true};
};

}  // namespace framebeam::emu

Q_DECLARE_METATYPE(framebeam::emu::AvInfo)
Q_DECLARE_METATYPE(framebeam::emu::CoreInfo)
Q_DECLARE_METATYPE(framebeam::emu::EmulationRunner::State)
