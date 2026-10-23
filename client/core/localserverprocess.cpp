#include "localserverprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTcpServer>

#include "envvar.h"
#include "localhub.h"

namespace uzume {

Q_LOGGING_CATEGORY(lcLocalServer, "uzume.localserver")

namespace {
QString exeName() {
#ifdef Q_OS_WIN
  return QStringLiteral("uzume-server.exe");
#else
  return QStringLiteral("uzume-server");
#endif
}
bool isFile(const QString& p) { return !p.isEmpty() && QFileInfo(p).isFile(); }
}  // namespace

LocalServerProcess::LocalServerProcess(QObject* parent) : QObject(parent) {
  readyTimer_.setSingleShot(true);
  restartTimer_.setSingleShot(true);
  stableTimer_.setSingleShot(true);
  connect(&readyTimer_, &QTimer::timeout, this, &LocalServerProcess::onReadyTimeout);
  connect(&restartTimer_, &QTimer::timeout, this, &LocalServerProcess::launch);
  connect(&stableTimer_, &QTimer::timeout, this, [this]() { restarts_ = 0; });
}

LocalServerProcess::~LocalServerProcess() { stop(); }

QString LocalServerProcess::findServerExe() {
  const QString env = uzume::env::get("UZUME_LOCAL_SERVER_EXE");
  if (isFile(env)) return env;
  const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(exeName());
  if (isFile(beside)) return beside;
  const localhub::Info info = localhub::detect();
  if (!info.installDir.isEmpty()) {
    const QString installed = QDir(info.installDir).filePath(exeName());
    if (isFile(installed)) return installed;
  }
  return {};
}

QString LocalServerProcess::defaultDataDir(const QString& baseDir) { return QDir(baseDir).filePath(QStringLiteral("server")); }

int LocalServerProcess::freeLoopbackPort() {
  QTcpServer s;
  if (!s.listen(QHostAddress::LocalHost, 0)) return 0;
  return s.serverPort();
}

QStringList LocalServerProcess::cleanedEnvironment(const QStringList& environment) {
  QStringList out;
  for (const QString& kv : environment) {
    if (kv.startsWith(QLatin1String("UZUME_"), Qt::CaseInsensitive)) continue;
    out.append(kv);
  }
  return out;
}

void LocalServerProcess::setState(State s) {
  if (state_ == s) return;
  state_ = s;
  if (s != State::Failed) error_.clear();
  emit stateChanged();
}

void LocalServerProcess::fail(const QString& text) {
  readyTimer_.stop();
  restartTimer_.stop();
  stableTimer_.stop();
  error_ = text;
  qCWarning(lcLocalServer) << "local server failed:" << text;
  dropProcess();
  state_ = State::Failed;
  emit stateChanged();
}

void LocalServerProcess::dropProcess() {
  if (proc_ == nullptr) return;
  QProcess* p = proc_;
  proc_ = nullptr;
  p->disconnect(this);
  if (p->state() != QProcess::NotRunning) {
    p->closeWriteChannel();
    if (!p->waitForFinished(2000)) {
      p->kill();
      p->waitForFinished(2000);
    }
  }
  p->deleteLater();
}

bool LocalServerProcess::start() {
  if (state_ == State::Starting || state_ == State::Running) return true;
  exe_ = config_.exe.isEmpty() ? findServerExe() : config_.exe;
  if (!isFile(exe_)) {
    fail(tr("The Uzume server program was not found next to the Uzume app."));
    return false;
  }
  if (config_.dataDir.isEmpty() || !QDir().mkpath(config_.dataDir)) {
    fail(tr("The folder for the Uzume server data could not be created."));
    return false;
  }
  port_ = config_.port > 0 ? config_.port : freeLoopbackPort();
  if (port_ <= 0) {
    fail(tr("No free port on this PC was found for the Uzume server."));
    return false;
  }
  stopping_ = false;
  retriedPort_ = false;
  restarts_ = 0;
  launch();
  return state_ == State::Starting;
}

void LocalServerProcess::launch() {
  dropProcess();
  outBuf_.clear();
  errBuf_.clear();
  lastErrLine_.clear();
  addressInUse_ = false;
  wasReady_ = false;
  proc_ = new QProcess(this);
  proc_->setProgram(exe_);
  proc_->setArguments({QStringLiteral("-child"), QStringLiteral("-data-dir"), QDir::toNativeSeparators(config_.dataDir),
                       QStringLiteral("-listen"), QStringLiteral("127.0.0.1:%1").arg(port_)});
  QProcessEnvironment env;
  for (const QString& kv : cleanedEnvironment(QProcessEnvironment::systemEnvironment().toStringList())) {
    const int eq = kv.indexOf(QLatin1Char('='));
    if (eq > 0) env.insert(kv.left(eq), kv.mid(eq + 1));
  }
  proc_->setProcessEnvironment(env);
  proc_->setProcessChannelMode(QProcess::SeparateChannels);
  connect(proc_, &QProcess::readyReadStandardOutput, this, &LocalServerProcess::onStdout);
  connect(proc_, &QProcess::readyReadStandardError, this, &LocalServerProcess::onStderr);
  connect(proc_, &QProcess::finished, this, &LocalServerProcess::onFinished);
  connect(proc_, &QProcess::errorOccurred, this, &LocalServerProcess::onErrorOccurred);
  setState(State::Starting);
  qCInfo(lcLocalServer) << "starting local server on port" << port_;
  readyTimer_.start(config_.readyTimeoutMs);
  proc_->start();
}

void LocalServerProcess::handleLine(const QString& line) {
  const QString t = line.trimmed();
  if (t.isEmpty()) return;
  static const QString kReady = QStringLiteral("UZUME_READY ");
  if (t.startsWith(kReady) && state_ == State::Starting) {
    bool ok = false;
    const int p = t.mid(kReady.size()).trimmed().toInt(&ok);
    if (ok && p > 0 && p <= 65535) {
      readyTimer_.stop();
      const bool changed = p != port_;
      port_ = p;
      wasReady_ = true;
      setState(State::Running);
      stableTimer_.start(config_.stableAfterMs);
      if (changed) emit portChanged(port_);
      qCInfo(lcLocalServer) << "local server ready on port" << port_;
      emit ready(port_);
      return;
    }
  }
  qCInfo(lcLocalServer).noquote() << "server:" << t.left(500);
}

void LocalServerProcess::onStdout() {
  if (proc_ == nullptr) return;
  outBuf_ += proc_->readAllStandardOutput();
  int nl;
  while ((nl = outBuf_.indexOf('\n')) >= 0) {
    const QString line = QString::fromUtf8(outBuf_.left(nl));
    outBuf_.remove(0, nl + 1);
    handleLine(line);
  }
}

void LocalServerProcess::onStderr() {
  if (proc_ == nullptr) return;
  errBuf_ += proc_->readAllStandardError();
  int nl;
  while ((nl = errBuf_.indexOf('\n')) >= 0) {
    const QString line = QString::fromUtf8(errBuf_.left(nl)).trimmed();
    errBuf_.remove(0, nl + 1);
    if (line.isEmpty()) continue;
    lastErrLine_ = line.left(300);
    if (line.contains(QLatin1String("address in use"), Qt::CaseInsensitive)) addressInUse_ = true;
    qCInfo(lcLocalServer).noquote() << "server:" << line.left(500);
  }
}

void LocalServerProcess::onReadyTimeout() {
  if (state_ != State::Starting) return;
  fail(tr("The Uzume server did not become ready in time."));
}

void LocalServerProcess::onErrorOccurred(QProcess::ProcessError e) {
  if (e == QProcess::FailedToStart && !stopping_) {
    fail(tr("The Uzume server could not be started."));
  }
}

void LocalServerProcess::onFinished(int code, QProcess::ExitStatus status) {
  if (stopping_ || proc_ == nullptr) return;
  onStdout();
  onStderr();
  // The rest of a last line without newline.
  if (!errBuf_.isEmpty()) {
    const QString rest = QString::fromUtf8(errBuf_).trimmed();
    errBuf_.clear();
    if (rest.contains(QLatin1String("address in use"), Qt::CaseInsensitive)) addressInUse_ = true;
    if (!rest.isEmpty()) lastErrLine_ = rest.left(300);
  }
  stableTimer_.stop();
  qCInfo(lcLocalServer) << "local server exited with code" << code << (status == QProcess::CrashExit ? "(crash)" : "");
  if (state_ == State::Starting) {
    readyTimer_.stop();
    if (addressInUse_ && !retriedPort_) {
      retriedPort_ = true;
      const int next = freeLoopbackPort();
      if (next > 0 && next != port_) {
        port_ = next;
        emit portChanged(port_);
        launch();
        return;
      }
    }
    fail(addressInUse_ ? tr("The port of the Uzume server is already in use.")
                       : (lastErrLine_.isEmpty() ? tr("The Uzume server stopped right after it started (code %1).").arg(code)
                                                 : tr("The Uzume server stopped right after it started: %1").arg(lastErrLine_)));
    return;
  }
  if (state_ == State::Running) {
    scheduleRestart();
  }
}

void LocalServerProcess::scheduleRestart() {
  if (restarts_ >= config_.maxRestarts) {
    fail(tr("The Uzume server keeps stopping."));
    return;
  }
  const int delay = config_.backoffBaseMs << restarts_;
  ++restarts_;
  qCInfo(lcLocalServer) << "restarting local server in" << delay << "ms (attempt" << restarts_ << ")";
  dropProcess();
  setState(State::Starting);
  restartTimer_.start(delay);
}

void LocalServerProcess::stop() {
  stopping_ = true;
  readyTimer_.stop();
  restartTimer_.stop();
  stableTimer_.stop();
  if (proc_ != nullptr) {
    QProcess* p = proc_;
    proc_ = nullptr;
    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
      qCInfo(lcLocalServer) << "stopping local server";
      p->closeWriteChannel();
      if (!p->waitForFinished(config_.stopWaitMs)) {
        qCWarning(lcLocalServer) << "local server did not stop in time, killing it";
        p->kill();
        p->waitForFinished(3000);
      }
    }
    delete p;
  }
  if (state_ != State::Stopped) {
    state_ = State::Stopped;
    error_.clear();
    emit stateChanged();
  }
}

}  // namespace uzume
