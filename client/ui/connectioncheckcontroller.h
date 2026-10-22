#pragma once
// ConnectionCheckController: view state and actions of the connection check dialog (design 6e). The check itself
// (network/connectioncheck.h) runs on its own worker thread; this class keeps the latest snapshot for QML, copies the
// result and holds the result that "Add to problem report" attached for the next diagnostics bundle.

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "connectioncheck.h"

namespace framebeam::ui {

class CheckWorker;

class ConnectionCheckController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  Q_PROPERTY(bool open READ isOpen NOTIFY changed)
  // A Hub profile is active, so there is something to check.
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  // "idle" | "running" | "direct" | "relayed" | "blocked" | "hub_unreachable" | "not_connected" | "canceled" | "timed_out"
  Q_PROPERTY(QString outcome READ outcome NOTIFY changed)
  // Tone of the summary card: "none" | "ok" | "warn" | "neutral"
  Q_PROPERTY(QString summaryTone READ summaryTone NOTIFY changed)
  Q_PROPERTY(QString summary READ summary NOTIFY changed)
  Q_PROPERTY(QString summaryDetail READ summaryDetail NOTIFY changed)
  Q_PROPERTY(QString progressText READ progressText NOTIFY changed)  // "step 3 of 5" while running
  Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed)          // "This device · Desktop-LivingRoom · to Home Hub"
  // Rows: {id, title, state, result, explanation}; state = waiting | running | ok | warning | failed | skipped
  Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
  Q_PROPERTY(bool finished READ finished NOTIFY changed)  // a run ended (also canceled)
  Q_PROPERTY(bool canEditHubAddress READ canEditHubAddress NOTIFY changed)
  Q_PROPERTY(bool attached READ attached NOTIFY changed)  // the shown result is added to the next problem report
  Q_PROPERTY(bool hasAttachment READ hasAttachment NOTIFY changed)  // some result waits for the next diagnostics bundle
  Q_PROPERTY(bool copied READ copied NOTIFY changed)
  Q_PROPERTY(bool canAttach READ canAttach NOTIFY changed)
  Q_PROPERTY(QString lastResultAt READ lastResultAt NOTIFY changed)  // ISO time (UTC) of the last finished run; empty = none

 public:
  using TargetProvider = std::function<ConnectionTarget()>;
  using ProbesFactory = std::function<std::unique_ptr<ConnectionProbes>()>;  // called on the worker thread
  ConnectionCheckController(TargetProvider target, ProbesFactory probes, QObject* parent = nullptr);
  ~ConnectionCheckController() override;

  bool isOpen() const { return open_; }
  bool available() const;
  bool running() const { return running_; }
  QString outcome() const;
  QString summaryTone() const;
  QString summary() const { return result_.summary; }
  QString summaryDetail() const { return result_.summaryDetail; }
  QString progressText() const;
  QString subtitle() const;
  QVariantList steps() const;
  bool finished() const { return !running_ && hasRun_; }
  bool canEditHubAddress() const { return !running_ && result_.outcome == CheckOutcome::HubUnreachable; }
  bool attached() const { return attachedCurrent_; }
  bool hasAttachment() const { return !attachment_.isEmpty(); }
  bool copied() const { return copied_; }
  bool canAttach() const;
  QString lastResultAt() const { return lastAt_.isValid() ? lastAt_.toString(Qt::ISODate) : QString(); }

  // JSON of the last finished run ("{}" before the first one) and its time (lastResultAt): for the diagnostics bundle and
  // the "Report a problem" dialog.
  Q_INVOKABLE QString lastResultJson() const;
  // The result "Add to problem report" attached, until the next bundle takes it. Empty when nothing is attached.
  QByteArray attachmentJson() const { return attachment_; }
  void clearAttachment();

  // Opens the dialog and starts a run (unless one runs). Entry points: Settings (Hubs, Diagnostics), later a failed Session
  // and the "Relayed" pill.
  Q_INVOKABLE void show();
  Q_INVOKABLE void close();  // cancels a running check
  Q_INVOKABLE void run();    // "Run again"
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void copyResult();
  Q_INVOKABLE void addToReport();
  Q_INVOKABLE void editHubAddress();

 signals:
  void changed();
  void editHubRequested();

 private slots:
  void onUpdated(const framebeam::CheckResult& r, bool running, int stepNumber);

 private:
  void ensureWorker();

  TargetProvider targetProvider_;
  ProbesFactory probesFactory_;
  QThread thread_;
  CheckWorker* worker_ = nullptr;
  CheckResult result_;
  bool open_ = false;
  bool running_ = false;
  bool hasRun_ = false;
  int stepNumber_ = 0;
  bool attachedCurrent_ = false;
  bool copied_ = false;
  QByteArray attachment_;
  QByteArray lastJson_;
  QDateTime lastAt_;
};

}  // namespace framebeam::ui
