// FrameTimingStats: fps over a sliding 1 s window, frame time split (emulation / readback) and the 5 s history.
#include <QtTest>

#include "frame_stats.h"

using framebeam::emu::FrameTimingStats;

class FrameStatsTest : public QObject {
  Q_OBJECT

  static void feed(FrameTimingStats& s, qint64 fromMs, qint64 toMs, double stepMs, double totalMs, double readbackMs) {
    for (double t = static_cast<double>(fromMs); t <= static_cast<double>(toMs); t += stepMs) {
      s.recordFrame(static_cast<qint64>(t), totalMs, readbackMs);
    }
  }

 private slots:
  void emptyIsInvalid() {
    FrameTimingStats s;
    const auto snap = s.snapshot(1000);
    QVERIFY(!snap.valid);
    QCOMPARE(snap.fps, 0.0);
    QVERIFY(snap.history.isEmpty());
  }

  void fpsFromSlidingWindow() {
    FrameTimingStats s;
    feed(s, 0, 3000, 1000.0 / 60.0, 6.0, 0.0);
    const auto snap = s.snapshot(3000);
    QVERIFY(snap.valid);
    QVERIFY2(qAbs(snap.fps - 60.0) < 1.5, qPrintable(QString::number(snap.fps)));
  }

  void fpsFollowsTheLastSecondOnly() {
    FrameTimingStats s;
    feed(s, 0, 2000, 1000.0 / 60.0, 6.0, 0.0);  // 60 fps
    feed(s, 2017, 3000, 1000.0 / 30.0, 6.0, 0.0);  // then 30 fps
    const auto snap = s.snapshot(3000);
    QVERIFY2(qAbs(snap.fps - 30.0) < 2.0, qPrintable(QString::number(snap.fps)));
  }

  void stallShowsAsLowFpsThenInvalid() {
    FrameTimingStats s;
    feed(s, 0, 1000, 1000.0 / 60.0, 6.0, 0.0);
    QVERIFY(s.snapshot(1000).valid);
    QVERIFY(!s.snapshot(2500).valid);  // nothing inside the last second (paused or stalled)
  }

  void frameTimeSplitSoftwareAndHardware() {
    FrameTimingStats sw;
    feed(sw, 0, 1000, 16.0, 6.2, 0.0);
    const auto a = sw.snapshot(1000);
    QVERIFY(qAbs(a.frameMs - 6.2) < 0.01 && qAbs(a.emuMs - 6.2) < 0.01 && a.readbackMs == 0.0);

    FrameTimingStats hw;
    feed(hw, 0, 1000, 16.0, 9.4, 2.3);
    const auto b = hw.snapshot(1000);
    QVERIFY(qAbs(b.frameMs - 9.4) < 0.01);
    QVERIFY(qAbs(b.readbackMs - 2.3) < 0.01);
    QVERIFY(qAbs(b.emuMs - 7.1) < 0.01);
    QVERIFY(qAbs(b.emuMs + b.readbackMs - b.frameMs) < 0.001);
  }

  void readbackNeverExceedsTheFrame() {
    FrameTimingStats s;
    s.recordFrame(10, 4.0, 9.0);  // bogus readback larger than the frame: clamped
    const auto snap = s.snapshot(10);
    QVERIFY(snap.valid);
    QCOMPARE(snap.readbackMs, 4.0);
    QCOMPARE(snap.emuMs, 0.0);
  }

  void historyKeepsFiveSeconds() {
    FrameTimingStats s;
    feed(s, 0, 9000, 20.0, 5.0, 0.0);
    const auto snap = s.snapshot(9000);
    QVERIFY(!snap.history.isEmpty());
    QVERIFY(snap.history.first().atMs >= 4000);
    QVERIFY(snap.history.last().atMs <= 9000);
    QVERIFY(snap.history.size() >= 240 && snap.history.size() <= 260);
    for (qsizetype i = 1; i < snap.history.size(); ++i) QVERIFY(snap.history.at(i).atMs >= snap.history.at(i - 1).atMs);
  }

  void resetClears() {
    FrameTimingStats s;
    feed(s, 0, 500, 16.0, 5.0, 1.0);
    s.reset();
    QVERIFY(!s.snapshot(500).valid);
  }
};

QTEST_APPLESS_MAIN(FrameStatsTest)
#include "frame_stats_test.moc"
