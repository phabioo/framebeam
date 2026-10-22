#include "connectioncheckcontroller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QVariantMap>

namespace uzume::ui {

// Lives on the worker thread: owns the ConnectionCheck (and with it the probes and their sockets) and forwards snapshots.
class CheckWorker : public QObject {
  Q_OBJECT
 public:
  explicit CheckWorker(ConnectionCheckController::ProbesFactory factory) : factory_(std::move(factory)) {}
  ~CheckWorker() override { check_.reset(); }

 public slots:
  void init() {
    if (check_) return;
    check_ = std::make_unique<ConnectionCheck>(factory_());
    connect(check_.get(), &ConnectionCheck::stepChanged, this, [this]() { push(); });
    connect(check_.get(), &ConnectionCheck::progress, this, [this](int n, int) {
      step_ = n;
      push();
    });
    connect(check_.get(), &ConnectionCheck::finished, this, [this]() { push(); });
  }
  void run(const uzume::ConnectionTarget& t) {
    init();
    step_ = 1;
    check_->start(t);
  }
  void cancel() {
    if (check_) check_->cancel();
  }

 signals:
  void updated(const uzume::CheckResult& r, bool running, int stepNumber);

 private:
  void push() { emit updated(check_->result(), check_->running(), step_); }

  ConnectionCheckController::ProbesFactory factory_;
  std::unique_ptr<ConnectionCheck> check_;
  int step_ = 1;
};

ConnectionCheckController::ConnectionCheckController(TargetProvider target, ProbesFactory probes, QObject* parent)
    : QObject(parent), targetProvider_(std::move(target)), probesFactory_(std::move(probes)) {
  qRegisterMetaType<uzume::CheckResult>();
  qRegisterMetaType<uzume::ConnectionTarget>();
}

ConnectionCheckController::~ConnectionCheckController() {
  if (thread_.isRunning()) {
    if (worker_ != nullptr) QMetaObject::invokeMethod(worker_, &CheckWorker::cancel, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
  }
}

void ConnectionCheckController::ensureWorker() {
  if (worker_ != nullptr) return;
  worker_ = new CheckWorker(probesFactory_);
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(worker_, &CheckWorker::updated, this, &ConnectionCheckController::onUpdated, Qt::QueuedConnection);
  thread_.setObjectName(QStringLiteral("connection-check"));
  thread_.start();
}

bool ConnectionCheckController::available() const {
  const ConnectionTarget t = targetProvider_ ? targetProvider_() : ConnectionTarget{};
  return !t.hubAddress.isEmpty();
}

QString ConnectionCheckController::outcome() const {
  if (running_) return QStringLiteral("running");
  if (!hasRun_) return QStringLiteral("idle");
  return outcomeName(result_.outcome);
}

QString ConnectionCheckController::summaryTone() const {
  if (running_ || !hasRun_) return QStringLiteral("none");
  switch (result_.outcome) {
    case CheckOutcome::Direct: return QStringLiteral("ok");
    case CheckOutcome::Relayed:
    case CheckOutcome::Blocked: return QStringLiteral("warn");
    default: return QStringLiteral("neutral");
  }
}

QString ConnectionCheckController::progressText() const {
  return running_ ? QStringLiteral("step %1 of %2").arg(stepNumber_).arg(kCheckStepCount) : QString();
}

QString ConnectionCheckController::subtitle() const {
  const ConnectionTarget t = targetProvider_ ? targetProvider_() : ConnectionTarget{};
  return QStringLiteral("This device · %1 · to %2")
      .arg(t.deviceName.isEmpty() ? QStringLiteral("this PC") : t.deviceName, t.hubName.isEmpty() ? QStringLiteral("the server") : t.hubName);
}

QVariantList ConnectionCheckController::steps() const {
  QVariantList out;
  if (result_.steps.isEmpty()) {
    for (int i = 0; i < kCheckStepCount; ++i) {
      const auto id = static_cast<CheckStepId>(i);
      out.append(QVariantMap{{QStringLiteral("id"), stepIdName(id)},
                             {QStringLiteral("title"), stepTitle(id)},
                             {QStringLiteral("state"), QStringLiteral("waiting")},
                             {QStringLiteral("result"), QStringLiteral("Waiting")},
                             {QStringLiteral("explanation"), QString()}});
    }
    return out;
  }
  for (const CheckStep& s : result_.steps) {
    out.append(QVariantMap{{QStringLiteral("id"), stepIdName(s.id)},
                           {QStringLiteral("title"), s.title()},
                           {QStringLiteral("state"), stepStateName(s.state)},
                           {QStringLiteral("result"), s.result},
                           {QStringLiteral("explanation"), s.explanation}});
  }
  return out;
}

bool ConnectionCheckController::canAttach() const {
  if (running_ || !hasRun_ || result_.steps.isEmpty()) return false;
  return result_.outcome != CheckOutcome::Canceled && result_.outcome != CheckOutcome::TimedOut;
}

QString ConnectionCheckController::lastResultJson() const { return QString::fromUtf8(lastJson_.isEmpty() ? QByteArray("{}") : lastJson_); }

void ConnectionCheckController::clearAttachment() {
  if (attachment_.isEmpty()) return;
  attachment_.clear();
  attachedCurrent_ = false;
  emit changed();
}

void ConnectionCheckController::show() {
  open_ = true;
  copied_ = false;
  emit changed();
  if (!running_ && available()) run();
}

void ConnectionCheckController::close() {
  if (running_) cancel();
  open_ = false;
  emit changed();
}

void ConnectionCheckController::run() {
  if (running_ || !available()) return;
  const ConnectionTarget t = targetProvider_();
  ensureWorker();
  result_ = CheckResult{};
  hasRun_ = true;
  running_ = true;
  stepNumber_ = 1;
  attachedCurrent_ = false;
  reportMark_ = false;
  copied_ = false;
  emit changed();
  QMetaObject::invokeMethod(worker_, [w = worker_, t]() { w->run(t); }, Qt::QueuedConnection);
}

void ConnectionCheckController::cancel() {
  if (!running_ || worker_ == nullptr) return;
  QMetaObject::invokeMethod(worker_, &CheckWorker::cancel, Qt::QueuedConnection);
}

void ConnectionCheckController::copyResult() {
  if (!hasRun_ || result_.steps.isEmpty()) return;
  if (QClipboard* cb = QGuiApplication::clipboard()) {
    cb->setText(result_.toPlainText());
    copied_ = true;
    emit changed();
  }
}

void ConnectionCheckController::addToReport() {
  if (!canAttach()) return;
  attachment_ = QJsonDocument(result_.toJson()).toJson(QJsonDocument::Indented);
  attachedCurrent_ = true;
  reportMark_ = true;
  emit changed();
}

void ConnectionCheckController::editHubAddress() {
  open_ = false;
  emit changed();
  emit editHubRequested();
}

void ConnectionCheckController::onUpdated(const CheckResult& r, bool running, int stepNumber) {
  if (!hasRun_) return;
  result_ = r;
  stepNumber_ = stepNumber;
  const bool wasRunning = running_;
  running_ = running;
  if (wasRunning && !running) {
    lastJson_ = QJsonDocument(r.toJson()).toJson(QJsonDocument::Indented);
    lastAt_ = r.finishedAt;
  }
  emit changed();
}

}  // namespace uzume::ui

#include "connectioncheckcontroller.moc"
