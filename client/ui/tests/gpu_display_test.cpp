// Zero-copy display (ADR 0027), UI side: GameSession with a fake platform display (a fake producer around a real GL texture
// plus a fake DisplayTextureSource on the software scene graph) end to end with the fake hardware core, the readback plan
// that follows the display state, and GameView drawing the GPU texture (single and split layouts). Needs a GL 3.3 core
// context for the core; without one the test returns 77 (ctest: SKIP) - except on Linux CI (environment variable CI set),
// where a missing context is a failure so the test cannot silently vanish.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QQuickWindow>
#include <QSGTexture>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

#include <atomic>
#include <memory>

#include "fake_gl_display.h"
#include "gameview.h"
#include "gamesession.h"
#include "gpudisplaybridge.h"
#include "processguard.h"

using namespace framebeam;
using namespace framebeam::ui;

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

// What the fake ring shares between its producer (emulation thread) and its source (render thread).
struct Shared {
  fbtest::FakeGlDisplay gl;
  QMutex mutex;
  QImage latest;  // the last presented frame (RGB32, w x h)
  quint64 presents = 0;
  std::atomic<int> textureCalls{0};
  std::atomic<int> textureHits{0};
  std::atomic<bool> sourceFailed{false};
  QSize lastContent;
};

class FakeProducer : public DisplayProducer {
 public:
  explicit FakeProducer(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  Status acquire(int w, int h, unsigned* tex, QSize* texSize) override {
    switch (s_->gl.acquire(w, h, tex, texSize)) {
      case fbtest::FakeGlDisplay::Status::Ok: return Status::Ok;
      case fbtest::FakeGlDisplay::Status::Skip: return Status::Skip;
      case fbtest::FakeGlDisplay::Status::NotReady: return Status::NotReady;
      case fbtest::FakeGlDisplay::Status::Unavailable: return Status::Unavailable;
      case fbtest::FakeGlDisplay::Status::Failed: return Status::Failed;
    }
    return Status::Failed;
  }
  void present(int w, int h) override {
    s_->gl.present(w, h);
    QMutexLocker l(&s_->mutex);
    s_->latest = s_->gl.lastImage();
    ++s_->presents;
  }
  void abort() override { s_->gl.abort(); }
  void release(bool glCurrent) override { s_->gl.release(glCurrent); }
  QString reason() const override { return QStringLiteral("fake ring says no"); }
  QString adapterName() const override { return QStringLiteral("Fake GPU"); }

 private:
  std::shared_ptr<Shared> s_;
};

// The "GPU texture": the presented image in the top-left corner of a LARGER image (magenta around it), so a wrong source
// rect shows up as magenta. Created from an image on the (software) scene graph; a new one per presented frame.
class FakeSource : public DisplayTextureSource {
 public:
  explicit FakeSource(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  // Like the real source: the textures go away on the render thread, before the next sync (a node may still hold one).
  ~FakeSource() override {
    struct Job : QRunnable {
      QSGTexture *a, *b;
      Job(QSGTexture* x, QSGTexture* y) : a(x), b(y) {}
      void run() override { delete a; delete b; }
    };
    if (window_) window_->scheduleRenderJob(new Job(current_, previous_), QQuickWindow::BeforeSynchronizingStage);
    else {
      delete current_;
      delete previous_;
    }
  }
  QSGTexture* texture(QQuickWindow* window, QSize* contentSize) override {
    QImage frame;
    quint64 n = 0;
    {
      QMutexLocker l(&s_->mutex);
      frame = s_->latest;
      n = s_->presents;
    }
    ++s_->textureCalls;
    window_ = window;
    if (frame.isNull()) return nullptr;
    if (n != seen_ || !current_) {
      QImage padded(frame.width() + 40, frame.height() + 20, QImage::Format_RGB32);
      padded.fill(qRgb(255, 0, 255));
      QPainter p(&padded);
      p.drawImage(0, 0, frame);
      p.end();
      delete previous_;  // the one before is certainly out of the scene graph by now
      previous_ = current_;
      current_ = window->createTextureFromImage(padded);
      seen_ = n;
    }
    ++s_->textureHits;
    s_->lastContent = frame.size();
    *contentSize = frame.size();
    return current_;
  }
  bool failed() const override { return s_->sourceFailed.load(); }

 private:
  std::shared_ptr<Shared> s_;
  QSGTexture* current_ = nullptr;
  QSGTexture* previous_ = nullptr;
  QPointer<QQuickWindow> window_;
  quint64 seen_ = 0;
};

enum class Kind { Normal, Unavailable, NullProducer };

struct Factory {
  Kind kind = Kind::Normal;
  int launches = 0;
  std::shared_ptr<Shared> last;
  PlatformDisplay operator()(QString* reason) {
    ++launches;
    PlatformDisplay pd;
    if (kind == Kind::NullProducer) {
      if (reason) *reason = QStringLiteral("fake: not here");
      return pd;
    }
    auto sh = std::make_shared<Shared>();
    if (kind == Kind::Unavailable) sh->gl.nextStatus = static_cast<int>(fbtest::FakeGlDisplay::Status::Unavailable);
    last = sh;
    pd.producer = std::make_unique<FakeProducer>(sh);
    pd.source = std::make_unique<FakeSource>(sh);
    pd.shared = sh;
    return pd;
  }
};

constexpr int kViewW = 128, kViewH = 96;

// The fake core's pattern (64x48; red bottom-left, green top-right, blue elsewhere) probed in a rect of a grabbed window.
QString probePattern(const QImage& shot, const QRectF& r, const QRectF& part) {
  // `part`: the part of the frame shown in r, in frame fractions (0..1)
  struct P { double fx, fy; QRgb c; };
  const double m = 0.06;  // well inside the squares (1/8 of the frame wide)
  const P probes[] = {{m, 1 - m, qRgb(255, 0, 0)}, {1 - m, m, qRgb(0, 255, 0)}, {m, m, qRgb(0, 0, 255)},
                      {1 - m, 1 - m, qRgb(0, 0, 255)}, {0.5, 0.5, qRgb(0, 0, 255)}};
  for (const P& p : probes) {
    // frame fraction -> position inside this rect, only for probes whose frame point lies in `part`
    const double gx = p.fx * 1.0, gy = p.fy * 1.0;
    if (gx < part.left() || gx > part.right() || gy < part.top() || gy > part.bottom()) continue;
    const double u = (gx - part.left()) / part.width(), v = (gy - part.top()) / part.height();
    const QPoint pt(static_cast<int>(r.left() + u * (r.width() - 1)), static_cast<int>(r.top() + v * (r.height() - 1)));
    const QRgb got = shot.pixel(pt) | 0xFF000000u;
    if (got != p.c) return QStringLiteral("pixel (%1,%2) is %3, expected %4").arg(pt.x()).arg(pt.y()).arg(got, 8, 16).arg(p.c, 8, 16);
  }
  return {};
}

}  // namespace

class GpuDisplayTest : public QObject {
  Q_OBJECT
  QTemporaryFile rom_;
  QTemporaryDir dir_;
  Factory factory_;

