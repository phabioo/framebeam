// FramePacer: which emulated frames go to the stream during speed-up and how late each is against its base-rate slot.
#include <QtTest>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "frame_pacer.h"

using framebeam::emu::FramePacer;

namespace {
constexpr int64_t kPeriod = 16'666'667;  // 60 fps

struct Sent {
  int64_t arrival;
  int64_t stamp;  // arrival - lateness, what the Session uses
};

// Emulated frames at `ratio` times real time for `seconds`; each frame takes `runNs` to emulate.
std::vector<Sent> simulate(double ratio, double seconds, int64_t runNs, int* emulated) {
  FramePacer p;
  const int64_t step = static_cast<int64_t>(kPeriod / ratio);
  p.reset(0);
  std::vector<Sent> sent;
  *emulated = 0;
  for (int64_t t = 0; t < static_cast<int64_t>(seconds * 1e9); t += step) {
    ++*emulated;
    if (!p.wantsFrame(t)) continue;
    const int64_t arrival = t + runNs;
    sent.push_back({arrival, arrival - p.delivered(t, arrival, kPeriod)});
  }
  return sent;
}
}  // namespace

class FramePacerTest : public QObject {
  Q_OBJECT
 private slots:
  void baseRateAndEvenStamps_data() {
    QTest::addColumn<double>("ratio");
    for (double r : {1.5, 2.0, 3.0, 4.0, 6.0, 8.0}) QTest::newRow(qPrintable(QString::number(r))) << r;
  }
  void baseRateAndEvenStamps() {
    QFETCH(double, ratio);
    int emulated = 0;
    const auto sent = simulate(ratio, 5.0, 1'000'000, &emulated);
    QVERIFY(emulated > 5.0 * 60 * ratio - 2);  // the core really ran faster than real time
    QVERIFY2(std::abs(static_cast<int>(sent.size()) - 300) <= 1, qPrintable(QString::number(sent.size())));  // 60 fps x 5 s
    for (size_t i = 1; i < sent.size(); ++i) {
      // Slot spacing is exact; arrival jitter is at most one emulated step.
      QVERIFY2(std::llabs((sent[i].stamp - sent[i - 1].stamp) - kPeriod) <= 1, qPrintable(QString::number(i)));
      QVERIFY(sent[i].arrival - sent[i].stamp >= 0 && sent[i].arrival - sent[i].stamp <= kPeriod);
    }
  }
  void lateFrameResyncs() {
    FramePacer p;
    p.reset(0);
    QVERIFY(p.wantsFrame(0));
    QCOMPARE(p.delivered(0, 1000, kPeriod), int64_t{1000});
    QVERIFY(!p.wantsFrame(kPeriod / 2));
    QVERIFY(p.wantsFrame(kPeriod));
    // Emulation stalled for 5 periods: the lateness is capped and the schedule restarts instead of bursting.
    QCOMPARE(p.delivered(5 * kPeriod, 6 * kPeriod, kPeriod), kPeriod);
    QVERIFY(!p.wantsFrame(5 * kPeriod + kPeriod / 2));
    QVERIFY(p.wantsFrame(6 * kPeriod));
  }
};

QTEST_APPLESS_MAIN(FramePacerTest)
#include "frame_pacer_test.moc"
