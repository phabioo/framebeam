// Speed-up (fast-forward) with the fake core (no ROM): speed and ratio choice, GET_FASTFORWARDING, on start,
// audio resampled to real time or dropped, UI frame rate, inhibit_toggle.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>

#include "emulation_runner.h"
#include "libretro_backend.h"

using namespace framebeam::emu;

namespace {
struct Probe {
  std::atomic<int> uiFrames{0};
  std::atomic<qint64> audioFrames{0};
  std::atomic<int> audioChunks{0};
  std::atomic<int> ffPixel{0};
  std::atomic<quint64> lastNr{0};
  std::atomic<bool> started{false};
};

QString makeGame(QTemporaryDir& dir) {
  QFile f(dir.filePath(QStringLiteral("game.bin")));
  f.open(QIODevice::WriteOnly);
  f.write("dummy");
  return f.fileName();
}

void connectProbe(EmulationRunner& r, Probe& p) {
  QObject::connect(&r, &EmulationRunner::frameReady, &r, [&p](const QImage& img, quint64 nr) {
    ++p.uiFrames;
    p.lastNr = nr;
    p.ffPixel = (qBlue(img.pixel(0, 0)) != 0) ? 1 : 0;
  }, Qt::DirectConnection);  // direct: counted in the emulation thread, no event loop needed
  QObject::connect(&r, &EmulationRunner::audioReady, &r, [&p](const QByteArray& pcm, int) {
    p.audioFrames += pcm.size() / 4;
    ++p.audioChunks;
  }, Qt::DirectConnection);
  QObject::connect(&r, &EmulationRunner::started, &r, [&p] { p.started = true; }, Qt::DirectConnection);
}

EmulationRunner::StartRequest request(QTemporaryDir& dir) {
  EmulationRunner::StartRequest req;
  req.corePath = QStringLiteral(FB_FAKE_CORE_PATH);
  req.gamePath = makeGame(dir);
  req.systemDir = dir.filePath(QStringLiteral("sys"));
  req.saveDir = dir.filePath(QStringLiteral("save"));
  QDir().mkpath(req.saveDir);
  return req;
}
}  // namespace

class FastForwardTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { qRegisterMetaType<QImage>(); }
  void init() {
    qunsetenv("FB_FAKE_FF_INHIBIT");
    qunsetenv("FB_FAKE_FF_RATIO");
  }

  void resampleLength() {
    QByteArray pcm(1470 * 4, 0);
    auto* s = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < 1470; ++i) { s[2 * i] = static_cast<qint16>(i); s[2 * i + 1] = static_cast<qint16>(-i); }
    for (const double ratio : {1.5, 2.0, 3.0, 4.0, 6.0, 8.0}) {
      double phase = 0.0;
      qint64 total = 0;
      for (int chunk = 0; chunk < 10; ++chunk) total += EmulationRunner::resampleForSpeedUp(pcm, ratio, &phase).size() / 4;
      QVERIFY2(std::abs(total - 14700.0 / ratio) <= 2.0, qPrintable(QStringLiteral("ratio %1: %2").arg(ratio).arg(total)));
    }
    // Interpolation keeps the ramp: ratio 2 picks every second frame.
    double phase = 0.0;
    const QByteArray out = EmulationRunner::resampleForSpeedUp(pcm, 2.0, &phase);
    const auto* o = reinterpret_cast<const qint16*>(out.constData());
    QCOMPARE(o[2 * 10], qint16(20));
    QCOMPARE(o[2 * 10 + 1], qint16(-20));
  }

  void ratioNormalizes() {
    QCOMPARE(EmulationRunner::normalizeSpeedUpRatio(2.9), 3.0);
    QCOMPARE(EmulationRunner::normalizeSpeedUpRatio(100), 8.0);
    QCOMPARE(EmulationRunner::normalizeSpeedUpRatio(0), 1.5);
  }

  void speedsUpAndChoosesRatio() {
    QTemporaryDir dir;
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    Probe p;
    connectProbe(runner, p);
    auto req = request(dir);
    runner.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(p.started.load(), 5000);
    QVERIFY(runner.supportsFastForward());
    QCOMPARE(runner.fastForwardRatio(), 2.0);  // default

    const auto measure = [&](double* uiRate, double* emuRate, double* audioRate) {
      QTest::qWait(200);
      const int f0 = p.uiFrames.load();
      const quint64 n0 = p.lastNr.load();
      const qint64 a0 = p.audioFrames.load();
      QElapsedTimer t;
      t.start();
      QTest::qWait(1000);
      const double secs = t.elapsed() / 1000.0;
      *uiRate = (p.uiFrames.load() - f0) / secs;
      *emuRate = static_cast<double>(p.lastNr.load() - n0) / secs;
      *audioRate = static_cast<double>(p.audioFrames.load() - a0) / secs;
    };
    double ui = 0, emu = 0, audio = 0;
    measure(&ui, &emu, &audio);
    const double normal = emu;
    QVERIFY2(normal > 30 && normal < 90, qPrintable(QString::number(normal)));
    QCOMPARE(p.ffPixel.load(), 0);

    runner.setFastForward(true);
    measure(&ui, &emu, &audio);
    QCOMPARE(p.ffPixel.load(), 1);  // the core saw GET_FASTFORWARDING == true
    QVERIFY2(emu > normal * 1.4, qPrintable(QStringLiteral("2x: emu %1 vs normal %2").arg(emu).arg(normal)));
    QVERIFY2(ui < 90, qPrintable(QStringLiteral("UI frames %1/s").arg(ui)));  // capped near the base fps
    // Audio resampled to real time: about 44100 frames/s (the core makes 735 per frame, 60 fps), not 2x.
    QVERIFY2(audio > 44100 * 0.5 && audio < 44100 * 1.5, qPrintable(QStringLiteral("audio %1/s").arg(audio)));

    runner.setFastForwardRatio(6.0);
    const double emu2 = emu;
    measure(&ui, &emu, &audio);
    QVERIFY2(emu > emu2 * 1.3, qPrintable(QStringLiteral("6x: emu %1 vs 2x %2").arg(emu).arg(emu2)));
    QVERIFY2(ui < 90, qPrintable(QString::number(ui)));
    QVERIFY2(audio > 44100 * 0.5 && audio < 44100 * 1.5, qPrintable(QStringLiteral("audio %1/s").arg(audio)));

    runner.setFastForward(false);
    measure(&ui, &emu, &audio);
    QCOMPARE(p.ffPixel.load(), 0);
    QVERIFY2(emu < normal * 1.5, qPrintable(QString::number(emu)));
    runner.stop();
  }

  void audioOffDropsAudio() {
    QTemporaryDir dir;
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    Probe p;
    connectProbe(runner, p);
    auto req = request(dir);
    req.speedUpAudio = false;
    runner.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(p.started.load(), 5000);
    QTest::qWait(200);
    QVERIFY(p.audioChunks.load() > 0);
    runner.setFastForward(true);
    QTest::qWait(100);
    const int c0 = p.audioChunks.load();
    QTest::qWait(500);
    QCOMPARE(p.audioChunks.load(), c0);
    runner.setFastForward(false);
    QTRY_VERIFY_WITH_TIMEOUT(p.audioChunks.load() > c0, 3000);
    runner.stop();
  }

  void onStart() {
    QTemporaryDir dir;
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    Probe p;
    connectProbe(runner, p);
    auto req = request(dir);
    req.speedUpOnStart = true;
    req.speedUpRatio = 4.0;
    runner.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(p.started.load(), 5000);
    QVERIFY(runner.fastForward());
    QCOMPARE(runner.fastForwardRatio(), 4.0);
    QTRY_COMPARE_WITH_TIMEOUT(p.ffPixel.load(), 1, 3000);
    runner.stop();
  }

  void onStartIgnoredWhenInhibited() {
    qputenv("FB_FAKE_FF_INHIBIT", "1");
    QTemporaryDir dir;
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    Probe p;
    connectProbe(runner, p);
    auto req = request(dir);
    req.speedUpOnStart = true;
    runner.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(p.started.load(), 5000);
    QVERIFY(!runner.supportsFastForward());
    QVERIFY(!runner.fastForward());
    runner.setFastForward(true);
    QVERIFY(!runner.fastForward());
    runner.stop();
  }

  void inhibitToggleDisablesSupport() {
    qputenv("FB_FAKE_FF_INHIBIT", "1");
    QTemporaryDir dir;
    LibretroBackend be;
    be.setSaveDirectory(dir.path());
    QString err;
    QVERIFY(be.supportsFastForward());  // nothing known before the core speaks
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(makeGame(dir), &err), qPrintable(err));
    QVERIFY(!be.supportsFastForward());
    be.unloadCore();
  }

  void coreRatioDoesNotOverrideChoice() {
    qputenv("FB_FAKE_FF_RATIO", "8");
    QTemporaryDir dir;
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    Probe p;
    connectProbe(runner, p);
    auto req = request(dir);
    req.speedUpRatio = 3.0;
    runner.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(p.started.load(), 5000);
    QVERIFY(runner.supportsFastForward());
    QCOMPARE(runner.fastForwardRatio(), 3.0);
    runner.stop();
  }
};

QTEST_GUILESS_MAIN(FastForwardTest)
#include "fast_forward_test.moc"
