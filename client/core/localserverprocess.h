#pragma once
// The Uzume server of "Play on this PC" (0.18 package 10, ADR 0030 D6): uzume-server runs as a child process of the app,
// bound to loopback, per user, no service, no UAC. The app starts it with `-child -data-dir DIR -listen 127.0.0.1:PORT`;
// the server prints exactly one stdout line `UZUME_READY <port>` once it listens and exits when its stdin reaches EOF.
// No QML in here: LocalHubController owns one instance.

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace uzume {

class LocalServerProcess : public QObject {
  Q_OBJECT
 public:
  enum class State { Stopped, Starting, Running, Failed };
  Q_ENUM(State)

  struct Config {
    QString exe;           // empty: findServerExe() when starting
    QString dataDir;       // required
    int port = 0;          // 0: pick a free loopback port; otherwise the stored port
    int readyTimeoutMs = 20000;
    int stopWaitMs = 10000;        // stdin closed, then killed after this long
    int maxRestarts = 3;           // after an unexpected exit while running
    int backoffBaseMs = 1000;      // 1x, 2x, 4x
    int stableAfterMs = 60000;     // running this long resets the restart counter
  };

  explicit LocalServerProcess(QObject* parent = nullptr);
  ~LocalServerProcess() override;

  // UZUME_LOCAL_SERVER_EXE, then <applicationDirPath>/uzume-server(.exe), then the install folder of the registered
  // service (localhub::detect()). Empty when none exists as a file.
  static QString findServerExe();
  // `<baseDir>/server` (baseDir = ProfileStore::baseDir()).
  static QString defaultDataDir(const QString& baseDir);
  // A free TCP port on 127.0.0.1 (0 when none could be found).
  static int freeLoopbackPort();
  // The environment the child gets: the current one without any UZUME_* variable, so nothing makes it share.
  static QStringList cleanedEnvironment(const QStringList& environment);

  State state() const { return state_; }
  QString error() const { return error_; }  // text while Failed
  int port() const { return port_; }        // the port in use (the reported one once Running)
  QString dataDir() const { return config_.dataDir; }
  QString exe() const { return exe_; }
  bool addressInUseRetried() const { return retriedPort_; }

  void setConfig(const Config& c) { config_ = c; }
  const Config& config() const { return config_; }

  // Starts the child (Stopped or Failed only). Returns false when it cannot even be tried (no exe, no data dir).
  bool start();
  // Closes stdin, waits up to config.stopWaitMs, then kills. Blocking; also used at app quit. No restart afterwards.
  void stop();

 signals:
  void stateChanged();
  void ready(int port);
  void portChanged(int port);

 private:
  void launch();
  void onStdout();
  void onStderr();
  void onFinished(int code, QProcess::ExitStatus status);
  void onErrorOccurred(QProcess::ProcessError e);
  void onReadyTimeout();
  void fail(const QString& text);
  void setState(State s);
  void handleLine(const QString& line);
  void scheduleRestart();
  void dropProcess();

  Config config_;
  QString exe_;
  State state_ = State::Stopped;
  QString error_;
  int port_ = 0;
  QProcess* proc_ = nullptr;
  QByteArray outBuf_, errBuf_;
  QString lastErrLine_;
  bool addressInUse_ = false;
  bool retriedPort_ = false;
  bool stopping_ = false;
  bool wasReady_ = false;
  int restarts_ = 0;
  QTimer readyTimer_;
  QTimer restartTimer_;
  QTimer stableTimer_;
};

}  // namespace uzume