  GameSession::LaunchConfig config(bool split = false) const {
    GameSession::LaunchConfig c;
    c.title = QStringLiteral("Fake HW");
    c.corePath = QStringLiteral(FB_FAKE_HW_CORE_PATH);
    c.gamePath = rom_.fileName();
    c.systemDir = dir_.filePath(QStringLiteral("sys"));
    c.saveDir = dir_.filePath(QStringLiteral("save"));
    if (split) {  // the 64x48 frame as two stacked 64x24 screens
      c.display.layout = QStringLiteral("vertical");
      c.display.gap = 0;
      c.display.screens = {{QStringLiteral("top"), 64, 24, false}, {QStringLiteral("bottom"), 64, 24, true}};
    }
    return c;
  }
  void install(Kind kind = Kind::Normal) {
    factory_ = Factory{};
    factory_.kind = kind;
    GameSession::setPlatformDisplayFactoryForTests([this](QString* r) { return factory_(r); });
  }

 private slots:
  void initTestCase() {
    QVERIFY(rom_.open());
    rom_.write("x");
    rom_.flush();
    QVERIFY(dir_.isValid());
  }
  void cleanup() {
    GameSession::setPlatformDisplayFactoryForTests({});
    qunsetenv("FB_FAKE_HW_COUNTER");
  }

