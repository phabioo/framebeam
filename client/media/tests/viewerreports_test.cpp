// ViewerReports: the host keeps the latest rx report per viewer and copies it into ViewerLinkStats (0.6 D7).
#include <QtTest>

#include "viewerreports.h"

using namespace framebeam;

class ViewerReportsTest : public QObject {
  Q_OBJECT

  static RxReport report(double loss, double kbps, std::optional<double> fps = std::nullopt, const QString& dec = QString()) {
    RxReport r;
    r.loss = loss;
    r.kbps = kbps;
    r.fps = fps;
    r.decoder = dec;
    return r;
  }

 private slots:
  void noReportYetLeavesTheLinkEmpty() {
    ViewerReports reports;
    ViewerLinkStats l;
    l.viewerId = QStringLiteral("a");
    l.hasReport = true;  // stale values are cleared
    l.reportKbps = 99;
    reports.applyTo(l);
    QVERIFY(!l.hasReport);
    QCOMPARE(l.reportKbps, 0.0);
    QVERIFY(!l.reportFps.has_value());
  }

  void latestReportWinsPerViewer() {
    ViewerReports reports;
    reports.set(QStringLiteral("a"), report(0.10, 1500, 55.0, QStringLiteral("h264")));
    reports.set(QStringLiteral("b"), report(0.00, 5800, 59.9, QStringLiteral("h264_cuvid")));
    reports.set(QStringLiteral("a"), report(0.01, 2000, 59.0, QStringLiteral("h264")));
    QList<ViewerLinkStats> links(2);
    links[0].viewerId = QStringLiteral("a");
    links[1].viewerId = QStringLiteral("b");
    reports.applyTo(links);
    QVERIFY(links[0].hasReport && links[1].hasReport);
    QCOMPARE(links[0].reportLoss, 0.01);
    QCOMPARE(links[0].reportKbps, 2000.0);
    QCOMPARE(*links[0].reportFps, 59.0);
    QCOMPARE(links[1].reportDecoder, QStringLiteral("h264_cuvid"));
    QCOMPARE(links[1].reportKbps, 5800.0);
  }

  void oldPlayerWithoutOptionalFields() {
    ViewerReports reports;
    reports.set(QStringLiteral("a"), report(0.0, 1000, 59.0, QStringLiteral("h264")));
    reports.set(QStringLiteral("a"), report(0.02, 900));  // an older Player: no fps, no decoder (not carried over)
    ViewerLinkStats l;
    l.viewerId = QStringLiteral("a");
    reports.applyTo(l);
    QVERIFY(l.hasReport);
    QVERIFY(!l.reportFps.has_value());
    QVERIFY(l.reportDecoder.isEmpty());
    QCOMPARE(l.reportLoss, 0.02);
  }

  void removedViewerForgotten() {
    ViewerReports reports;
    reports.set(QStringLiteral("a"), report(0.0, 1000));
    QVERIFY(reports.has(QStringLiteral("a")));
    reports.remove(QStringLiteral("a"));
    QVERIFY(!reports.has(QStringLiteral("a")));
    reports.set(QStringLiteral("b"), report(0.0, 1));
    reports.clear();
    QVERIFY(!reports.has(QStringLiteral("b")));
  }
};

QTEST_APPLESS_MAIN(ViewerReportsTest)
#include "viewerreports_test.moc"
