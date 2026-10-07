// AIMD bitrate controller and the rx report message of the fb-diag DataChannel (ADR 0012 D5).
#include <QtTest>

#include "processguard.h"

#include "bitratecontroller.h"

using namespace framebeam;

namespace {
const RxReport kGood{0.0, 2000.0};
const RxReport kBad{0.10, 1500.0};
const RxReport kMild{0.03, 1800.0};  // between 1 % and 5 %
}  // namespace

class BitrateControllerTest : public QObject {
  Q_OBJECT
 private slots:
  void startsAt2000() {
    BitrateController c;
    QCOMPARE(c.targetKbps(), 2000);
  }

  void highLossMultipliesBy07() {
    BitrateController c;
    QCOMPARE(c.report(QStringLiteral("a"), kBad, 1000), std::optional<int>(1400));
    QCOMPARE(c.targetKbps(), 1400);
    QCOMPARE(c.report(QStringLiteral("a"), kBad, 3000), std::optional<int>(980));
  }

  void lossAtExactlyFivePercentIsNotHigh() {
    BitrateController c;
    QVERIFY(!c.report(QStringLiteral("a"), RxReport{0.05, 1000}, 1000));
    QCOMPARE(c.targetKbps(), 2000);
  }

  void neverBelowMinimum() {
    BitrateController c;
    qint64 t = 0;
    for (int i = 0; i < 20; ++i) {
      c.report(QStringLiteral("a"), kBad, t += 2000);
    }
    QCOMPARE(c.targetKbps(), 300);
    QVERIFY(!c.report(QStringLiteral("a"), kBad, t += 2000));  // stays, no change reported
  }