  // The whole life of the zero-copy display of one game: Pending (readback runs, the view draws the readback image), Active
  // after the view drew a GPU frame (readback off, frames still delivered), shared Session (readback back at the share
  // size), unshared again, then the producer fails: Failed, readback resumes, the view draws readback frames; the next game
  // tries again.
  void activeFailedAndRetriedOnTheNextGame() {
    install();
    GameSession gs;
    QQuickWindow window;
    window.resize(kViewW, kViewH);
    GameView view;
    view.setParentItem(window.contentItem());
    view.setSize(QSizeF(kViewW, kViewH));
    view.setSession(&gs);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QSignalSpy displayChanged(&gs, &GameSession::displayStateChanged);
    QCOMPARE(gs.displayState(), GpuDisplayBridge::State::Unavailable);  // no game yet
    QVERIFY(!gs.displaySource());

    gs.start(config());
    QCOMPARE(factory_.launches, 1);
    QCOMPARE(gs.displayState(), GpuDisplayBridge::State::Pending);
    QVERIFY(gs.displayReadbackEnabled());  // not confirmed yet: the view needs readback frames
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Active, 10000);  // the view drew a GPU frame
    QTRY_VERIFY_WITH_TIMEOUT(displayChanged.count() >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!gs.displayReadbackEnabled(), 5000);  // nothing else needs pixels
    auto sh = factory_.last;
    QVERIFY(sh);
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().zeroCopy && gs.diagnostics().displaysPerSec > 5.0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec == 0.0, 5000);
    {  // the readback image is stale now, the frames keep coming, the picture is right
      QVERIFY(gs.frame().isNull());  // the stale readback image was dropped when the display went Active
      const qint64 key = gs.frame().cacheKey();
      const quint64 nr = gs.frameNumber();
      QTRY_VERIFY_WITH_TIMEOUT(gs.frameNumber() >= nr + 5, 5000);
      QCOMPARE(gs.frame().cacheKey(), key);
      QVERIFY(gs.diagnostics().readbackSize.isEmpty());
      const int hits = sh->textureHits.load();
      QTRY_VERIFY_WITH_TIMEOUT(sh->textureHits.load() > hits, 5000);  // the view draws the GPU texture
      const QImage shot = window.grabWindow();
      QVERIFY2(probePattern(shot, QRectF(0, 0, kViewW, kViewH), QRectF(0, 0, 1, 1)).isEmpty(),
               qPrintable(probePattern(shot, QRectF(0, 0, kViewW, kViewH), QRectF(0, 0, 1, 1))));
      QCOMPARE(sh->lastContent, QSize(64, 48));
    }

