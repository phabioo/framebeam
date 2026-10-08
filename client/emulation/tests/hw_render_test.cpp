// OpenGL hardware rendering of LibretroBackend with a fake HW core (no ROM). Needs a GUI application and a
// working OpenGL 3.3 core context. Without one the test returns 77 (ctest: SKIP) - except on Linux CI
// (environment variable CI set), where a missing context is a failure so the test cannot silently vanish.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QtTest>

#include <atomic>

#include "emulation_runner.h"
#include "hw_render.h"
#include "libretro_backend.h"

using namespace framebeam::emu;

namespace {
bool canCreateGl33Core() {
  QOffscreenSurface surface;
  surface.create();
  if (!surface.isValid()) return false;
  QSurfaceFormat fmt;
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  QOpenGLContext ctx;
  ctx.setFormat(fmt);
  return ctx.create() && ctx.makeCurrent(&surface);
}

QRgb px(const QImage& img, int x, int y) { return img.pixel(x, y); }
}  // namespace

class HwRenderTest : public QObject {
  Q_OBJECT
  QTemporaryFile m_log;  // events written by the fake core (FB_FAKE_HW_LOG)
  QStringList events() {
    QFile f(m_log.fileName());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  }
  int count(const QString& prefix) {
    int n = 0;
    for (const QString& e : events()) n += e.startsWith(prefix) ? 1 : 0;
    return n;
  }
  void clearLog() { QVERIFY(QFile::resize(m_log.fileName(), 0)); }

 private slots:
  void init() { clearLog(); }
  void initTestCase() {
    QVERIFY(m_log.open());
    qputenv("FB_FAKE_HW_LOG", m_log.fileName().toUtf8());
    qunsetenv("FRAMEBEAM_DISABLE_HW_RENDER");
    qunsetenv("FB_FAKE_HW_BOTTOM_LEFT");
    qunsetenv("FB_FAKE_HW_CTX");
  }

