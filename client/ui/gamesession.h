#pragma once
// GameSession: a local emulation session (EmulationRunner + AudioOutput) for the game view.
// Holds the last frame, the keyboard state and the display profile for touch conversion.

#include <QImage>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "audiooutput.h"
#include "emulation_runner.h"
#include "inputmapping.h"
#include "system_manifest.h"

namespace framebeam::ui {

class GameSession : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")
  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(QString title READ title NOTIFY titleChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
  Q_PROPERTY(bool active READ isActive NOTIFY stateChanged)
  Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged)
  Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
 public:
  enum State { Idle, Starting, Running, Paused, Failed };
  Q_ENUM(State)

  struct LaunchConfig {
    QString title;
    QString corePath;
    QString gamePath;
    QString systemDir;
    QString saveDir;
    QMap<QString, QString> coreOptions;
    emu::DisplayProfile display;
  };

  explicit GameSession(QObject* parent = nullptr);
  ~GameSession() override;

  State state() const { return state_; }
  QString title() const { return title_; }
  QString errorText() const { return error_; }
  bool isActive() const { return state_ == Starting || state_ == Running || state_ == Paused; }
  bool isPaused() const { return state_ == Paused; }
  bool hasFrame() const { return !frame_.isNull(); }
  QImage frame() const { return frame_; }
  quint64 frameNumber() const { return frameNr_; }
  const emu::DisplayProfile& displayProfile() const { return display_; }
  const AudioOutput& audio() const { return audio_; }

  void start(const LaunchConfig& config);

  Q_INVOKABLE void pause();
  Q_INVOKABLE void resume();
  Q_INVOKABLE void togglePause();
  Q_INVOKABLE void reset();
  Q_INVOKABLE void stop();  // blocks until the core is unloaded

  // Input (UI thread). Returns true if the key is mapped.
  bool keyEvent(int qtKey, bool pressed);
  void releaseAllKeys();
  void setPointer(const QPointF& frameNormalized, bool pressed);

 signals:
  void stateChanged();
  void titleChanged();
  void errorChanged();
  void frameChanged();
  void started();                       // core and game are running
  void startFailed(const QString& msg);  // failed before the first frame
  void finished();                       // after stop()

 private:
  void setState(State s);
  void fail(const QString& msg);
  void teardown();

  std::unique_ptr<emu::EmulationRunner> runner_;
  AudioOutput audio_;
  KeyboardJoypad keys_;
  emu::DisplayProfile display_;
  QImage frame_;
  quint64 frameNr_ = 0;
  QString title_;
  QString error_;
  State state_ = Idle;
  bool startedEmitted_ = false;
};

}  // namespace framebeam::ui