    gs.setShareSize(QSize(1280, 1920));  // shared without GPU-direct encoding: pixels for the Session
    QVERIFY(gs.displayReadbackEnabled());
    QCOMPARE(gs.readbackLimit(), QSize(1280, 1920));
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec > 5.0 && gs.diagnostics().displaysPerSec > 5.0, 5000);
    QCOMPARE(gs.displayState(), GpuDisplayBridge::State::Active);  // the GPU frames stay
    gs.setShareSize(QSize());
    QVERIFY(!gs.displayReadbackEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec == 0.0, 5000);

    sh->gl.nextStatus = static_cast<int>(fbtest::FakeGlDisplay::Status::Failed);
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Failed, 10000);
    QCOMPARE(gs.displayReason(), QStringLiteral("fake ring says no"));
    QTRY_VERIFY_WITH_TIMEOUT(gs.displayReadbackEnabled(), 5000);  // readback resumes ...
    QVERIFY(!gs.displaySource());                                 // ... and the view draws it
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec > 5.0, 5000);
    QVERIFY(!gs.diagnostics().zeroCopy);
    {
      const QImage shot = window.grabWindow();
      QVERIFY2(probePattern(shot, QRectF(0, 0, kViewW, kViewH), QRectF(0, 0, 1, 1)).isEmpty(),
               qPrintable(probePattern(shot, QRectF(0, 0, kViewW, kViewH), QRectF(0, 0, 1, 1))));
    }
    QVERIFY2(sh->gl.violations().isEmpty(), qPrintable(sh->gl.violations().join(QLatin1Char(';'))));
    QCOMPARE(gs.state(), GameSession::Running);  // nothing stops a game

    gs.stop();  // the next game tries again
    gs.start(config());
    QCOMPARE(factory_.launches, 2);
    QCOMPARE(gs.displayState(), GpuDisplayBridge::State::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Active, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!gs.displayReadbackEnabled(), 5000);
    QVERIFY(factory_.last != sh);
    gs.stop();
    QCOMPARE(factory_.last->gl.count(QStringLiteral("release")), 1);  // the game quit released the ring (GL current)
    QVERIFY(factory_.last->gl.lastReleaseGlCurrent());
    QVERIFY(factory_.last->gl.violations().isEmpty());
  }

  // The texture source giving up (render side) ends in Failed as well, and readback resumes.
  void sourceFailureEndsInFailed() {
    install();
    GameSession gs;
    QQuickWindow window;
    window.resize(kViewW, kViewH);
    GameView view;
    view.setParentItem(window.contentItem());
    view.setSize(QSizeF(kViewW, kViewH));
    view.setSession(&gs);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    gs.start(config());
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Active, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!gs.displayReadbackEnabled(), 5000);
    factory_.last->sourceFailed = true;
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Failed, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(gs.displayReadbackEnabled(), 5000);
    QVERIFY(!gs.displaySource());
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec > 5.0, 5000);
    QCOMPARE(gs.state(), GameSession::Running);
    gs.stop();
  }

  void unavailableKeepsTheReadback() {
    install(Kind::Unavailable);
    GameSession gs;
    QQuickWindow window;
    window.resize(kViewW, kViewH);
    GameView view;
    view.setParentItem(window.contentItem());
    view.setSize(QSizeF(kViewW, kViewH));
    view.setSession(&gs);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    gs.start(config());
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Unavailable, 10000);
    QCOMPARE(gs.displayReason(), QStringLiteral("fake ring says no"));
    QVERIFY(gs.displayReadbackEnabled());
    QVERIFY(!gs.displaySource());
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec > 5.0, 5000);
    QCOMPARE(gs.diagnostics().displaysPerSec, 0.0);
    const QImage shot = window.grabWindow();
    QVERIFY2(probePattern(shot, QRectF(0, 0, kViewW, kViewH), QRectF(0, 0, 1, 1)).isEmpty(), "readback picture");
    gs.stop();
  }

  void noPlatformDisplayChangesNothing() {
    install(Kind::NullProducer);
    GameSession gs;
    QQuickWindow window;
    window.resize(kViewW, kViewH);
    GameView view;
    view.setParentItem(window.contentItem());
    view.setSize(QSizeF(kViewW, kViewH));
    view.setSession(&gs);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    gs.start(config());
    QCOMPARE(factory_.launches, 1);
    QCOMPARE(gs.displayState(), GpuDisplayBridge::State::Unavailable);
    QVERIFY(!gs.displaySource());
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(gs.diagnostics().readbacksPerSec > 5.0, 5000);
    QVERIFY(gs.displayReadbackEnabled());
    QVERIFY(!gs.diagnostics().zeroCopy);
    QCOMPARE(gs.diagnostics().displaysPerSec, 0.0);
    gs.setShareSize(QSize(1280, 1920));  // the limit rules are the old ones
    QCOMPARE(gs.readbackLimit(), QSize(1280, 1920).expandedTo(QSize(kViewW, kViewH)));
    gs.stop();
  }

  // A live-save restart replaces the display: every view is told (displayStateChanged) so it rebuilds its node in the next
  // sync, also though no frame signal comes with the restart; the old ring is gone, the picture stays right.
  void restartWhileActiveTellsTheViews() {
    install();
    GameSession gs;
    QQuickWindow window;
    window.resize(kViewW, kViewH);
    GameView view;
    view.setParentItem(window.contentItem());
    view.setSize(QSizeF(kViewW, kViewH));
    view.setSession(&gs);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    gs.start(config());
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Active, 10000);
    std::weak_ptr<Shared> old = factory_.last;
    QSignalSpy changed(&gs, &GameSession::displayStateChanged);
    QSignalSpy frames(&gs, &GameSession::frameChanged);
    QVERIFY(gs.restartWith([]() { return true; }));
    QVERIFY(changed.count() >= 1);  // emitted synchronously by the replacement, not by a frame
    QCOMPARE(frames.count(), 0);
    QCOMPARE(factory_.launches, 2);
    QVERIFY(factory_.last != old.lock());
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Active, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!gs.displayReadbackEnabled(), 5000);
    const QImage shot = window.grabWindow();
    QVERIFY2(probePattern(shot, QRectF(0, 0, kViewW, kViewH), QRectF(0, 0, 1, 1)).isEmpty(), "picture after the restart");
    gs.stop();
  }

  // The bridge on its own: state machine, callback and the log lines of D5 (once each).
  void bridgeStatesCallbackAndLogLines() {
    {
      auto sh = std::make_shared<Shared>();
      PlatformDisplay pd;
      pd.producer = std::make_unique<FakeProducer>(sh);
      pd.source = std::make_unique<FakeSource>(sh);
      pd.shared = sh;
      GpuDisplayBridge bridge(std::move(pd));
      std::atomic<int> notified{0};
      bridge.setStateCallback([&]() { ++notified; });
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Pending);
      QVERIFY(bridge.wanted());
      QVERIFY(bridge.source());
      QTest::ignoreMessage(QtInfoMsg, "Zero-copy display active: D3D11 textures on \"Fake GPU\" (no readback for the game view)");
      bridge.confirmShown();
      bridge.confirmShown();  // once
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Active);
      QCOMPARE(notified.load(), 1);
      QVERIFY(bridge.wanted());
      QTest::ignoreMessage(QtInfoMsg, "Zero-copy display off for this game (blit failed); the game view uses readback frames");
      bridge.fail(QStringLiteral("blit failed"));
      bridge.fail(QStringLiteral("second reason"));  // the first one wins, no second line
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Failed);
      QCOMPARE(bridge.reason(), QStringLiteral("blit failed"));
      QCOMPARE(notified.load(), 2);
      QVERIFY(!bridge.wanted());
      QVERIFY(!bridge.source());
      bridge.confirmShown();  // no way back
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Failed);
    }
    {  // the producer says Unavailable on the first acquire: quiet state, one info line
      auto sh = std::make_shared<Shared>();
      sh->gl.nextStatus = static_cast<int>(fbtest::FakeGlDisplay::Status::Unavailable);
      PlatformDisplay pd;
      pd.producer = std::make_unique<FakeProducer>(sh);
      pd.source = std::make_unique<FakeSource>(sh);
      pd.shared = sh;
      GpuDisplayBridge bridge(std::move(pd));
      unsigned tex = 0;
      QSize ts;
      QTest::ignoreMessage(QtInfoMsg, "Zero-copy display not available (fake ring says no); the game view uses readback frames");
      QCOMPARE(bridge.acquire(64, 48, &tex, &ts), emu::GpuDisplayTarget::Acquire::Unavailable);
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Unavailable);
      bridge.fail(QStringLiteral("later"));  // Unavailable is never turned into Failed
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Unavailable);
      QVERIFY(!bridge.source() && !bridge.wanted());
      bridge.confirmShown();
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Unavailable);
    }
    {  // a failing source ends in Failed through wanted() (emulation thread) and source() (render thread)
      auto sh = std::make_shared<Shared>();
      PlatformDisplay pd;
      pd.producer = std::make_unique<FakeProducer>(sh);
      pd.source = std::make_unique<FakeSource>(sh);
      pd.shared = sh;
      GpuDisplayBridge bridge(std::move(pd));
      sh->sourceFailed = true;
      QTest::ignoreMessage(QtInfoMsg, "Zero-copy display off for this game (the texture source failed); the game view uses readback frames");
      QVERIFY(!bridge.wanted());
      QCOMPARE(bridge.state(), GpuDisplayBridge::State::Failed);
    }
  }

  // Split layout: each screen is a part of the GPU texture (its source rect computed from the delivered content size, not
  // from the larger texture), identical to the readback picture.
  void splitLayoutDrawsPerScreenSourceRects_data() {
    QTest::addColumn<QString>("layout");
    QTest::newRow("side by side") << QStringLiteral("side");
    QTest::newRow("top only") << QStringLiteral("top");
  }

  void splitLayoutDrawsPerScreenSourceRects() {
    QFETCH(QString, layout);
    install();
    GameSession gs;
    QQuickWindow window;
    window.resize(256, kViewH);
    GameView view;
    view.setParentItem(window.contentItem());
    view.setSize(QSizeF(256, kViewH));
    view.setSession(&gs);
    view.setLayout(layout);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    gs.start(config(true));
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Active, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!gs.displayReadbackEnabled(), 5000);
    auto sh = factory_.last;
    const auto probeAll = [&]() -> QString {
      const QImage shot = window.grabWindow();
      const QList<QRectF> targets = view.placement().targets;
      if (targets.size() != 2) return QStringLiteral("no placement");
      // top screen = upper half of the frame, bottom screen = lower half
      if (!targets.at(0).isEmpty()) {
        const QString r = probePattern(shot, targets.at(0), QRectF(0, 0, 1, 0.5));
        if (!r.isEmpty()) return QStringLiteral("top screen: ") + r;
      }
      if (!targets.at(1).isEmpty()) {
        const QString r = probePattern(shot, targets.at(1), QRectF(0, 0.5, 1, 0.5));
        if (!r.isEmpty()) return QStringLiteral("bottom screen: ") + r;
      }
      for (int y = 0; y < shot.height(); y += 7)  // the padding of the texture (magenta) is never drawn
        for (int x = 0; x < shot.width(); x += 7)
          if ((shot.pixel(x, y) | 0xFF000000u) == qRgb(255, 0, 255)) return QStringLiteral("padding visible at %1,%2").arg(x).arg(y);
      return {};
    };
    const int hits = sh->textureHits.load();
    QTRY_VERIFY_WITH_TIMEOUT(sh->textureHits.load() > hits + 2, 5000);
    QVERIFY(!view.placement().targets.isEmpty());
    if (layout == QLatin1String("top")) QVERIFY(view.placement().targets.at(1).isEmpty());  // the bottom screen is hidden
    else QVERIFY(!view.placement().targets.at(1).isEmpty());
    QString why = probeAll();
    QVERIFY2(why.isEmpty(), qPrintable(why));
    QCOMPARE(sh->lastContent, QSize(64, 48));

    // The same picture from the readback frames once the display has failed.
    sh->gl.nextStatus = static_cast<int>(fbtest::FakeGlDisplay::Status::Failed);
    QTRY_COMPARE_WITH_TIMEOUT(gs.displayState(), GpuDisplayBridge::State::Failed, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(gs.displayReadbackEnabled(), 5000);
    const quint64 nr = gs.frameNumber();
    QTRY_VERIFY_WITH_TIMEOUT(gs.frameNumber() >= nr + 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(probeAll().isEmpty(), 5000);
    gs.stop();
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
  GpuDisplayTest t;
  return QTest::qExec(&t, argc, argv);
}

#include "gpu_display_test.moc"