  void hwFrameMatchesOrientation() {
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    LibretroBackend be;
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(rom.fileName(), &err), qPrintable(err));
    QCOMPARE(count(QStringLiteral("accepted")), 1);
    QCOMPARE(count(QStringLiteral("reset")), 1);
    QCOMPARE(count(QStringLiteral("destroy")), 0);
    {  // diagnostics: hardware path active, GPU strings from the live context, no fallback reason
      const RenderInfo ri = be.renderInfo();
      QVERIFY(ri.hwRequested && ri.hwActive);
      QVERIFY(ri.fallbackReason.isEmpty());
      QVERIFY2(ri.api.startsWith(QStringLiteral("OpenGL 3.")) || ri.api.startsWith(QStringLiteral("OpenGL 4.")), qPrintable(ri.api));
      QVERIFY(!ri.gpu.isEmpty());
    }
    {
      const QStringList ver = events().filter(QStringLiteral("reset ")).value(0).mid(6).split(QLatin1Char('.'));
      QVERIFY(ver.size() == 2 && ver.at(0).toInt() * 10 + ver.at(1).toInt() >= 33);  // core asked for 3.3
    }
    QVERIFY(be.runFrame());
    QCOMPARE(count(QStringLiteral("frame without fbo")), 0);
    const QImage f = be.videoFrame();
    QCOMPARE(f.size(), QSize(64, 48));
    QCOMPARE(f.format(), QImage::Format_RGB32);
    QCOMPARE(be.frameCount(), quint64(1));
    QCOMPARE(px(f, 2, 45), qRgb(255, 0, 0));    // GL bottom-left red -> bottom-left of the image
    QCOMPARE(px(f, 61, 2), qRgb(0, 255, 0));    // GL top-right green -> top-right of the image
    QCOMPARE(px(f, 2, 2), qRgb(0, 0, 255));     // top-left stays blue
    QCOMPARE(px(f, 61, 45), qRgb(0, 0, 255));   // bottom-right stays blue
    QCOMPARE(px(f, 32, 24), qRgb(0, 0, 255));
    QCOMPARE(qAlpha(px(f, 0, 0)), 255);
    QVERIFY(be.lastReadbackMs() >= 0.0 && be.lastReadbackMs() < 1000.0);  // timed around the GPU readback
    be.unloadGame();
    QCOMPARE(count(QStringLiteral("destroy")), 1);  // context_destroy before retro_unload_game
    be.unloadCore();
  }

  void topLeftOriginIsNotFlipped() {
    qputenv("FB_FAKE_HW_BOTTOM_LEFT", "0");
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    LibretroBackend be;
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(rom.fileName(), &err), qPrintable(err));
    QVERIFY(be.runFrame());
    const QImage f = be.videoFrame();
    qunsetenv("FB_FAKE_HW_BOTTOM_LEFT");
    QCOMPARE(px(f, 2, 2), qRgb(255, 0, 0));
    QCOMPARE(px(f, 61, 45), qRgb(0, 255, 0));
  }

  void disabledByEnvironment() {
    qputenv("FRAMEBEAM_DISABLE_HW_RENDER", "1");
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    LibretroBackend be;
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY(!be.loadGame(rom.fileName(), &err));  // the fake core needs HW rendering and gives up
    QCOMPARE(count(QStringLiteral("rejected")), 1);
    QCOMPARE(count(QStringLiteral("reset")), 0);
    {  // diagnostics: requested but software, with the reason (0.6 D10)
      const RenderInfo ri = be.renderInfo();
      QVERIFY(ri.hwRequested && !ri.hwActive);
      QCOMPARE(ri.fallbackReason, QString::fromLatin1(kFallbackDisabled));
      QVERIFY(ri.api.isEmpty() && ri.gpu.isEmpty());
    }
    qunsetenv("FRAMEBEAM_DISABLE_HW_RENDER");
  }

  void glDescriptionHelpers() {
    using framebeam::emu::HwRenderContext;
    QCOMPARE(HwRenderContext::describeApi(QStringLiteral("4.6.0 NVIDIA 555.42"), true), QStringLiteral("OpenGL 4.6 Core"));
    QCOMPARE(HwRenderContext::describeApi(QStringLiteral("3.3 (Compatibility Profile) Mesa 23.2.1"), false), QStringLiteral("OpenGL 3.3"));
    QCOMPARE(HwRenderContext::describeApi(QString(), true), QStringLiteral("OpenGL Core"));
    QCOMPARE(HwRenderContext::describeGpu(QStringLiteral("Example GPU"), QStringLiteral("4.6.0 NVIDIA 555.42")),
             QStringLiteral("Example GPU · Driver NVIDIA 555.42"));
    QCOMPARE(HwRenderContext::describeGpu(QStringLiteral("llvmpipe (LLVM 15.0.7, 256 bits)"), QStringLiteral("4.5 (Core Profile) Mesa 23.2.1")),
             QStringLiteral("llvmpipe (LLVM 15.0.7, 256 bits) · Driver Mesa 23.2.1"));
    QCOMPARE(HwRenderContext::describeGpu(QString(), QStringLiteral("4.1")), QStringLiteral("GPU"));
  }

  void otherApisAreRejected() {
    qputenv("FB_FAKE_HW_CTX", "6");  // Vulkan
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    LibretroBackend be;
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY(!be.loadGame(rom.fileName(), &err));
    QCOMPARE(count(QStringLiteral("rejected")), 1);
    QCOMPARE(be.renderInfo().fallbackReason, QString::fromLatin1(kFallbackUnsupported));
    QVERIFY(be.renderInfo().hwRequested && !be.renderInfo().hwActive);
    qunsetenv("FB_FAKE_HW_CTX");
  }

  void unavailableVersionIsRejected() {
    qputenv("FB_FAKE_HW_MAJOR", "9");
    qputenv("FB_FAKE_HW_MINOR", "9");
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    LibretroBackend be;
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY(!be.loadGame(rom.fileName(), &err));
    QCOMPARE(count(QStringLiteral("rejected")), 1);
    qunsetenv("FB_FAKE_HW_MAJOR");
    qunsetenv("FB_FAKE_HW_MINOR");
  }

  // Loads the fake HW core into `be`; the ROM file is a dummy.
  void loadFake(LibretroBackend& be, QTemporaryFile& rom) {
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(rom.fileName(), &err), qPrintable(err));
  }

  void videoNotWantedSkipsReadback() {
    qputenv("FB_FAKE_HW_LOGAV", "1");
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    QCOMPARE(be.hwReadbackCount(), quint64(0));
    be.setVideoWanted(true);
    QVERIFY(be.runFrame());
    QCOMPARE(be.hwReadbackCount(), quint64(1));
    const QImage shown = be.videoFrame();
    QVERIFY(!shown.isNull());
    for (int i = 0; i < 3; ++i) {
      be.setVideoWanted(false);
      QVERIFY(be.runFrame());
    }
    QCOMPARE(be.hwReadbackCount(), quint64(1));  // no readback for frames nobody sees
    QCOMPARE(be.frameCount(), quint64(4));       // but they are still emulated
    QCOMPARE(be.videoFrame(), shown);            // the last frame stays
    be.setVideoWanted(true);
    QVERIFY(be.runFrame());
    QCOMPARE(be.hwReadbackCount(), quint64(2));
    // GET_AUDIO_VIDEO_ENABLE reached the core with the right bits: 3 = video + audio, 2 = audio only.
    const QStringList av = events().filter(QStringLiteral("av "));
    QCOMPARE(av, (QStringList{QStringLiteral("av 3"), QStringLiteral("av 2"), QStringLiteral("av 2"), QStringLiteral("av 2"),
                              QStringLiteral("av 3")}));
    qunsetenv("FB_FAKE_HW_LOGAV");
  }

  // Red value of the fake core's frame counter (GL (28..36, 20..28) -> image row 23).
  static int counterOf(const QImage& img) { return qRed(px(img, 32, 23)); }

  void pboReadbackHasOneFrameLatency() {
    qputenv("FB_FAKE_HW_COUNTER", "1");
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    QVERIFY(be.runFrame());
    QCOMPARE(counterOf(be.videoFrame()), 1);           // the first frame is valid at once
    QCOMPARE(px(be.videoFrame(), 2, 45), qRgb(255, 0, 0));
    for (int k = 2; k <= 8; ++k) {
      QVERIFY(be.runFrame());
      const QImage f = be.videoFrame();
      QCOMPARE(f.size(), QSize(64, 48));
      QCOMPARE(counterOf(f), k - 1);                    // asynchronous: the previous frame
      QCOMPARE(px(f, 2, 45), qRgb(255, 0, 0));          // orientation and pixels intact
      QCOMPARE(px(f, 61, 2), qRgb(0, 255, 0));
    }
    qunsetenv("FB_FAKE_HW_COUNTER");
  }

  void syncFallbackIsCurrentFrame() {
    qputenv("FB_FAKE_HW_COUNTER", "1");
    qputenv("FRAMEBEAM_SYNC_READBACK", "1");
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    for (int k = 1; k <= 4; ++k) {
      QVERIFY(be.runFrame());
      QCOMPARE(counterOf(be.videoFrame()), k);
      QCOMPARE(px(be.videoFrame(), 2, 45), qRgb(255, 0, 0));
    }
    qunsetenv("FRAMEBEAM_SYNC_READBACK");
    qunsetenv("FB_FAKE_HW_COUNTER");
  }

  void readbackTimingSyncVsPbo() {  // informational measurement (llvmpipe): ms per readback on the emulation thread
    QTemporaryFile rom;
    double ms[2] = {0, 0};
    for (int mode = 0; mode < 2; ++mode) {
      if (mode == 1) qputenv("FRAMEBEAM_SYNC_READBACK", "1");
      LibretroBackend be;
      QTemporaryFile r;
      loadFake(be, r);
      QVERIFY(be.runFrame());
      double sum = 0;
      for (int i = 0; i < 100; ++i) {
        QVERIFY(be.runFrame());
        sum += be.lastReadbackMs();
      }
      ms[mode] = sum / 100;
      qunsetenv("FRAMEBEAM_SYNC_READBACK");
    }
    qInfo("readback avg: PBO %.3f ms, sync %.3f ms (64x48)", ms[0], ms[1]);
  }

  void runnerRequestsVideoOnlyAtBaseRateWhileFast() {
    qputenv("FB_FAKE_HW_LOGAV", "1");
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    auto owned = std::make_unique<LibretroBackend>();
    LibretroBackend* be = owned.get();
    EmulationRunner runner(std::move(owned));
    std::atomic<int> ui{0};
    QObject::connect(&runner, &EmulationRunner::frameReady, &runner, [&ui] { ++ui; }, Qt::DirectConnection);
    EmulationRunner::StartRequest req;
    req.corePath = QStringLiteral(FB_FAKE_HW_CORE_PATH);
    req.gamePath = rom.fileName();
    req.speedUpRatio = 4.0;
    QSignalSpy started(&runner, &EmulationRunner::started);
    runner.start(req);
    QTRY_VERIFY2(started.count() == 1, "not started");
    QTest::qWait(300);
    // Normal speed: every frame is wanted.
    const quint64 rb0 = be->hwReadbackCount();
    const quint64 fr0 = be->frameCount();
    QTest::qWait(500);
    const quint64 rbNormal = be->hwReadbackCount() - rb0;
    const quint64 frNormal = be->frameCount() - fr0;
    QVERIFY2(frNormal > 15, qPrintable(QString::number(frNormal)));
    QVERIFY2(rbNormal >= frNormal - 2, qPrintable(QStringLiteral("normal: %1 readbacks for %2 frames").arg(rbNormal).arg(frNormal)));

    runner.setFastForward(true);
    QTest::qWait(300);
    const quint64 rb1 = be->hwReadbackCount();
    const quint64 fr1 = be->frameCount();
    const int ui1 = ui.load();
    QElapsedTimer t;
    t.start();
    QTest::qWait(1500);
    const double secs = t.elapsed() / 1000.0;
    const quint64 rbFast = be->hwReadbackCount() - rb1;
    const quint64 frFast = be->frameCount() - fr1;
    const int uiFast = ui.load() - ui1;
    qInfo("4x: %.1f emu frames/s, %.1f readbacks/s, %.1f UI frames/s", frFast / secs, rbFast / secs, uiFast / secs);
    QVERIFY2(frFast / secs > 60 * 2.0, qPrintable(QStringLiteral("emu %1/s").arg(frFast / secs)));  // actually faster
    QVERIFY2(rbFast / secs < 60 * 1.3, qPrintable(QStringLiteral("readbacks %1/s").arg(rbFast / secs)));  // ~base fps
    QVERIFY2(rbFast / secs > 60 * 0.5, qPrintable(QStringLiteral("readbacks %1/s").arg(rbFast / secs)));
    QVERIFY2(rbFast <= static_cast<quint64>(uiFast) + 3, "readback without a shown frame");
    runner.stop();
    // The core saw video on/off: both answers occurred while fast.
    QVERIFY(count(QStringLiteral("av 2")) > 0);
    QVERIFY(count(QStringLiteral("av 3")) > 0);
    qunsetenv("FB_FAKE_HW_LOGAV");
  }

  // ---- readback size limit (GPU downscale before readback)

  // Runs `n` frames and returns the last video frame (the PBO path lags one frame behind a limit change).
  static QImage runFrames(LibretroBackend& be, int n) {
    for (int i = 0; i < n; ++i)
      if (!be.runFrame()) return {};
    return be.videoFrame();
  }

  void scaledReadbackSizeRules() {
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, {}), QSize(2048, 3072));
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, QSize(0, 0)), QSize(2048, 3072));
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, QSize(4096, 4096)), QSize(2048, 3072));  // never upscale
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, QSize(2048, 3072)), QSize(2048, 3072));
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, QSize(1312, 1968)), QSize(1312, 1968));
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, QSize(1312, 5000)), QSize(1312, 1968));  // width limits
    QCOMPARE(HwRenderContext::scaledReadbackSize(2048, 3072, QSize(5000, 1000)), QSize(667, 1000));   // height limits
    QCOMPARE(HwRenderContext::scaledReadbackSize(256, 192, QSize(1, 1)), QSize(1, 1));
  }

  void checkPattern(const QImage& f, int w, int h) {  // red bottom-left, green top-right, blue elsewhere
    QCOMPARE(f.size(), QSize(w, h));
    QCOMPARE(f.format(), QImage::Format_RGB32);
    const int m = std::max(2, w / 32);  // well inside the squares (w/8 wide)
    QCOMPARE(px(f, m, h - 1 - m), qRgb(255, 0, 0));
    QCOMPARE(px(f, w - 1 - m, m), qRgb(0, 255, 0));
    QCOMPARE(px(f, m, m), qRgb(0, 0, 255));
    QCOMPARE(px(f, w - 1 - m, h - 1 - m), qRgb(0, 0, 255));
    QCOMPARE(px(f, w / 2, h / 2), qRgb(0, 0, 255));
    QCOMPARE(qAlpha(px(f, 0, 0)), 255);
  }

  void limitedReadbackKeepsAspectOrientationAndColors_data() {
    QTest::addColumn<bool>("bottomLeft");
    QTest::addColumn<bool>("syncReadback");
    QTest::newRow("bottom-left, PBO") << true << false;
    QTest::newRow("top-left, PBO") << false << false;
    QTest::newRow("bottom-left, sync") << true << true;
  }

  void limitedReadbackKeepsAspectOrientationAndColors() {
    QFETCH(bool, bottomLeft);
    QFETCH(bool, syncReadback);
    qputenv("FB_FAKE_HW_SCALE", "4");  // 256x192 frame, the core leaves GL_SCISSOR_TEST enabled
    qputenv("FB_FAKE_HW_BOTTOM_LEFT", bottomLeft ? "1" : "0");
    if (syncReadback) qputenv("FRAMEBEAM_SYNC_READBACK", "1");
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    QVERIFY(be.sourceFrameSize().isEmpty());
    be.setReadbackLimit(QSize(128, 128));
    // 256x192 -> width limits: 128x96 (aspect 4:3 kept)
    const QImage f = runFrames(be, 4);
    if (bottomLeft) checkPattern(f, 128, 96);
    else {  // top-left origin: the pattern is not flipped, so red is top-left and green bottom-right
      QCOMPARE(f.size(), QSize(128, 96));
      QCOMPARE(px(f, 4, 4), qRgb(255, 0, 0));
      QCOMPARE(px(f, 123, 91), qRgb(0, 255, 0));
      QCOMPARE(px(f, 4, 91), qRgb(0, 0, 255));
    }
    QCOMPARE(be.sourceFrameSize(), QSize(256, 192));  // diagnostics still know the real frame size
    qunsetenv("FB_FAKE_HW_SCALE");
    qunsetenv("FB_FAKE_HW_BOTTOM_LEFT");
    qunsetenv("FRAMEBEAM_SYNC_READBACK");
  }

  void limitClearedAndNeverUpscaled() {
    qputenv("FB_FAKE_HW_SCALE", "4");
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    QCOMPARE(runFrames(be, 3).size(), QSize(256, 192));  // no limit: unchanged
    be.setReadbackLimit(QSize(1000, 1000));
    QCOMPARE(runFrames(be, 3).size(), QSize(256, 192));  // limit above the frame: never upscaled
    be.setReadbackLimit(QSize(256, 192));
    checkPattern(runFrames(be, 3), 256, 192);
    qunsetenv("FB_FAKE_HW_SCALE");
  }

  void limitChangesMidRun() {
    qputenv("FB_FAKE_HW_SCALE", "4");
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    checkPattern(runFrames(be, 3), 256, 192);
    be.setReadbackLimit(QSize(128, 128));
    checkPattern(runFrames(be, 3), 128, 96);
    be.setReadbackLimit(QSize(64, 64));
    checkPattern(runFrames(be, 3), 64, 48);
    be.setReadbackLimit(QSize(200, 100));  // height limits: 133x100
    const QImage f = runFrames(be, 3);
    QCOMPARE(f.size(), QSize(133, 100));
    QCOMPARE(px(f, 3, 96), qRgb(255, 0, 0));
    QCOMPARE(px(f, 129, 3), qRgb(0, 255, 0));
    be.setReadbackLimit(QSize());  // cleared: full size again
    checkPattern(runFrames(be, 3), 256, 192);
    qunsetenv("FB_FAKE_HW_SCALE");
  }

  void skippedFramesReportNoReadbackTime() {
    QTemporaryFile rom;
    LibretroBackend be;
    loadFake(be, rom);
    be.setVideoWanted(true);
    QVERIFY(be.runFrame());
    QVERIFY(be.lastReadbackMs() > 0.0);
    be.setVideoWanted(false);
    QVERIFY(be.runFrame());
    QCOMPARE(be.lastReadbackMs(), 0.0);  // not the stale value of the previous frame
  }

  void readbackTimingFullVsLimited() {  // informational (llvmpipe): ms per readback of a large frame
    qputenv("FB_FAKE_HW_SCALE", "32");  // 2048x1536
    const QSize limits[] = {QSize(), QSize(1312, 984), QSize(656, 492)};
    for (const QSize& lim : limits) {
      QTemporaryFile rom;
      LibretroBackend be;
      loadFake(be, rom);
      be.setReadbackLimit(lim);
      runFrames(be, 3);
      double sum = 0;
      for (int i = 0; i < 20; ++i) {
        QVERIFY(be.runFrame());
        sum += be.lastReadbackMs();
      }
      qInfo("readback 2048x1536 -> %dx%d: %.2f ms avg", be.videoFrame().width(), be.videoFrame().height(), sum / 20);
    }
    qunsetenv("FB_FAKE_HW_SCALE");
  }

  void runnerAppliesReadbackLimit() {
    qputenv("FB_FAKE_HW_SCALE", "4");
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QSignalSpy frames(&runner, &EmulationRunner::frameReady);
    QSignalSpy failed(&runner, &EmulationRunner::startFailed);
    runner.setReadbackLimit(QSize(128, 128));  // before start: applies from the first frame
    EmulationRunner::StartRequest req;
    req.corePath = QStringLiteral(FB_FAKE_HW_CORE_PATH);
    req.gamePath = rom.fileName();
    runner.start(req);
    QTRY_VERIFY2(frames.count() >= 3 || failed.count() > 0, "no frames");
    QCOMPARE(failed.count(), 0);
    QCOMPARE(frames.last().at(0).value<QImage>().size(), QSize(128, 96));
    QCOMPARE(runner.sourceFrameSize(), QSize(256, 192));
    runner.setReadbackLimit(QSize());  // thread-safe change while running
    const int before = frames.count();
    QTRY_VERIFY(frames.count() >= before + 4);
    QCOMPARE(frames.last().at(0).value<QImage>().size(), QSize(256, 192));
    runner.stop();
    qunsetenv("FB_FAKE_HW_SCALE");
  }

  void runnerUsesEmulationThread() {
    QTemporaryFile rom;
    QVERIFY(rom.open());
    rom.write("x");
    rom.flush();
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QSignalSpy frames(&runner, &EmulationRunner::frameReady);
    QSignalSpy failed(&runner, &EmulationRunner::startFailed);
    EmulationRunner::StartRequest req;
    req.corePath = QStringLiteral(FB_FAKE_HW_CORE_PATH);
    req.gamePath = rom.fileName();
    runner.start(req);
    QTRY_VERIFY2(frames.count() >= 3 || failed.count() > 0, "no frames");
    QCOMPARE(failed.count(), 0);
    const QImage f = frames.last().at(0).value<QImage>();
    QCOMPARE(f.size(), QSize(64, 48));
    QCOMPARE(px(f, 2, 45), qRgb(255, 0, 0));
    runner.stop();
    QCOMPARE(count(QStringLiteral("reset")), 1);
    QCOMPARE(count(QStringLiteral("destroy")), 1);
  }
};

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (!canCreateGl33Core()) {
#ifdef Q_OS_LINUX
    const bool required = qEnvironmentVariableIsSet("CI");  // GitHub Actions sets CI=true
#else
    const bool required = false;
#endif

    qWarning("No OpenGL 3.3 core context available (platform %s)", qPrintable(QGuiApplication::platformName()));
    return required ? 1 : 77;
  }
  HwRenderTest t;
  return QTest::qExec(&t, argc, argv);
}

#include "hw_render_test.moc"
