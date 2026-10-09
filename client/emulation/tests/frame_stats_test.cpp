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

  void readbackIsAveragedOverAllFramesWithCount() {
    FrameTimingStats s;
    // 100 frames at 100/s; every 5th reads back for 2.5 ms, the others (skipped video) do not.
    for (int i = 0; i < 100; ++i) s.recordFrame(i * 10, 3.0, i % 5 == 0 ? 2.5 : 0.0);
    const auto snap = s.snapshot(995);
    QVERIFY(snap.valid);
    QVERIFY(qAbs(snap.readbackMs - 0.5) < 0.01);         // mean over all frames, not the last value
    QVERIFY(qAbs(snap.readbackMsPerRead - 2.5) < 0.01);  // per actual readback
    QVERIFY(qAbs(snap.readbacksPerSec - 20.0) < 0.5);
    QVERIFY(qAbs(snap.emuMs - 2.5) < 0.01);
  }

  void readbackNeverExceedsTheFrame() {
    FrameTimingStats s;
    s.recordFrame(10, 4.0, 9.0);  // bogus readback larger than the frame: clamped
    const auto snap = s.snapshot(10);
    QVERIFY(snap.valid);
    QCOMPARE(snap.readbackMs, 4.0);
    QCOMPARE(snap.emuMs, 0.0);
  }

  // GPU copy (Session encode texture, ADR 0019): its own part of the frame, excluded from the emulation part.
  void gpuCopyIsExcludedFromEmuAndCountsOnlyCaptures() {
    FrameTimingStats s;
    // 100 frames at 100/s, 10 ms each, 2 ms readback. Every 2nd frame blits and captures (1.5 ms), the others only blit
    // (0.5 ms) and capture nothing.
    for (int i = 0; i < 100; ++i) s.recordFrame(i * 10, 10.0, 2.0, i % 2 == 0 ? 1.5 : 0.5, i % 2 == 0);
    const auto snap = s.snapshot(995);
    QVERIFY(snap.valid);
    QVERIFY(qAbs(snap.frameMs - 10.0) < 0.01);
    QVERIFY(qAbs(snap.readbackMs - 2.0) < 0.01);
    QVERIFY(qAbs(snap.gpuCopyMs - 1.0) < 0.01);          // mean over all frames, like the readback
    QVERIFY(qAbs(snap.emuMs - 7.0) < 0.01);              // 10 - 2 - 1
    QVERIFY(qAbs(snap.emuMs + snap.readbackMs + snap.gpuCopyMs - snap.frameMs) < 0.001);
    QVERIFY2(qAbs(snap.gpuCopiesPerSec - 50.0) < 0.5, qPrintable(QString::number(snap.gpuCopiesPerSec)));  // captures only
    QVERIFY(snap.history.last().gpuCaptured == false && snap.history.at(snap.history.size() - 2).gpuCaptured);
    QVERIFY(qAbs(snap.history.last().gpuCopyMs - 0.5) < 0.001);
  }

  void blitWithoutCaptureIsNotACopyPerSecond() {
    FrameTimingStats s;
    for (int i = 0; i < 50; ++i) s.recordFrame(i * 20, 8.0, 1.0, 0.4, false);  // e.g. the target is not ready yet
    const auto snap = s.snapshot(990);
    QVERIFY(snap.valid);
    QVERIFY(qAbs(snap.gpuCopyMs - 0.4) < 0.01);
    QCOMPARE(snap.gpuCopiesPerSec, 0.0);
  }

  void withoutGpuEncodingNothingChanges() {
    FrameTimingStats s;
    for (int i = 0; i < 100; ++i) s.recordFrame(i * 10, 6.0, 1.0);  // the old three-argument form
    const auto snap = s.snapshot(995);
    QVERIFY(snap.valid);
    QCOMPARE(snap.gpuCopyMs, 0.0);
    QCOMPARE(snap.gpuCopiesPerSec, 0.0);
    QVERIFY(qAbs(snap.emuMs - 5.0) < 0.01);
    QVERIFY(!snap.history.last().gpuCaptured);
  }

  void gpuCopyNeverExceedsTheRestOfTheFrame() {
    FrameTimingStats s;
    s.recordFrame(10, 4.0, 1.0, 9.0, true);  // bogus copy time larger than the frame: clamped to what is left
    const auto snap = s.snapshot(10);
    QVERIFY(snap.valid);
    QCOMPARE(snap.readbackMs, 1.0);
    QCOMPARE(snap.gpuCopyMs, 3.0);
    QCOMPARE(snap.emuMs, 0.0);
    s.recordFrame(20, 4.0, 1.0, -2.0, false);  // negative: ignored
    QCOMPARE(s.snapshot(20).history.last().gpuCopyMs, 0.0f);
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