  void increaseAfterThreeCleanReports() {
    BitrateController c(BitrateController::Config{.startKbps = 1000});
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 1000));
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 2000));
    QCOMPARE(c.report(QStringLiteral("a"), kGood, 3000), std::optional<int>(1100));
    // The streak starts over after a change.
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 4000));
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 5000));
    QCOMPARE(c.report(QStringLiteral("a"), kGood, 6000), std::optional<int>(1210));
  }

  void mildLossBreaksTheStreakAndHolds() {
    BitrateController c(BitrateController::Config{.startKbps = 1000});
    c.report(QStringLiteral("a"), kGood, 1000);
    c.report(QStringLiteral("a"), kGood, 2000);
    QVERIFY(!c.report(QStringLiteral("a"), kMild, 3000));  // neither decrease nor increase
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 4000));
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 5000));
    QCOMPARE(c.report(QStringLiteral("a"), kGood, 6000), std::optional<int>(1100));
    QCOMPARE(c.targetKbps(), 1100);
  }

  void neverAboveMaximum() {
    BitrateController c;
    qint64 t = 0;
    for (int i = 0; i < 60; ++i) {
      c.report(QStringLiteral("a"), kGood, t += 1000);
    }
    QCOMPARE(c.targetKbps(), 4000);
    QVERIFY(!c.report(QStringLiteral("a"), kGood, t += 1000));
  }

  void atMostOneChangePerTwoSeconds() {
    BitrateController c;
    QCOMPARE(c.report(QStringLiteral("a"), kBad, 10000), std::optional<int>(1400));
    QVERIFY(!c.report(QStringLiteral("a"), kBad, 11000));  // 1 s later: held
    QVERIFY(!c.report(QStringLiteral("a"), kBad, 11999));
    QCOMPARE(c.targetKbps(), 1400);
    QCOMPARE(c.report(QStringLiteral("a"), kBad, 12000), std::optional<int>(980));  // 2 s: allowed again
  }

  void heldIncreaseFiresOnTheNextAllowedReport() {
    BitrateController c(BitrateController::Config{.startKbps = 1000});
    QCOMPARE(c.report(QStringLiteral("a"), kBad, 1000), std::optional<int>(700));
    // clean reports inside the 2 s window collect the streak but do not change anything
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 1500));
    QVERIFY(!c.report(QStringLiteral("a"), kGood, 2000));
    QCOMPARE(c.report(QStringLiteral("a"), kGood, 3000), std::optional<int>(770));
  }

  void worstViewerWins() {
    BitrateController c;
    QVERIFY(!c.report(QStringLiteral("good"), kGood, 1000));
    QCOMPARE(c.report(QStringLiteral("bad"), kBad, 2000), std::optional<int>(1400));
    // Both keep reporting: the good one clean, the bad one with mild loss. Never up while somebody is not clean.
    QVERIFY(!c.report(QStringLiteral("good"), kGood, 3000));
    QCOMPARE(c.report(QStringLiteral("bad"), kBad, 4000), std::optional<int>(980));
    for (qint64 t = 5000; t <= 12000; t += 1000) {
      QVERIFY(!c.report(QStringLiteral("good"), kGood, t));
      QVERIFY(!c.report(QStringLiteral("bad"), kMild, t));
    }
    QCOMPARE(c.targetKbps(), 980);
  }

  void increaseNeedsEveryViewerClean() {
    BitrateController c(BitrateController::Config{.startKbps = 1000});
    qint64 t = 0;
    c.report(QStringLiteral("a"), kGood, t += 1000);
    c.report(QStringLiteral("b"), kMild, t);  // b is not clean
    for (int i = 0; i < 5; ++i) {
      QVERIFY(!c.report(QStringLiteral("a"), kGood, t += 1000));
    }
    QCOMPARE(c.targetKbps(), 1000);
    // b cleans up: three clean reports of b, then the increase fires.
    QVERIFY(!c.report(QStringLiteral("b"), kGood, t += 1000));
    QVERIFY(!c.report(QStringLiteral("b"), kGood, t += 1000));
    QCOMPARE(c.report(QStringLiteral("b"), kGood, t += 1000), std::optional<int>(1100));
  }

  void leavingViewerIsRemoved() {
    BitrateController c;
    QCOMPARE(c.report(QStringLiteral("bad"), kBad, 1000), std::optional<int>(1400));
    c.removeViewer(QStringLiteral("bad"));
    QVERIFY(!c.report(QStringLiteral("good"), kGood, 3000));
    QVERIFY(!c.report(QStringLiteral("good"), kGood, 4000));
    // Without the bad viewer's loss the clean viewer can raise the rate again.
    QCOMPARE(c.report(QStringLiteral("good"), kGood, 5000), std::optional<int>(1540));
  }

  void resetRestoresStart() {
    BitrateController c;
    c.report(QStringLiteral("a"), kBad, 1000);
    c.reset();
    QCOMPARE(c.targetKbps(), 2000);
    QCOMPARE(c.report(QStringLiteral("a"), kBad, 1000), std::optional<int>(1400));  // interval state cleared too
    c.reset(100);
    QCOMPARE(c.targetKbps(), 300);  // clamped
  }

  void rxReportRoundTrip() {
    const QByteArray m = makeRxReport(0.0123456, 1999.6);
    const auto r = parseRxReport(m);
    QVERIFY(r.has_value());
    QVERIFY(qAbs(r->loss - 0.0123) < 1e-9);
    QCOMPARE(r->kbps, 2000.0);
    QVERIFY(m.contains("\"t\":\"rx\""));
  }

  void rxReportIgnoresMalformed_data() {
    QTest::addColumn<QByteArray>("message");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not json") << QByteArray("rx");
    QTest::newRow("array") << QByteArray("[1,2]");
    QTest::newRow("other type") << QByteArray("{\"t\":\"ping\",\"loss\":0,\"kbps\":1}");
    QTest::newRow("no type") << QByteArray("{\"loss\":0,\"kbps\":1}");
    QTest::newRow("missing loss") << QByteArray("{\"t\":\"rx\",\"kbps\":1}");
    QTest::newRow("missing kbps") << QByteArray("{\"t\":\"rx\",\"loss\":0}");
    QTest::newRow("string loss") << QByteArray("{\"t\":\"rx\",\"loss\":\"0.1\",\"kbps\":1}");
    QTest::newRow("loss above 1") << QByteArray("{\"t\":\"rx\",\"loss\":1.5,\"kbps\":1}");
    QTest::newRow("negative loss") << QByteArray("{\"t\":\"rx\",\"loss\":-0.1,\"kbps\":1}");
    QTest::newRow("negative kbps") << QByteArray("{\"t\":\"rx\",\"loss\":0,\"kbps\":-5}");
    QTest::newRow("null") << QByteArray("{\"t\":\"rx\",\"loss\":null,\"kbps\":null}");
    QTest::newRow("oversized") << (QByteArray("{\"t\":\"rx\",\"loss\":0,\"kbps\":1,\"x\":\"") + QByteArray(600, 'a') + QByteArray("\"}"));
  }
  void rxReportIgnoresMalformed() {
    QFETCH(QByteArray, message);
    QVERIFY(!parseRxReport(message).has_value());
  }

  void rxReportIgnoresUnknownFields() {
    const auto r = parseRxReport("{\"t\":\"rx\",\"loss\":0.02,\"kbps\":900,\"future\":true}");
    QVERIFY(r.has_value());
    QCOMPARE(r->loss, 0.02);
  }
};

FB_TEST_MAIN(BitrateControllerTest)
#include "bitratecontroller_test.moc"
