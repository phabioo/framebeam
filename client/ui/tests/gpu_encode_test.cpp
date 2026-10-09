// GPU-direct encoding of the own Session (ADR 0019), UI side: GpuEncodeBridge between the emulation thread and the UI
// thread, and GameSession handing the target to every runner it creates. The pipeline is real except for CUDA: the fake
// hardware core renders on a real GL 3.3 context (Mesa llvmpipe under xvfb), the real CudaGlCapture runs against the fake
// CUDA API of testutil/fake_cuda.h and copies the texture into host-memory frames. Needs a GL 3.3 core context; without
// one the test returns 77 (ctest: SKIP) - except on Linux CI (environment variable CI set), where a missing context is a
// failure so the test cannot silently vanish.
#include "fake_cuda.h"  // first: it includes the CUDA shim before any FFmpeg CUDA header

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThreadPool>
#include <QtTest>
#include <atomic>
#include <memory>

#include "emulation_runner.h"
#include "gamesession.h"
#include "gpuencodebridge.h"
#include "libretro_backend.h"
#include "processguard.h"

using namespace framebeam;
using namespace framebeam::ui;
using emu::EmulationRunner;
using emu::LibretroBackend;
using fbtest::FakeCuda;

namespace {

struct GlEnv {
  QOffscreenSurface surface;
  QOpenGLContext context;

  bool create() {
    surface.create();
    if (!surface.isValid()) return false;
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    context.setFormat(fmt);
    return context.create() && context.makeCurrent(&surface);
  }
};

bool canCreateGl33Core() {
  GlEnv env;
  return env.create();
}

constexpr unsigned kRgba8 = 0x8058;

uint8_t byteAt(const AVFrame* f, int x, int y, int channel) {
  return f->data[0][static_cast<size_t>(y) * static_cast<size_t>(f->linesize[0]) + static_cast<size_t>(x) * 4 + static_cast<size_t>(channel)];
}

// The fake core's pattern as the encoder gets it: rows top-down, bytes R,G,B; red bottom-left, green top-right, blue
// elsewhere (the core draws with a bottom-left origin, so the flip is visible). Empty = fine.
QString checkPattern(const AVFrame* f, int w, int h) {
  if (!f) return QStringLiteral("no frame");
  if (f->width != w || f->height != h) return QStringLiteral("frame is %1x%2, not %3x%4").arg(f->width).arg(f->height).arg(w).arg(h);
  if (f->format != AV_PIX_FMT_CUDA) return QStringLiteral("format %1, not CUDA").arg(f->format);
  const int m = (std::max)(2, w / 32);  // well inside the squares (w/8 wide); parentheses: windows.h max macro
  struct Probe {
    int x, y;
    int r, g, b;
  };
  const Probe probes[] = {{m, h - 1 - m, 255, 0, 0}, {w - 1 - m, m, 0, 255, 0}, {m, m, 0, 0, 255}, {w - 1 - m, h - 1 - m, 0, 0, 255}, {w / 2, h / 2, 0, 0, 255}};
  for (const Probe& p : probes) {
    if (byteAt(f, p.x, p.y, 0) != p.r || byteAt(f, p.x, p.y, 1) != p.g || byteAt(f, p.x, p.y, 2) != p.b) {
      return QStringLiteral("pixel (%1,%2) is %3,%4,%5, expected %6,%7,%8")
          .arg(p.x).arg(p.y).arg(byteAt(f, p.x, p.y, 0)).arg(byteAt(f, p.x, p.y, 1)).arg(byteAt(f, p.x, p.y, 2)).arg(p.r).arg(p.g).arg(p.b);
    }
  }
  return {};
}

// Red value of the fake core's frame counter (GL (28..36, 20..28) of a 64x48 frame -> image pixel (32, 23)).
int gpuCounter(const AVFrame* f) { return byteAt(f, 32, 23, 0); }
int displayCounter(const QImage& img) { return qRed(img.pixel(32, 23)); }

void waitForPool() { QThreadPool::globalInstance()->waitForDone(); }

// For QTRY_*: true once a frame was taken from the bridge. Sticky, because QTRY evaluates its expression again after the
// wait loop and takeFrame() consumes the frame.
bool frameArrived(GpuEncodeBridge& bridge, bool* arrived) {
  if (bridge.takeFrame()) *arrived = true;
  return *arrived;
}

// Runs frames until the bridge delivers one (the CUDA context is created on a pool thread, so the first frames find
// attach() not ready), up to `maxFrames`.
std::shared_ptr<AVFrame> runUntilFrame(LibretroBackend& be, GpuEncodeBridge& bridge, int maxFrames = 30) {
  for (int i = 0; i < maxFrames; ++i) {
    if (!be.runFrame()) return {};
    waitForPool();
    if (std::shared_ptr<AVFrame> f = bridge.takeFrame()) return f;
  }
  return {};
}

void clearKnobs() {
  for (const char* k : {"FB_FAKE_HW_SCALE", "FB_FAKE_HW_COUNTER", "FB_FAKE_HW_BOTTOM_LEFT", "FB_FAKE_HW_STRIPES", "FB_FAKE_HW_DUPE_EVERY",
                        "FB_FAKE_HW_READFBO", "FB_FAKE_HW_GLERROR", "FB_FAKE_HW_CTX", "FB_FAKE_HW_LOG", "FRAMEBEAM_DISABLE_HW_RENDER"}) {
    qunsetenv(k);
  }
}

}  // namespace

