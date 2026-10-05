// Headless-Test mit echtem melonDS-DS-Core und selbst erzeugter Homebrew-Test-ROM.
// Ohne FRAMEBEAM_MELONDS_DS_CORE: Rueckgabecode 77 (ctest: SKIP).
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core_locator.h"
#include "emulation_runner.h"
#include "libretro_backend.h"
#include "system_manifest.h"

using namespace framebeam::emu;

namespace {
struct RunResult {
  QImage frame;
  QByteArray hash;
  qint64 audioBytes = 0;
  QString error;
};
}  // namespace

class CoreTest : public QObject {
  Q_OBJECT
  SystemManifest m_nds;
  QString m_corePath;
  QTemporaryDir m_dirs;

  // Frame-Hash ueber Pixeldaten (unabhaengig von Zeilenpadding).
  static QByteArray hashOf(const QImage& img) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (int y = 0; y < img.height(); ++y)
      h.addData(QByteArrayView(reinterpret_cast<const char*>(img.constScanLine(y)), img.width() * 4));
    return h.result().toHex();
  }

  bool startBackend(LibretroBackend& be, QString* err) {
    // Verzeichnisse vor loadCore: melonDS DS liest sie schon in retro_set_environment.
    be.setSystemDirectory(m_dirs.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dirs.filePath(QStringLiteral("save")));
    if (!be.loadCore(m_corePath, err)) return false;
    for (auto it = m_nds.coreOptions.cbegin(); it != m_nds.coreOptions.cend(); ++it)
      if (!be.setCoreOption(it.key(), it.value())) {
        *err = QStringLiteral("Option abgelehnt: ") + it.key();
        return false;
      }
    return be.loadGame(QStringLiteral(FB_TEST_ROM_PATH), err);
  }

  RunResult runFrames(LibretroBackend& be, int n) {
    RunResult r;
    for (int i = 0; i < n; ++i) {
      if (!be.runFrame()) {
        r.error = QStringLiteral("runFrame fehlgeschlagen bei Frame %1").arg(i);
        return r;
      }
      r.audioBytes += be.takeAudio().size();
    }
    r.frame = be.videoFrame();
    r.hash = hashOf(r.frame);
    return r;
  }

  static QColor px(const QImage& img, int x, int y) { return QColor::fromRgb(img.pixel(x, y)); }

 private slots:
  void initTestCase() {
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("system"))));
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("save"))));
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    m_nds = *reg.find(QStringLiteral("nds"));
    const CoreLocation loc = CoreLocator().locate(m_nds);
    QVERIFY2(loc.found(), "Core nicht gefunden");
    m_corePath = loc.path;
    QCOMPARE(loc.source, QStringLiteral("env"));
  }

  void coreInfoAndOptions() {
    LibretroBackend be;
    QString err;
    be.setSystemDirectory(m_dirs.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dirs.filePath(QStringLiteral("save")));
    QVERIFY2(be.loadCore(m_corePath, &err), qPrintable(err));
    // Zweites Backend im selben Prozess muss sauber scheitern (libretro-Globals).
    LibretroBackend other;
    QVERIFY(!other.loadCore(m_corePath, &err));
    QVERIFY(!err.isEmpty());

    const CoreInfo info = be.coreInfo();
    QVERIFY2(info.name.contains(QStringLiteral("melonDS"), Qt::CaseInsensitive), qPrintable(info.name));
    QVERIFY(!info.version.isEmpty());
    for (const QString& e : m_nds.extensions) QVERIFY(info.extensions.contains(e));

    // melonDS DS registriert seine Core Options erst in retro_load_game.
    QVERIFY(be.coreOptions().isEmpty());
    QVERIFY(be.setCoreOption(QStringLiteral("melonds_render_mode"), QStringLiteral("software")));  // vorgemerkt
    QVERIFY2(be.loadGame(QStringLiteral(FB_TEST_ROM_PATH), &err), qPrintable(err));
    const QList<CoreOption> opts = be.coreOptions();
    QVERIFY(!opts.isEmpty());
    QVERIFY(!be.coreOptionCategories().isEmpty());
    // Manifest-Defaults muessen echte Core-Optionen mit gueltigem Wert sein (keine erfundenen).
    for (auto it = m_nds.coreOptions.cbegin(); it != m_nds.coreOptions.cend(); ++it) {
      bool found = false;
      for (const CoreOption& o : opts) {
        if (o.key != it.key()) continue;
        found = true;
        QVERIFY2(!o.values.isEmpty() && std::any_of(o.values.cbegin(), o.values.cend(), [&](const CoreOptionValue& v) { return v.value == it.value(); }),
                 qPrintable(o.key + QStringLiteral("=") + it.value()));
      }
      QVERIFY2(found, qPrintable(it.key()));
    }
    QVERIFY(!be.setCoreOption(QStringLiteral("melonds_render_mode"), QStringLiteral("gibt-es-nicht")));
    QVERIFY(!be.setCoreOption(QStringLiteral("erfundene_option"), QStringLiteral("x")));
    be.unloadCore();
  }

  void framesPatternAndDeterminism() {
    const QSize expected = m_nds.display.frameSize();
    QByteArray firstHash;
    for (int run = 0; run < 2; ++run) {
      LibretroBackend be;
      QString err;
      QVERIFY2(startBackend(be, &err), qPrintable(err));
      const AvInfo av = be.avInfo();
      QVERIFY(av.fps > 50 && av.fps < 70);
      QVERIFY(av.sampleRate >= 8000);

      const RunResult r = runFrames(be, 120);
      QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
      QCOMPARE(r.frame.format(), QImage::Format_RGB32);
      QCOMPARE(r.frame.size(), expected);
      QVERIFY2(r.audioBytes > 0, "keine Audio-Samples");
      QCOMPARE(r.audioBytes % 4, 0);

      // Oberer Screen: vier Quadranten; unterer Screen: Backdrop-Farbe Magenta.
      auto near = [&](QPoint p, int rr, int gg, int bb) {
        const QColor c = px(r.frame, p.x(), p.y());
        return qAbs(c.red() - rr) < 24 && qAbs(c.green() - gg) < 24 && qAbs(c.blue() - bb) < 24;
      };
      QVERIFY2(near({64, 48}, 255, 0, 0), qPrintable(px(r.frame, 64, 48).name()));
      QVERIFY2(near({192, 48}, 0, 255, 0), qPrintable(px(r.frame, 192, 48).name()));
      QVERIFY2(near({64, 140}, 0, 0, 255), qPrintable(px(r.frame, 64, 140).name()));
      QVERIFY2(near({192, 140}, 255, 255, 255), qPrintable(px(r.frame, 192, 140).name()));
      QVERIFY2(near({128, 192 + 96}, 255, 0, 255), qPrintable(px(r.frame, 128, 288).name()));

      if (run == 0) firstHash = r.hash;
      else QCOMPARE(r.hash, firstHash);
      qInfo() << "frame sha256 (120 Frames):" << r.hash;
      be.unloadCore();  // zweiter Durchlauf laedt den Core erneut
    }
  }

  void inputAccepted() {
    LibretroBackend be;
    QString err;
    QVERIFY2(startBackend(be, &err), qPrintable(err));
    // Touch auf dem unteren Screen (Mitte), Buttons, dann loslassen.
    const QPointF p = m_nds.display.toFrameNormalized(m_nds.display.touchScreenIndex(), {0.5, 0.5});
    be.setPointer(p.x(), p.y(), true);
    be.setJoypadState(0, buttonMask(JoypadButton::A) | buttonMask(JoypadButton::Start));
    RunResult r = runFrames(be, 10);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    be.setPointer(p.x(), p.y(), false);
    be.setJoypadState(0, 0);
    r = runFrames(be, 10);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.frame.size(), m_nds.display.frameSize());
    be.reset();
    r = runFrames(be, 5);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    be.unloadCore();
  }

  void runnerThread() {
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QSignalSpy started(&runner, &EmulationRunner::started);
    QSignalSpy failed(&runner, &EmulationRunner::startFailed);
    QSignalSpy frames(&runner, &EmulationRunner::frameReady);
    QSignalSpy audio(&runner, &EmulationRunner::audioReady);
    QSignalSpy stopped(&runner, &EmulationRunner::stopped);

    EmulationRunner::StartRequest req;
    req.corePath = m_corePath;
    req.gamePath = QStringLiteral(FB_TEST_ROM_PATH);
    req.systemDir = m_dirs.filePath(QStringLiteral("system"));
    req.saveDir = m_dirs.filePath(QStringLiteral("save"));
    req.coreOptions = m_nds.coreOptions;
    runner.start(req);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 20000);
    QCOMPARE(failed.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 20, 10000);
    QVERIFY(audio.count() > 0);
    QCOMPARE(audio.first().at(0).toByteArray().size() % 4, 0);
    QVERIFY(audio.first().at(1).toInt() >= 8000);
    QCOMPARE(frames.first().at(0).value<QImage>().size(), m_nds.display.frameSize());

    runner.pause();
    QTRY_COMPARE(runner.state(), EmulationRunner::State::Paused);
    QTest::qWait(100);
    const int paused = frames.count();
    QTest::qWait(200);
    QVERIFY(frames.count() - paused <= 1);
    runner.setPointer(0.5, 0.75, true);
    runner.resume();
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() > paused + 5, 10000);
    runner.stop();
    QCOMPARE(stopped.count(), 1);
    QCOMPARE(runner.state(), EmulationRunner::State::Idle);

    // Fehlerpfad: Spiel existiert nicht.
    req.gamePath = m_dirs.filePath(QStringLiteral("nicht_da.nds"));
    runner.start(req);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 20000);
    runner.stop();
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("FRAMEBEAM_MELONDS_DS_CORE")) {
    std::puts("FRAMEBEAM_MELONDS_DS_CORE nicht gesetzt - Test uebersprungen");
    return 77;
  }
  QCoreApplication app(argc, argv);
  CoreTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "core_test.moc"
