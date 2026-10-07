// melonDS DS with its OpenGL renderer (hardware frames read back by LibretroBackend) and the homebrew test ROM.
// Without FRAMEBEAM_MELONDS_DS_CORE or without a working OpenGL 3.3 core context: return code 77 (ctest: SKIP),
// except on Linux CI where the context is required (see hw_render_test.cpp).
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QtTest>

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
}  // namespace

class CoreGlTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dirs;

  bool start(LibretroBackend& be, const QString& mode, const QString& resolution, QString* err) {
    be.prepareForStart();
    be.setSystemDirectory(m_dirs.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dirs.filePath(QStringLiteral("save")));
    if (!be.loadCore(qEnvironmentVariable("FRAMEBEAM_MELONDS_DS_CORE"), err)) return false;
    be.setCoreOption(QStringLiteral("melonds_render_mode"), mode);
    be.setCoreOption(QStringLiteral("melonds_opengl_resolution"), resolution);
    be.setCoreOption(QStringLiteral("melonds_screen_layout1"), QStringLiteral("top-bottom"));
    be.setCoreOption(QStringLiteral("melonds_number_of_screen_layouts"), QStringLiteral("1"));
    be.setCoreOption(QStringLiteral("melonds_screen_gap"), QStringLiteral("0"));
    be.setCoreOption(QStringLiteral("melonds_boot_mode"), QStringLiteral("direct"));
    return be.loadGame(QStringLiteral(FB_TEST_ROM_PATH), err);
  }

 private slots:
  void initTestCase() {
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("system"))));
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("save"))));
  }

  void openGlFramesAtTwoTimesResolution() {
    LibretroBackend be;
    QString err;
    QVERIFY2(start(be, QStringLiteral("opengl"), QStringLiteral("2"), &err), qPrintable(err));
    for (int i = 0; i < 90; ++i) QVERIFY(be.runFrame());
    const QImage f = be.videoFrame();
    QVERIFY(!f.isNull());
    QCOMPARE(f.format(), QImage::Format_RGB32);
    qInfo() << "OpenGL frame size at 2x:" << f.size();
    QCOMPARE(f.size(), QSize(512, 768));  // two 256x192 screens stacked, scaled 2x
    // The frame must not be a flat color (the test ROM paints quadrants on both screens).
    QSet<QRgb> colors;
    for (int y = 0; y < f.height(); y += 16)
      for (int x = 0; x < f.width(); x += 16) colors.insert(f.pixel(x, y));
    QVERIFY2(colors.size() >= 3, qPrintable(QString::number(colors.size())));
    be.unloadCore();
  }

  void softwareModeUnchanged() {
    LibretroBackend be;
    QString err;
    QVERIFY2(start(be, QStringLiteral("software"), QStringLiteral("2"), &err), qPrintable(err));
    for (int i = 0; i < 30; ++i) QVERIFY(be.runFrame());
    QCOMPARE(be.videoFrame().size(), QSize(256, 384));
    be.unloadCore();
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("FRAMEBEAM_MELONDS_DS_CORE")) {
    std::puts("FRAMEBEAM_MELONDS_DS_CORE not set - test skipped");
    return 77;
  }
  QGuiApplication app(argc, argv);
  if (!canCreateGl33Core()) {
#ifdef Q_OS_LINUX
    const bool required = qEnvironmentVariableIsSet("CI");
#else
    const bool required = false;
#endif
    qWarning("No OpenGL 3.3 core context available (platform %s)", qPrintable(QGuiApplication::platformName()));
    return required ? 1 : 77;
  }
  CoreGlTest t;
  return QTest::qExec(&t, argc, argv);
}

#include "core_gl_test.moc"
