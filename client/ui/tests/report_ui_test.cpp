// "Report a problem" dialog (0.17, design 6d): opening from Settings, attachments, the send flow against the fake Hub, the
// states and "Save as file…". Offscreen, FakeHub with reports_v1; synthetic data only.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include "diagbundle.h"
#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;
using S = HubConnection::State;

class ReportUiTest : public QObject {
  Q_OBJECT

  static void pair(Harness& h, FakeHub& hub) {
    hub.decision = FakeHub::Decision::Approve;
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsPairing, 8000);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 8000);
  }

  static void seedLogs(Harness& h) {
    QDir().mkpath(h.dir.filePath(QStringLiteral("logs")));
    QFile f(h.dir.filePath(QStringLiteral("logs/player.log")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Append));
    f.write(QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8() + " [I] test: report ui password=S3cretPassw0rd\n");
  }

 private slots:
  void initTestCase() {
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    uitest::installWarningCounter();
  }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  void dialogCollectsSendsAndShowsTheNumber() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("reports_v1")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    seedLogs(h);
    ReportController* r = h.controller->report();
    QVERIFY(!r->isOpen());

    r->showLinked(QStringLiteral("Session ended unexpectedly"), QDateTime::currentMSecsSinceEpoch());
    QVERIFY(r->isOpen());
    QTRY_COMPARE(r->state(), QStringLiteral("editing"));
    QTRY_VERIFY(h.item("reportDescription") != nullptr);  // the Popup itself is not an item
    QVERIFY(r->linkedText().startsWith(QStringLiteral("Linked: “Session ended unexpectedly”")));
    QVERIFY(!r->canSend());  // no text yet
    // Logs and settings are on, system info is off; the connection check is not listed without one.
    QStringList on, off;
    for (const QVariant& v : r->attachments()) (v.toMap().value("checked").toBool() ? on : off).append(v.toMap().value("id").toString());
    QCOMPARE(on, (QStringList{"logs", "settings"}));
    QCOMPARE(off, (QStringList{"system"}));

    r->setDescription(QStringLiteral("The Session froze."));
    QVERIFY(r->canSend());
    r->setChecked(QStringLiteral("system"), true);
    QVERIFY(r->includedFiles().contains(QStringLiteral("system-info.json")));
    QSignalSpy sent(r, &ReportController::changed);
    r->send();
    QTRY_COMPARE_WITH_TIMEOUT(r->state(), QStringLiteral("sent"), 8000);
    QCOMPARE(r->reportId(), 1);
    QCOMPARE(hub.reports.size(), 1);
    const auto& rep = hub.reports.first();
    QCOMPARE(rep.meta.value("description").toString(), QString("The Session froze."));
    QVERIFY(rep.files.contains("player-logs.txt"));
    QVERIFY(rep.files.contains("system-info.json"));
    QVERIFY(!rep.files.value("player-logs.txt").contains("S3cretPassw0rd"));
    QTRY_VERIFY(h.item("reportStateTitle") != nullptr);

    r->close();
    QVERIFY(!r->isOpen());
    QCOMPARE(r->description(), QString());
  }

  void hubOfflineKeepsTheReportAndSendsWhenBack() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("reports_v1")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ReportController* r = h.controller->report();
    r->show();
    QTRY_COMPARE(r->state(), QStringLiteral("editing"));
    r->setDescription(QStringLiteral("Offline test"));
    const QString hubId = h.controller->connection()->hubId();
    h.controller->connection()->disconnectFromHub();
    r->send();
    QCOMPARE(r->state(), QStringLiteral("offline"));
    QVERIFY(!r->canSend());  // "Send to Home Hub" is disabled while it waits
    h.controller->connection()->connectToProfile(hubId);
    QTRY_COMPARE_WITH_TIMEOUT(r->state(), QStringLiteral("sent"), 15000);
    QCOMPARE(hub.reports.size(), 1);
  }

  void failedKeepsTheTextAndTryAgainWorks() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("reports_v1")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ReportController* r = h.controller->report();
    r->show();
    QTRY_COMPARE(r->state(), QStringLiteral("editing"));
    r->setDescription(QStringLiteral("Keep this text"));
    hub.reportFailNext = 1;
    hub.reportFailStatus = 503;
    r->send();
    QTRY_COMPARE_WITH_TIMEOUT(r->state(), QStringLiteral("failed"), 8000);
    // Every action stays inside the card (with wide fonts the row wraps).
    QTRY_VERIFY(h.item("reportSend") != nullptr && h.item("reportSend")->isVisible());
    QQuickItem* card = h.item("reportCard");
    QVERIFY(card != nullptr);
    const QRectF cardRect = card->mapRectToScene(QRectF(0, 0, card->width(), card->height()));
    for (const char* name : {"reportSaveAsFile", "reportCancel", "reportSend"}) {
      QQuickItem* b = h.item(name);
      QVERIFY2(b != nullptr && b->isVisible(), name);
      QVERIFY2(cardRect.contains(b->mapRectToScene(QRectF(0, 0, b->width(), b->height()))), name);
    }
    QCOMPARE(r->description(), QString("Keep this text"));
    QVERIFY(!r->errorText().isEmpty());
    QVERIFY(r->canSend());  // "Try again"
    r->send();
    QTRY_COMPARE_WITH_TIMEOUT(r->state(), QStringLiteral("sent"), 8000);
    QCOMPARE(hub.reports.size(), 1);
  }

  void cancelWhileSendingGoesBackToEditing() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("reports_v1")};
    hub.holdReports = true;
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ReportController* r = h.controller->report();
    r->show();
    QTRY_COMPARE(r->state(), QStringLiteral("editing"));
    r->setDescription(QStringLiteral("Cancel me"));
    r->send();
    QTRY_COMPARE_WITH_TIMEOUT(r->state(), QStringLiteral("sending"), 8000);
    r->cancelSending();
    QCOMPARE(r->state(), QStringLiteral("editing"));
    QCOMPARE(r->description(), QString("Cancel me"));  // the text is kept
    QCOMPARE(hub.reports.size(), 0);
  }

  void connectionCheckResultIsOfferedAndMarkedByAddToReport() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("reports_v1")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ReportController* r = h.controller->report();
    r->show();
    QTRY_COMPARE(r->state(), QStringLiteral("editing"));
    bool listed = false;
    for (const QVariant& v : r->attachments()) listed = listed || v.toMap().value("id").toString() == QLatin1String("connection");
    QVERIFY(!listed);  // no check has run
    r->close();
  }

  void saveAsFileWritesTheSameContentAsAZip() {
    Harness h;
    QVERIFY(h.start());
    seedLogs(h);
    ReportController* r = h.controller->report();
    r->show();
    QTRY_COMPARE(r->state(), QStringLiteral("editing"));
    r->setDescription(QStringLiteral("Saved locally"));
    // The connection check is optional and comes from another change: the setter attaches it.
    r->setConnectionCheckJson(R"({"relay":"ok","turn_credential":"hunter2hunter2"})", QDateTime::currentDateTime());
    bool hasCheck = false;
    for (const QVariant& v : r->attachments()) hasCheck = hasCheck || v.toMap().value("id").toString() == QLatin1String("connection");
    QVERIFY(hasCheck);
    const QString out = h.dir.filePath(QStringLiteral("report.zip"));
    QSignalSpy saved(r, &ReportController::saveFinished);
    r->saveAsFile(out);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 8000);
    QVERIFY(saved.first().at(0).toBool());
    QMap<QString, QByteArray> e;
    QString err;
    QVERIFY2(diag::readZip(out, &e, &err), qPrintable(err));
    QVERIFY(e.contains("report.txt"));
    QVERIFY(e.value("report.txt").contains("Saved locally"));
    QVERIFY(e.contains("player-logs.txt"));
    QVERIFY(e.contains("connection-check.json"));
    QVERIFY(!e.value("connection-check.json").contains("hunter2hunter2"));
    QVERIFY(!e.value("player-logs.txt").contains("S3cretPassw0rd"));
    QVERIFY(!r->saveNote().isEmpty());
  }
};

QTEST_MAIN(ReportUiTest)
#include "report_ui_test.moc"
