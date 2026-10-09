// Speed-up with the real melonDS DS core and the homebrew test ROM (NEEDS_CORE: return code 77 without a core):
// ratio 2 must really emulate faster than ratio 1, and the audio stays at the real-time rate.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>

#include "core_locator.h"
#include "emulation_runner.h"
#include "libretro_backend.h"
#include "system_manifest.h"

using namespace framebeam::emu;

class CoreSpeedTest : public QObject {
  Q_OBJECT
  SystemManifest m_nds;
  QString m_corePath;
  QTemporaryDir m_dirs;
  std::atomic<qint64> m_audioFrames{0};
  std::atomic<int> m_rate{0};

  struct Rates { double fps = 0; double audio = 0; };
  Rates measure(EmulationRunner& runner, int ms) {
    QTest::qWait(300);  // settle after a change of speed
    const qint64 a0 = m_audioFrames.load();
    const auto t0 = EmulationRunner::monotonicMs();
    QTest::qWait(ms);
    const auto snap = runner.timing();
    const double secs = static_cast<double>(EmulationRunner::monotonicMs() - t0) / 1000.0;
    Rates r;
    r.fps = snap.fps;  // frames inside the last second
    r.audio = static_cast<double>(m_audioFrames.load() - a0) / secs;
    return r;
  }

 private slots:
  void initTestCase() {
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("system"))));
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("save"))));
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    m_nds = *reg.resolve(QStringLiteral("nds"), QStringLiteral("melondsds"));
    const CoreLocation loc = CoreLocator().locate(m_nds);
    QVERIFY2(loc.found(), "Core not found");
    m_corePath = loc.path;
    if (!qEnvironmentVariableIsSet("FRAMEBEAM_DISABLE_HW_RENDER")) qputenv("FRAMEBEAM_DISABLE_HW_RENDER", "1");  // software path unless overridden
  }

  void ratioTwoIsFaster() { runScenario(0); }
  void ratioTwoIsFasterWithCoarseTimer() { runScenario(16); }

 private:
  void runScenario(int quantumMs) {
    EmulationRunner::sleepQuantumMsForTest = quantumMs;
    m_audioFrames = 0;
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QObject::connect(&runner, &EmulationRunner::audioReady, &runner, [this](const QByteArray& pcm, int rate) {
      m_audioFrames += pcm.size() / 4;
      m_rate = rate;
    }, Qt::DirectConnection);
    QSignalSpy started(&runner, &EmulationRunner::started);
    EmulationRunner::StartRequest req;
    req.corePath = m_corePath;
    req.gamePath = QStringLiteral(FB_TEST_ROM_PATH);
    req.systemDir = m_dirs.filePath(QStringLiteral("system"));
    req.saveDir = m_dirs.filePath(QStringLiteral("save"));
    req.coreOptions = m_nds.coreOptions;
    runner.start(req);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 20000);
    QVERIFY(runner.supportsFastForward());

    const Rates normal = measure(runner, 1500);
    QVERIFY2(normal.fps > 40 && normal.fps < 80, qPrintable(QString::number(normal.fps)));
    const double realAudio = static_cast<double>(m_rate.load());
    QVERIFY(realAudio > 8000);
    QVERIFY2(std::abs(normal.audio - realAudio) < realAudio * 0.25, qPrintable(QString::number(normal.audio)));

    const double ratios[3] = {2.0, 4.0, 8.0};
    for (int i = 0; i < 3; ++i) {
      runner.setFastForwardRatio(ratios[i]);
      runner.setFastForward(true);
      const Rates fast = measure(runner, 1500);
      qInfo("quantum %d ratio %.0f: %.1f fps (x%.2f), audio %.0f Hz (real %.0f)", quantumMs, ratios[i], fast.fps, fast.fps / normal.fps, fast.audio, realAudio);
      QVERIFY2(fast.fps >= normal.fps * ratios[i] * 0.8,
               qPrintable(QStringLiteral("ratio %1: fast %2 vs normal %3 fps").arg(ratios[i]).arg(fast.fps).arg(normal.fps)));
      QVERIFY2(std::abs(fast.audio - realAudio) < realAudio * 0.25, qPrintable(QStringLiteral("audio %1 Hz").arg(fast.audio)));
    }
    runner.stop();
    EmulationRunner::sleepQuantumMsForTest = 0;
  }
};

QTEST_MAIN(CoreSpeedTest)
#include "core_speed_test.moc"
