#pragma once
// EmulationRunner: fuehrt ein EmulatorBackend in einem eigenen Thread aus und taktet runFrame()
// mit der vom Core gemeldeten FPS. Keine Audioausgabe und kein Rendering (macht die UI).
//
// Der Runner lebt im Thread des Aufrufers (UI); alle Backend-Aufrufe (Core laden, Frames, Entladen)
// laufen im Emulationsthread. Signale werden aus dem Emulationsthread gesendet; Empfaenger in
// anderen Threads erhalten sie ueber Queued Connections (Auto-Connection).

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
    QMap<QString, QString> coreOptions;  // z. B. Manifest-Defaults, vor dem Spielstart gesetzt
  };

  explicit EmulationRunner(std::unique_ptr<EmulatorBackend> backend, QObject* parent = nullptr);
  ~EmulationRunner() override;

  State state() const { return m_state; }

  // Asynchron: Core laden, Spiel laden, Takt starten. Ergebnis: started() oder startFailed().
  void start(const StartRequest& request);
  void pause();
  void resume();
  void reset();
  // Blockiert, bis der Emulationsthread beendet und der Core entladen ist; dann stopped().
  void stop();

  // thread-sicher, wirken beim naechsten Frame
  void setJoypadState(unsigned port, quint32 buttonMask);
  void setPointer(double x, double y, bool pressed);
  bool setCoreOption(const QString& key, const QString& value);
  QList<CoreOption> coreOptions() const;
  QList<CoreOptionCategory> coreOptionCategories() const;

 signals:
  void started(const framebeam::emu::AvInfo& av, const framebeam::emu::CoreInfo& core);
  void startFailed(const QString& error);
  // Frame im Format QImage::Format_RGB32 (XRGB8888); implizit geteilt, nicht veraendern.
  void frameReady(const QImage& frame, quint64 frameNumber);
  // Interleaved Stereo int16, nativ; sampleRate in Hz (vom Core).
  void audioReady(const QByteArray& pcm, int sampleRate);
  void stateChanged(framebeam::emu::EmulationRunner::State state);
  // Core beendete sich selbst bzw. runFrame schlug fehl; der Thread laeuft aus (danach stop()).
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