class GpuEncodeTest : public QObject {
  Q_OBJECT
  QTemporaryFile rom_;  // dummy game file
  QTemporaryDir dir_;

  void loadFake(LibretroBackend& be) {
    be.prepareForStart();
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_HW_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(rom_.fileName(), &err), qPrintable(err));
  }
  GameSession::LaunchConfig config() const {
    GameSession::LaunchConfig c;
    c.title = QStringLiteral("Fake HW");
    c.corePath = QStringLiteral(FB_FAKE_HW_CORE_PATH);
    c.gamePath = rom_.fileName();
    c.systemDir = dir_.filePath(QStringLiteral("sys"));
    c.saveDir = dir_.filePath(QStringLiteral("save"));
    return c;
  }
  void checkBalanced(const FakeCuda& fake) {
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
    QVERIFY2(fake.violations().isEmpty(), qPrintable(fake.violations().join(QLatin1Char(';'))));
    QCOMPARE(FakeCuda::liveFrames(), 0);
  }

 private slots:
  void initTestCase() {
    QVERIFY(rom_.open());
    rom_.write("x");
    rom_.flush();
    QVERIFY(dir_.isValid());
  }
  void init() { clearKnobs(); }
  void cleanup() { clearKnobs(); }

  // The fake core's 256x192 frame (the core leaves GL_SCISSOR_TEST on) arrives fitted into 100x100: 100x74, rows top-down,
  // bytes R,G,B, at the pitch of the pool frame.
  void framesArriveTopDownAtTheEncodeSize() {
    qputenv("FB_FAKE_HW_SCALE", "4");
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(QSize(100, 100), fake.deps());
      QCOMPARE(bridge->maxSize(), QSize(100, 100));
      QVERIFY(!bridge->wanted());
      bridge->setWanted(true);
      QVERIFY(bridge->wanted());
      LibretroBackend be;
      loadFake(be);
      be.setGpuEncodeTarget(bridge);
      QTest::ignoreMessage(QtInfoMsg, "Session encode texture 100x74 for a 256x192 frame");
      const std::shared_ptr<AVFrame> f = runUntilFrame(be, *bridge);
      QVERIFY2(f, "no frame reached the mailbox");
      const QString err = checkPattern(f.get(), 100, 74);
      QVERIFY2(err.isEmpty(), qPrintable(err));
      QVERIFY(f->linesize[0] >= 100 * 4);
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Running);
      QVERIFY(bridge->reason().isEmpty());
      // Later frames keep coming and keep the orientation.
      for (int i = 0; i < 3; ++i) {
        const std::shared_ptr<AVFrame> next = runUntilFrame(be, *bridge, 3);
        QVERIFY(next);
        QVERIFY2(checkPattern(next.get(), 100, 74).isEmpty(), qPrintable(checkPattern(next.get(), 100, 74)));
      }
      be.unloadGame();  // detaches with GL current
      QCOMPARE(fake.counters().registrations, 1);
      QCOMPARE(fake.counters().unregistrations, 1);
      be.unloadCore();
    }
    checkBalanced(fake);
    QCOMPARE(fake.contextCreateThreads().size(), size_t(1));
    QVERIFY(fake.contextCreateThreads().front() != std::this_thread::get_id());  // the context was not made on the emulation thread
  }

  // The GPU frame taken after a runFrame has the content of the image that runFrame displays (one frame behind both).
  void counterMatchesTheDisplayedFrame() {
    qputenv("FB_FAKE_HW_COUNTER", "1");
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(kShareEncodeMax, fake.deps());
      bridge->setWanted(true);
      LibretroBackend be;
      loadFake(be);
      be.setGpuEncodeTarget(bridge);
      int compared = 0;
      int last = -1;
      for (int k = 0; k < 60 && compared < 12; ++k) {
        QVERIFY(be.runFrame());
        waitForPool();
        const std::shared_ptr<AVFrame> f = bridge->takeFrame();
        if (!f) continue;
        const int gpu = gpuCounter(f.get());
        const int shown = displayCounter(be.videoFrame());
        QVERIFY2(gpu == shown, qPrintable(QStringLiteral("frame %1: GPU counter %2, displayed counter %3").arg(k).arg(gpu).arg(shown)));
        QVERIFY2(gpu != last, "the same frame twice");
        last = gpu;
        ++compared;
      }
      QVERIFY2(compared >= 12, qPrintable(QString::number(compared)));
      be.unloadGame();
      be.unloadCore();
    }
    checkBalanced(fake);
  }

  // The same pairing on the real runner thread while sped up 4x: the mailbox is read in frameReady (emulation thread), the
  // way the UI would, and every pair must match; GPU frames follow the displayed (base-rate) frames.
  void speedUpKeepsThePairing() {
    qputenv("FB_FAKE_HW_COUNTER", "1");
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(kShareEncodeMax, fake.deps());
      bridge->setWanted(true);
      EmulationRunner runner(std::make_unique<LibretroBackend>());
      std::atomic<int> compared{0}, mismatches{0};
      QObject::connect(&runner, &EmulationRunner::frameReady, &runner, [&](const QImage& img, quint64) {
        const std::shared_ptr<AVFrame> f = bridge->takeFrame();
        if (!f) return;
        ++compared;
        if (gpuCounter(f.get()) != displayCounter(img)) ++mismatches;
      }, Qt::DirectConnection);
      runner.setGpuEncodeTarget(bridge);
      EmulationRunner::StartRequest req;
      req.corePath = QStringLiteral(FB_FAKE_HW_CORE_PATH);
      req.gamePath = rom_.fileName();
      req.speedUpRatio = 4.0;
      QSignalSpy started(&runner, &EmulationRunner::started);
      runner.start(req);
      QTRY_VERIFY2_WITH_TIMEOUT(started.count() == 1, "not started", 10000);
      QTRY_VERIFY_WITH_TIMEOUT(compared.load() >= 20, 10000);
      const int normal = compared.load();
      QCOMPARE(mismatches.load(), 0);
      runner.setFastForward(true);
      QTest::qWait(1500);
      qInfo("pairs: %d at 1x, %d in total", normal, compared.load());
      QVERIFY2(compared.load() >= normal + 20, qPrintable(QStringLiteral("%1 -> %2").arg(normal).arg(compared.load())));
      QCOMPARE(mismatches.load(), 0);
      runner.stop();
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Running);
    }
    checkBalanced(fake);
  }

  // setWanted(false) empties the mailbox, and a bridge that is not wanted gets no blit and no capture. Wanted again: frames
  // come back without a new attach.
  void unwantedEmptiesTheMailboxAndStopsCapturing() {
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(kShareEncodeMax, fake.deps());
      bridge->setWanted(true);
      LibretroBackend be;
      loadFake(be);
      be.setGpuEncodeTarget(bridge);
      QVERIFY(runUntilFrame(be, *bridge));
      QVERIFY(be.runFrame());  // one more captured frame, left in the mailbox
      QCOMPARE(FakeCuda::liveFrames(), 1);
      bridge->setWanted(false);
      QCOMPARE(FakeCuda::liveFrames(), 0);  // released by setWanted (outside the lock)
      QVERIFY(!bridge->takeFrame());
      QVERIFY(!bridge->wanted());
      const int captures = fake.counters().copies;
      for (int i = 0; i < 4; ++i) QVERIFY(be.runFrame());
      QVERIFY(!bridge->takeFrame());
      QCOMPARE(fake.counters().copies, captures);  // no capture while unwanted
      QCOMPARE(fake.counters().registrations, 1);  // ... and the registration stays
      bridge->setWanted(true);
      QVERIFY(runUntilFrame(be, *bridge, 5));
      QCOMPARE(fake.counters().registrations, 1);
      be.unloadGame();
      be.unloadCore();
    }
    checkBalanced(fake);
  }

  // The mailbox in isolation: only the newest frame is kept, a frame captured after setWanted(false) goes back to the pool
  // at once, and the first failure wins.
  void mailboxKeepsTheNewestAndStoresOnlyWhileWanted() {
    GlEnv env;
    QVERIFY(env.create());
    QOpenGLExtraFunctions* f = env.context.extraFunctions();
    GLuint tex = 0;
    f->glGenTextures(1, &tex);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(kRgba8), 64, 48, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    FakeCuda fake;
    {
      GpuEncodeBridge b(QSize(100, 100), fake.deps());
      b.setWanted(true);
      emu::GpuEncodeTarget::Attach st = b.attach(tex, 64, 48);
      QElapsedTimer timer;
      timer.start();
      while (st == emu::GpuEncodeTarget::Attach::NotReady && timer.elapsed() < 5000) {
        waitForPool();
        st = b.attach(tex, 64, 48);
      }
      QCOMPARE(st, emu::GpuEncodeTarget::Attach::Ok);
      QVERIFY(b.capture());
      QVERIFY(b.capture());
      QVERIFY(b.capture());
      QCOMPARE(FakeCuda::liveFrames(), 1);  // three captures, one slot: the older two went back to the pool
      std::shared_ptr<AVFrame> taken = b.takeFrame();
      QVERIFY(taken);
      QCOMPARE(taken->width, 64);
      QCOMPARE(taken->height, 48);
      QVERIFY(!b.takeFrame());
      taken.reset();
      QCOMPARE(FakeCuda::liveFrames(), 0);

      b.setWanted(false);
      QVERIFY(b.capture());  // a capture that was already running: succeeds, but the frame is not kept
      QCOMPARE(FakeCuda::liveFrames(), 0);
      QVERIFY(!b.takeFrame());
      b.setWanted(true);
      QVERIFY(b.capture());
      QVERIFY(b.takeFrame());

      // fail(): the first reason wins, the bridge is no longer wanted
      QCOMPARE(b.state(), GpuEncodeBridge::State::Running);
      b.fail(QStringLiteral("first"));
      b.fail(QStringLiteral("second"));
      QCOMPARE(b.state(), GpuEncodeBridge::State::Failed);
      QCOMPARE(b.reason(), QStringLiteral("first"));
      QVERIFY(!b.wanted());
      b.detach(true);
    }
    f->glDeleteTextures(1, &tex);
    checkBalanced(fake);
  }

  // attach() maps the capture's status: Unavailable (here: no GL context) stays Unavailable for the caller (released
  // quietly) and Failed (here: no frame pool) stays Failed; each ends in its own state for the UI, and fail() never
  // turns Unavailable into Failed.
  void attachMapsStatuses() {
    {
      QVERIFY(!QOpenGLContext::currentContext());
      FakeCuda fake;
      GpuEncodeBridge b(QSize(100, 100), fake.deps());
      b.setWanted(true);
      QCOMPARE(b.attach(1, 64, 48), emu::GpuEncodeTarget::Attach::Unavailable);
      QCOMPARE(b.state(), GpuEncodeBridge::State::Unavailable);
      QVERIFY2(b.reason().contains(QStringLiteral("OpenGL")), qPrintable(b.reason()));
      const QString reason = b.reason();
      b.fail(QStringLiteral("later"));
      QCOMPARE(b.state(), GpuEncodeBridge::State::Unavailable);
      QCOMPARE(b.reason(), reason);
      QVERIFY(!b.wanted());
    }
    {
      GlEnv env;
      QVERIFY(env.create());
      QOpenGLExtraFunctions* f = env.context.extraFunctions();
      GLuint tex = 0;
      f->glGenTextures(1, &tex);
      f->glBindTexture(GL_TEXTURE_2D, tex);
      f->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(kRgba8), 64, 48, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      FakeCuda fake;
      fake.setPoolFailure(QStringLiteral("no pool"));
      {
        GpuEncodeBridge b(QSize(100, 100), fake.deps());
        b.setWanted(true);
        emu::GpuEncodeTarget::Attach st = b.attach(tex, 64, 48);
        QElapsedTimer timer;
        timer.start();
        while (st == emu::GpuEncodeTarget::Attach::NotReady && timer.elapsed() < 5000) {
          waitForPool();
          st = b.attach(tex, 64, 48);
        }
        QCOMPARE(st, emu::GpuEncodeTarget::Attach::Failed);
        QCOMPARE(b.state(), GpuEncodeBridge::State::Failed);
        QVERIFY2(!b.reason().isEmpty(), "a failure without a reason");
        b.fail(QStringLiteral("later"));
        QVERIFY(b.reason() != QStringLiteral("later"));  // the first reason wins
        b.detach(true);
      }
      f->glDeleteTextures(1, &tex);
      checkBalanced(fake);
    }
  }

  // A CUDA error in the middle of the Session: the bridge fails with the reason, the emulation side detaches the target
  // (registration released with GL current) and the display keeps running.
  void captureFailureFailsTheBridge() {
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(kShareEncodeMax, fake.deps());
      bridge->setWanted(true);
      LibretroBackend be;
      loadFake(be);
      be.setGpuEncodeTarget(bridge);
      QVERIFY(runUntilFrame(be, *bridge));
      fake.failNext("cuGraphicsMapResources", 1);  // CUDA_ERROR_INVALID_VALUE: share-scoped, not fatal
      QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("GPU-direct capture failed at map.*")));
      QTest::ignoreMessage(QtWarningMsg, "Session encode target dropped (capture failed); the Session encoder gets readback frames");
      for (int i = 0; i < 4 && bridge->state() == GpuEncodeBridge::State::Running; ++i) QVERIFY(be.runFrame());
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Failed);
      QVERIFY2(bridge->reason().startsWith(QStringLiteral("map")), qPrintable(bridge->reason()));
      QVERIFY(!bridge->wanted());
      QCOMPARE(fake.counters().maps, fake.counters().unmaps);
      QCOMPARE(fake.counters().registrations, fake.counters().unregistrations);  // dropTarget detached before freeing
      // The Session keeps running with display frames.
      const quint64 frames = be.frameCount();
      for (int i = 0; i < 3; ++i) QVERIFY(be.runFrame());
      QCOMPARE(be.frameCount(), frames + 3);
      QCOMPARE(fake.counters().registrations, 1);
      be.unloadGame();
      be.unloadCore();
    }
    checkBalanced(fake);
  }

  // GameSession: the target is stored and applied by launch(); unloading detaches it; a second start() with the target
  // still set attaches again.
  void secondStartKeepsTheTarget() {
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(kShareEncodeMax, fake.deps());
      bridge->setWanted(true);
      GameSession gs;
      bool arrived = false;
      QVERIFY(!gs.hardwareRendered());
      gs.setGpuEncodeTarget(bridge);  // before start(): applied by launch()
      gs.start(config());
      QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 10000);
      QVERIFY(gs.hardwareRendered());
      arrived = false;
      QTRY_VERIFY_WITH_TIMEOUT(frameArrived(*bridge, &arrived), 10000);
      QCOMPARE(fake.counters().registrations, 1);
      gs.stop();
      QVERIFY(!gs.hardwareRendered());
      QCOMPARE(fake.counters().unregistrations, 1);  // teardown left the target detached
      bridge->takeFrame();
      gs.start(config());
      QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 10000);
      arrived = false;
      QTRY_VERIFY_WITH_TIMEOUT(frameArrived(*bridge, &arrived), 10000);
      QCOMPARE(fake.counters().registrations, 2);
      QCOMPARE(fake.counters().contextsCreated, 1);  // the CUDA context lives on in the bridge
      gs.setGpuEncodeTarget(nullptr);                // dropped while running: detached at the next frame
      QTRY_COMPARE_WITH_TIMEOUT(fake.counters().unregistrations, 2, 10000);
      gs.stop();
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Running);
    }
    checkBalanced(fake);
  }

  // Live save while shared (GameSession::restartWithSave): the core restarts on a new runner, no new started() and the
  // target stays: it re-attaches (new texture, same CUDA context) and frames flow again.
  void restartWhileSharedReattachesAndFramesFlowAgain() {
    FakeCuda fake;
    {
      auto bridge = std::make_shared<GpuEncodeBridge>(kShareEncodeMax, fake.deps());
      bridge->setWanted(true);
      GameSession gs;
      bool arrived = false;
      QSignalSpy started(&gs, &GameSession::started);
      gs.setGpuEncodeTarget(bridge);
      gs.start(config());
      QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 10000);
      arrived = false;
      QTRY_VERIFY_WITH_TIMEOUT(frameArrived(*bridge, &arrived), 10000);
      QCOMPARE(started.count(), 1);
      QCOMPARE(fake.counters().registrations, 1);
      QCOMPARE(fake.counters().contextsCreated, 1);

      bridge->takeFrame();  // nothing left over from the first runner
      QSignalSpy stateSpy(&gs, &GameSession::stateChanged);
      QVERIFY(gs.restartWithSave(dir_.filePath(QStringLiteral("restart.sav")), QByteArray("save")));
      QCOMPARE(fake.counters().unregistrations, 1);  // the old runner detached the target with GL current while unloading
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Running);
      // The window SessionController::updateGpuEncode has to survive: the new core is starting, so hardwareRendered() is
      // false although the target stays set; the controller keeps its bridge by the state (Starting) and runs its plan
      // again on the stateChanged that follows (Running).
      QCOMPARE(gs.state(), GameSession::Starting);
      QVERIFY(!gs.hardwareRendered());
      QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 10000);
      QVERIFY(stateSpy.count() >= 1);
      arrived = false;
      QTRY_VERIFY_WITH_TIMEOUT(frameArrived(*bridge, &arrived), 10000);  // frames flow again
      QCOMPARE(started.count(), 1);                                    // same Session: no second started()
      QCOMPARE(fake.counters().registrations, 2);                      // a new texture was registered ...
      QCOMPARE(fake.counters().contextsCreated, 1);                    // ... in the same CUDA context
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Running);
      QVERIFY(gs.hardwareRendered());

      gs.stop();
      QCOMPARE(fake.counters().unregistrations, 2);
      QCOMPARE(bridge->state(), GpuEncodeBridge::State::Running);
    }
    checkBalanced(fake);
  }
};

int main(int argc, char** argv) {
  fbtest::prepareProcess();
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
  GpuEncodeTest t;
  return QTest::qExec(&t, argc, argv);
}

#include "gpu_encode_test.moc"
