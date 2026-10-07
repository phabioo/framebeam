// OpenGL hardware rendering of LibretroBackend with a fake HW core (no ROM). Needs a GUI application and a
// working OpenGL 3.3 core context. Without one the test returns 77 (ctest: SKIP) - except on Linux CI
// (environment variable CI set), where a missing context is a failure so the test cannot silently vanish.
#include <QGuiApplication>
#include <QTemporaryFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QtTest>

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
