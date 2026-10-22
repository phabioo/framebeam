// "Report a problem" upload (0.17, feature reports_v1) against the fake Hub: multipart body, states, retry, offline, cancel.
// Synthetic data only.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "problemreport.h"
#include "profilestore.h"
#include "reportuploader.h"

using namespace uzume;
using State = HubConnection::State;
using US = ReportUploader::State;

class ReportUploadTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<ReportUploader> up_;

  void pairAndConnect() {
    conn_->connectToAddress(hub_->address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
    hub_->decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }

  // A report with a description and the default attachments; body and content type ready to send.
  void makeBody(QByteArray* body, QByteArray* ctype, const QByteArray& logText = "2026-10-22T10:00:00.000 [I] hub: connected\n") {
    QDir().mkpath(dir_->filePath(QStringLiteral("data/logs")));
    QFile log(dir_->filePath(QStringLiteral("data/logs/player.log")));
    QVERIFY(log.open(QIODevice::WriteOnly));
    log.write(QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8() + logText.mid(23));
    log.close();
    ProblemReport::Sources s;
    s.dataDir = dir_->filePath(QStringLiteral("data"));
    s.playerVersion = QStringLiteral("0.17.0");
    s.os = QStringLiteral("Windows 11");
    ProblemReport r = ProblemReport::collect(s);
    r.setDescription(QStringLiteral("The Session froze after four hours."));
    r.setLinkedEvent(QStringLiteral("Session ended unexpectedly"), QDateTime::currentDateTime());
    *body = r.multipartBody(ctype);
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->filePath(QStringLiteral("data")));
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->features = {QStringLiteral("saves_v1"), QStringLiteral("reports_v1")};
    QVERIFY(hub_->start());
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
    up_ = std::make_unique<ReportUploader>(conn_.get());
  }
  void cleanup() {
    up_.reset();
    conn_.reset();
    hub_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  void sendsTheMultipartReportAndKeepsTheNumber() {
    pairAndConnect();
    QVERIFY(up_->supported());
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    QSignalSpy states(up_.get(), &ReportUploader::stateChanged);
    up_->send(body, ctype);
    QCOMPARE(up_->state(), US::Sending);
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Sent, 8000);
    QCOMPARE(up_->reportId(), 1);
    QCOMPARE(states.size(), 2);  // Sending, Sent
    QCOMPARE(hub_->reports.size(), 1);
    const auto& rep = hub_->reports.first();
    QCOMPARE(rep.meta.value("description").toString(), QString("The Session froze after four hours."));
    QCOMPARE(rep.meta.value("os").toString(), QString("Windows 11"));
    QCOMPARE(rep.meta.value("linked_event").toObject().value("title").toString(), QString("Session ended unexpectedly"));
    QCOMPARE(QStringList(rep.files.keys()), (QStringList{"player-logs.txt", "settings.json"}));
    QVERIFY(rep.files.value("player-logs.txt").contains("hub: connected"));
    QVERIFY(rep.authorization.startsWith("Bearer "));
  }

  void progressIsReportedForTheWholeBody() {
    pairAndConnect();
    QByteArray body, ctype;
    makeBody(&body, &ctype, "2026-10-22T10:00:00.000 [I] " + QByteArray(512 * 1024, 'x') + "\n");
    QSignalSpy progress(up_.get(), &ReportUploader::progress);
    up_->send(body, ctype);
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Sent, 15000);
    QVERIFY(!progress.isEmpty());
    const auto last = progress.last();
    QCOMPARE(last.at(0).toLongLong(), last.at(1).toLongLong());
    QVERIFY(last.at(1).toLongLong() >= body.size() - 64);
  }

  void aHubWithoutTheFeatureIsRefusedBeforeAnyRequest() {
    hub_->features = {QStringLiteral("saves_v1")};
    pairAndConnect();
    QVERIFY(!up_->supported());
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    up_->send(body, ctype);
    QCOMPARE(up_->state(), US::Failed);
    QCOMPARE(up_->errorCode(), QString("unsupported"));
    QVERIFY(!up_->errorText().isEmpty());
    QCOMPARE(hub_->reportRequests(), 0);
  }

  void failedKeepsTheBodyAndRetrySucceeds() {
    pairAndConnect();
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    hub_->reportDropNext = 100;  // the connection keeps breaking after the body went out (Qt may resend)
    up_->send(body, ctype);
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Failed, 8000);
    QCOMPARE(up_->errorCode(), QString("network"));
    QVERIFY(up_->errorText().contains("Your text is kept"));
    QCOMPARE(hub_->reports.size(), 0);
    hub_->reportDropNext = 0;
    up_->send(body, ctype);  // "Try again"
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Sent, 8000);
    QCOMPARE(hub_->reports.size(), 1);
    QCOMPARE(up_->errorText(), QString());
  }

  void hubRefusalsMapToErrorCodes_data() {
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("code");
    QTest::newRow("too large") << 413 << "too_large";
    QTest::newRow("rate limited") << 429 << "rate_limited";
    QTest::newRow("server error") << 503 << "server";
  }
  void hubRefusalsMapToErrorCodes() {
    QFETCH(int, status);
    QFETCH(QString, code);
    pairAndConnect();
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    hub_->reportFailNext = 1;
    hub_->reportFailStatus = status;
    up_->send(body, ctype);
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Failed, 8000);
    QCOMPARE(up_->errorCode(), code);
    QVERIFY(!up_->errorText().isEmpty());
  }

  void offlineWaitsAndSendsWhenTheHubIsBack() {
    pairAndConnect();
    const QString hubId = conn_->hubId();
    conn_->disconnectFromHub();
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    up_->send(body, ctype);
    QCOMPARE(up_->state(), US::Offline);
    QCOMPARE(hub_->reportRequests(), 0);
    conn_->connectToProfile(hubId);
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Sent, 12000);
    QCOMPARE(hub_->reports.size(), 1);
  }

  void cancelWhileOfflineDropsTheWaitingReport() {
    pairAndConnect();
    const QString hubId = conn_->hubId();
    conn_->disconnectFromHub();
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    up_->send(body, ctype);
    QCOMPARE(up_->state(), US::Offline);
    up_->cancel();
    QCOMPARE(up_->state(), US::Idle);
    conn_->connectToProfile(hubId);
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 12000);
    QTest::qWait(200);
    QCOMPARE(hub_->reportRequests(), 0);
    QCOMPARE(up_->state(), US::Idle);
  }

  void cancelWhileSendingAbortsTheRequest() {
    pairAndConnect();
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    hub_->holdReports = true;
    up_->send(body, ctype);
    QTRY_COMPARE_WITH_TIMEOUT(hub_->reportsHeld, 1, 8000);
    QCOMPARE(up_->state(), US::Sending);
    up_->cancel();
    QCOMPARE(up_->state(), US::Idle);
    QTest::qWait(200);
    QCOMPARE(up_->state(), US::Idle);  // the aborted request does not turn into Failed
    QCOMPARE(hub_->reports.size(), 0);
  }

  void anExpiredTokenIsRenewedAndTheReportGoesOutAgain() {
    pairAndConnect();
    QByteArray body, ctype;
    makeBody(&body, &ctype);
    hub_->reportUnauthorizedNext = 1;
    up_->send(body, ctype);
    QTRY_COMPARE_WITH_TIMEOUT(up_->state(), US::Sent, 12000);
    QCOMPARE(hub_->reports.size(), 1);
    QVERIFY(hub_->reportRequests() >= 2);
  }
};

QTEST_MAIN(ReportUploadTest)
#include "report_upload_test.moc"
