// GPU-direct encoding (ADR 0019): the real CudaGlCapture on the fake CUDA API of testutil/fake_cuda.h and a real GL 3.3
// core context. The fake enforces the interop rules (GL current, context pushed, map/unmap pairing, texture format and
// liveness) and copies the texture into host-memory frames, so the whole call sequence runs on any machine with GL,
// e.g. Mesa llvmpipe under xvfb. SKIP/77 without a GL 3.3 context, except on Linux CI where it must run.
#include "fake_cuda.h"  // first: it includes the CUDA shim before any FFmpeg CUDA header

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QtTest>
#include <thread>
#include <vector>

#include "cudadriver.h"
#include "cudaglcapture.h"
#include "processguard.h"

using namespace framebeam;
using fbtest::FakeCuda;
using Status = CudaGlCapture::Status;

namespace {

constexpr unsigned kRgba8 = 0x8058;
constexpr unsigned kRgb8 = 0x8051;

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
GlEnv* g_env = nullptr;

QOpenGLExtraFunctions* gl() { return QOpenGLContext::currentContext()->extraFunctions(); }

// Known top-down pattern: texture row y (= image row y, the top row is GL row 0) and column x.
uint8_t patternByte(int x, int y, int channel) {
  switch (channel) {
    case 0: return static_cast<uint8_t>(x * 3 + 1);
    case 1: return static_cast<uint8_t>(y * 5 + 2);
    case 2: return static_cast<uint8_t>(x * 7 + y * 11);
    default: return 0xFF;
  }
}

std::vector<uint8_t> patternPixels(int w, int h, int seed = 0) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < 4; ++c) {
        px[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(patternByte(x, y, c) + (c < 3 ? seed : 0));
      }
    }
  }
  return px;
}

unsigned makeTexture(int w, int h, unsigned internalFormat = kRgba8, int seed = 0) {
  QOpenGLExtraFunctions* f = gl();
  GLuint tex = 0;
  f->glGenTextures(1, &tex);
  f->glBindTexture(GL_TEXTURE_2D, tex);
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const std::vector<uint8_t> px = patternPixels(w, h, seed);
  f->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return tex;
}

void updateTexture(unsigned tex, int w, int h, int seed) {
  QOpenGLExtraFunctions* f = gl();
  f->glBindTexture(GL_TEXTURE_2D, tex);
  const std::vector<uint8_t> px = patternPixels(w, h, seed);
  f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

void deleteTexture(unsigned tex) {
  const GLuint t = tex;
  gl()->glDeleteTextures(1, &t);
}

Status attachUntilDecided(CudaGlCapture& cap, unsigned tex, int w, int h) {
  QElapsedTimer timer;
  timer.start();
  Status st = cap.attach(tex, w, h);
  while (st == Status::NotReady && timer.elapsed() < 5000) {
    QThread::msleep(2);
    st = cap.attach(tex, w, h);
  }
  return st;
}

// Rows top-down, bytes R,G,B at the pitch of the frame, the padding behind each row untouched (poisoned 0xCD).
QString checkFrame(const AVFrame* f, int w, int h, int seed = 0) {
  if (!f) return QStringLiteral("no frame");
  if (f->width != w || f->height != h) return QStringLiteral("frame is %1x%2, not %3x%4").arg(f->width).arg(f->height).arg(w).arg(h);
  if (f->format != AV_PIX_FMT_CUDA) return QStringLiteral("format %1").arg(f->format);
  if (f->linesize[0] < w * 4 || f->linesize[0] % 256 != 0) return QStringLiteral("pitch %1").arg(f->linesize[0]);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = f->data[0] + static_cast<size_t>(y) * f->linesize[0];
    for (int x = 0; x < w; ++x) {
      for (int c = 0; c < 3; ++c) {
        const int want = static_cast<uint8_t>(patternByte(x, y, c) + seed);
        if (row[x * 4 + c] != want) {
          return QStringLiteral("pixel (%1,%2) channel %3 is %4, expected %5").arg(x).arg(y).arg(c).arg(row[x * 4 + c]).arg(want);
        }
      }
    }
    for (int i = w * 4; i < f->linesize[0]; ++i) {
      if (row[i] != 0xCD) return QStringLiteral("row %1 padding byte %2 was written").arg(y).arg(i);
    }
  }
  return {};
}

}  // namespace

class CudaGlCaptureTest : public QObject {
  Q_OBJECT

  static void waitForPool() { QThreadPool::globalInstance()->waitForDone(); }

 private slots:
  void init() { QVERIFY(g_env->context.makeCurrent(&g_env->surface)); }

  void createsTheContextOffTheCallingThread() {
    FakeCuda fake;
    fake.setContextCreateDelayMs(30);
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(cap.attach(tex, 64, 48), Status::NotReady);  // the context is created on a pool thread
      QCOMPARE(cap.status(), Status::NotReady);
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      QCOMPARE(cap.status(), Status::Ok);
      QCOMPARE(cap.deviceName(), QStringLiteral("Fake NVIDIA GPU"));

      const auto threads = fake.contextCreateThreads();
      QCOMPARE(threads.size(), size_t(1));
      QVERIFY(threads.front() != std::this_thread::get_id());
      const FakeCuda::Counters c = fake.counters();
      QCOMPARE(c.deviceQueries, 1);
      QCOMPARE(c.contextsCreated, 1);
      QCOMPARE(c.streamsCreated, 1);
      QCOMPARE(c.registrations, 1);
      QCOMPARE(fake.lastContextFlags(), cuda::kCtxSchedBlockingSync);
      QCOMPARE(fake.lastStreamFlags(), cuda::kStreamNonBlocking);
      QCOMPARE(fake.lastRegisterFlags(), cuda::kRegisterReadOnly);
      QCOMPARE(fake.configureCalls(), std::vector<QSize>{QSize(64, 48)});

      QCOMPARE(cap.attach(tex, 64, 48), Status::Ok);  // asking again changes nothing
      QCOMPARE(fake.counters().registrations, 1);
      cap.detach(true);
    }
    deleteTexture(tex);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void copiesTheTextureTopDownWithTheFramePitch() {
    FakeCuda fake;
    const int w = 100, h = 74;  // 400 bytes per row: the frame pitch is 512, the padding must stay untouched
    const unsigned tex = makeTexture(w, h);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, w, h), Status::Ok);
      std::shared_ptr<AVFrame> a = cap.capture();
      QVERIFY(a);
      QCOMPARE(a->linesize[0], 512);
      QVERIFY2(checkFrame(a.get(), w, h).isEmpty(), qPrintable(checkFrame(a.get(), w, h)));

      updateTexture(tex, w, h, 40);  // the next capture sees the new content: no stale array, no cached map
      std::shared_ptr<AVFrame> b = cap.capture();
      QVERIFY(b);
      QVERIFY(b->data[0] != a->data[0]);  // a new buffer per capture, the first stays intact
      QVERIFY2(checkFrame(b.get(), w, h, 40).isEmpty(), qPrintable(checkFrame(b.get(), w, h, 40)));
      QVERIFY2(checkFrame(a.get(), w, h).isEmpty(), qPrintable(checkFrame(a.get(), w, h)));
      QCOMPARE(FakeCuda::liveFrames(), 2);
      QCOMPARE(fake.counters().maps, 2);
      QCOMPARE(fake.counters().unmaps, 2);
      QCOMPARE(fake.counters().copies, 2);
      QCOMPARE(fake.counters().syncs, 2);  // complete on the GPU before capture() returns

      // A frame outlives the capture, the registration and the texture (design I2): the context goes with the last one.
      cap.detach(true);
      deleteTexture(tex);
      a.reset();
      QCOMPARE(FakeCuda::liveFrames(), 1);
      QVERIFY(checkFrame(b.get(), w, h, 40).isEmpty());
      b.reset();
    }
    QCOMPARE(FakeCuda::liveFrames(), 0);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void anInFlightFrameKeepsTheContextAlive() {
    FakeCuda fake;
    const unsigned tex = makeTexture(32, 24);
    std::shared_ptr<AVFrame> frame;
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 32, 24), Status::Ok);
      frame = cap.capture();
      QVERIFY(frame);
      cap.detach(true);
    }
    deleteTexture(tex);
    waitForPool();
    QCOMPARE(fake.counters().streamsDestroyed, 1);  // ours goes with the capture
    QCOMPARE(fake.counters().contextsDestroyed, 0);   // the frame still holds the context
    QVERIFY(checkFrame(frame.get(), 32, 24).isEmpty());
    frame.reset();
    QCOMPARE(fake.counters().contextsDestroyed, 1);
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void unmapsAndSynchronizesWhateverFailsAfterTheMap_data() {
    QTest::addColumn<QString>("symbol");
    QTest::addColumn<QString>("step");
    QTest::addColumn<int>("maps");
    QTest::addColumn<int>("syncs");
    QTest::newRow("mapped array") << "cuGraphicsSubResourceGetMappedArray" << "mapped array" << 1 << 1;
    QTest::newRow("copy") << "cuMemcpy2DAsync_v2" << "copy" << 1 << 1;
    QTest::newRow("synchronize") << "cuStreamSynchronize" << "synchronize" << 1 << 0;
    QTest::newRow("map") << "cuGraphicsMapResources" << "map" << 0 << 0;
  }
  void unmapsAndSynchronizesWhateverFailsAfterTheMap() {
    QFETCH(QString, symbol);
    QFETCH(QString, step);
    QFETCH(int, maps);
    QFETCH(int, syncs);
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      fake.failNext(symbol.toLatin1().constData(), 205);  // a non-fatal code
      QVERIFY(!cap.capture());
      QCOMPARE(cap.status(), Status::Failed);
      QVERIFY2(cap.reason().startsWith(step + QLatin1String(": ")), qPrintable(cap.reason()));
      QVERIFY2(cap.reason().contains(QLatin1String("CUDA_ERROR_MAP_FAILED (205)")), qPrintable(cap.reason()));
      const FakeCuda::Counters c = fake.counters();
      QCOMPARE(c.maps, maps);
      QCOMPARE(c.unmaps, maps);  // unmapped whatever happened after a successful map
      QCOMPARE(c.syncs, syncs);  // and the stream synchronised, so nothing stays in flight
      QCOMPARE(fake.mappedNow(), 0);
      QCOMPARE(FakeCuda::liveFrames(), 0);  // the frame buffer went back to the pool
      QVERIFY(cuda::Driver::instance().state() != cuda::Driver::State::Dead);
      QVERIFY(!cap.capture());  // Failed is final
      cap.detach(true);
    }
    deleteTexture(tex);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void aFatalCodeKillsTheDriver() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    cuda::Driver& driver = cuda::Driver::instance();
    const cuda::Driver::State before = driver.state();
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      fake.failNext("cuGraphicsMapResources", 700);
      QVERIFY(!cap.capture());
      QCOMPARE(cap.status(), Status::Failed);
      QCOMPARE(driver.state(), cuda::Driver::State::Dead);
      QVERIFY2(driver.reason().contains(QLatin1String("CUDA_ERROR_ILLEGAL_ADDRESS (700) at map")), qPrintable(driver.reason()));
      cap.detach(true);
    }
    driver.resetForTest();
    QCOMPARE(driver.state(), before);
    deleteTexture(tex);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void notOnACudaDeviceIsUnavailable() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      fake.failNext("cuGLGetDevices_v2", 219);
      QCOMPARE(cap.attach(tex, 64, 48), Status::Unavailable);
      QCOMPARE(cap.status(), Status::Unavailable);
      QVERIFY2(cap.reason().contains(QLatin1String("CUDA_ERROR_INVALID_GRAPHICS_CONTEXT (219)")), qPrintable(cap.reason()));
      QVERIFY(cap.reason().startsWith(QLatin1String("the OpenGL context is not on a CUDA device")));
      QCOMPARE(cap.attach(tex, 64, 48), Status::Unavailable);  // final
      QVERIFY(!cap.capture());
      QCOMPARE(fake.counters().contextsCreated, 0);
    }
    {
      fake.setGlDeviceCount(0);
      CudaGlCapture cap(fake.deps());
      QCOMPARE(cap.attach(tex, 64, 48), Status::Unavailable);
      QVERIFY2(cap.reason().contains(QLatin1String("no device")), qPrintable(cap.reason()));
    }
    {
      fake.setGlDeviceCount(2);  // SLI: the first device is used
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      cap.detach(true);
    }
    deleteTexture(tex);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void withoutAGlContextItIsUnavailable() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      g_env->context.doneCurrent();
      QCOMPARE(cap.attach(tex, 64, 48), Status::Unavailable);
      QCOMPARE(cap.reason(), QStringLiteral("no current OpenGL context"));
      QVERIFY(g_env->context.makeCurrent(&g_env->surface));
      QCOMPARE(cap.attach(tex, 64, 48), Status::Unavailable);  // final, even with GL back
    }
    QCOMPARE(fake.counters().deviceQueries, 0);
    QCOMPARE(fake.counters().contextsCreated, 0);
    deleteTexture(tex);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void aLostGlContextLeaksTheRegistration() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      QTest::ignoreMessage(QtWarningMsg, "GPU-direct: GL context lost; leaking the CUDA registration");
      cap.detach(false);
      QCOMPARE(fake.counters().registrations, 1);
      QCOMPARE(fake.counters().unregistrations, 0);  // never released without GL
      cap.detach(false);                             // idempotent, nothing left to leak
    }
    deleteTexture(tex);
    waitForPool();
    QCOMPARE(fake.counters().unregistrations, 0);
    QString why;
    QVERIFY2(fake.balanced(&why, /*leakedRegistrationsOk=*/true), qPrintable(why));
  }

  void destroyingWhileTheContextIsBeingCreated() {
    FakeCuda fake;
    fake.setContextCreateDelayMs(200);
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(cap.attach(tex, 64, 48), Status::NotReady);
    }  // gone before the pool task finished: the task cleans up after itself
    waitForPool();
    QTRY_VERIFY2(fake.counters().contextsDestroyed == 1, "the context made after the capture was gone leaked");
    QCOMPARE(fake.counters().contextsCreated, 1);
    QCOMPARE(fake.counters().registrations, 0);
    deleteTexture(tex);
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void aNewSizeNeedsANewPoolFrameSizeAndANewRegistration() {
    FakeCuda fake;
    const unsigned first = makeTexture(64, 48);
    unsigned second = 0;
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, first, 64, 48), Status::Ok);
      std::shared_ptr<AVFrame> a = cap.capture();
      QVERIFY2(checkFrame(a.get(), 64, 48).isEmpty(), qPrintable(checkFrame(a.get(), 64, 48)));

      cap.detach(true);  // the owner detaches before it deletes the texture
      QCOMPARE(fake.counters().unregistrations, 1);
      deleteTexture(first);
      second = makeTexture(32, 24, kRgba8, 7);
      QCOMPARE(cap.attach(second, 32, 24), Status::Ok);  // context and stream exist: no new creation, no NotReady
      QCOMPARE(fake.counters().contextsCreated, 1);
      QCOMPARE(fake.counters().registrations, 2);
      QCOMPARE(fake.configureCalls(), (std::vector<QSize>{QSize(64, 48), QSize(32, 24)}));
      std::shared_ptr<AVFrame> b = cap.capture();
      QVERIFY2(checkFrame(b.get(), 32, 24, 7).isEmpty(), qPrintable(checkFrame(b.get(), 32, 24, 7)));
      QVERIFY2(checkFrame(a.get(), 64, 48).isEmpty(), "the old-size frame is untouched");
      cap.detach(true);
    }
    deleteTexture(second);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void aTextureOfTheWrongFormatFailsTheRegistration() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48, kRgb8);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Failed);  // the fake refuses what the driver would
      QVERIFY2(cap.reason().startsWith(QLatin1String("register: ")), qPrintable(cap.reason()));
      QCOMPARE(fake.counters().registrations, 0);
      QCOMPARE(cap.attach(tex, 64, 48), Status::Failed);  // final
    }
    QVERIFY(fake.violations().size() == 1);
    deleteTexture(tex);
    waitForPool();
    // The refusal was the point of this test, not a broken rule of ours.
    QVERIFY2(fake.counters().contextsCreated == fake.counters().contextsDestroyed, "context leaked");
    QVERIFY2(fake.counters().pushes == fake.counters().pops, "push/pop unbalanced");
  }

  void theFakeCatchesATextureDeletedBeforeTheDetach() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      deleteTexture(tex);  // the order the design forbids: detach comes first
      cap.detach(true);
    }
    waitForPool();
    QCOMPARE(fake.violations().size(), 1);
    QVERIFY2(fake.violations().first().contains(QLatin1String("after the texture was deleted")), qPrintable(fake.violations().first()));
  }

  void poolAndFrameContextFailuresFailTheCapture() {
    {
      FakeCuda fake;
      fake.setPoolFailure(QStringLiteral("no FFmpeg CUDA"));
      const unsigned tex = makeTexture(64, 48);
      {
        CudaGlCapture cap(fake.deps());
        QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Failed);
        QCOMPARE(cap.reason(), QStringLiteral("frames context: no FFmpeg CUDA"));
      }
      deleteTexture(tex);
      waitForPool();
      QString why;
      QVERIFY2(fake.balanced(&why), qPrintable(why));  // context and stream destroyed by the failing job itself
    }
    {
      FakeCuda fake;
      fake.setConfigureFailure(QStringLiteral("boom"));
      const unsigned tex = makeTexture(64, 48);
      {
        CudaGlCapture cap(fake.deps());
        QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Failed);
        QCOMPARE(cap.reason(), QStringLiteral("frames context: boom"));
      }
      deleteTexture(tex);
      waitForPool();
      QString why;
      QVERIFY2(fake.balanced(&why), qPrintable(why));
    }
    {
      FakeCuda fake;
      fake.failNext("cuCtxCreate_v2", 2);
      const unsigned tex = makeTexture(64, 48);
      {
        CudaGlCapture cap(fake.deps());
        QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Failed);
        QVERIFY2(cap.reason().startsWith(QLatin1String("context: CUDA_ERROR_OUT_OF_MEMORY (2)")), qPrintable(cap.reason()));
      }
      deleteTexture(tex);
      waitForPool();
      QString why;
      QVERIFY2(fake.balanced(&why), qPrintable(why));
    }
    {
      FakeCuda fake;
      fake.failNext("cuStreamCreate", 2);
      const unsigned tex = makeTexture(64, 48);
      {
        CudaGlCapture cap(fake.deps());
        QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Failed);
        QVERIFY2(cap.reason().startsWith(QLatin1String("stream: ")), qPrintable(cap.reason()));
      }
      deleteTexture(tex);
      waitForPool();
      QString why;
      QVERIFY2(fake.balanced(&why), qPrintable(why));
    }
  }

  void captureNeedsARegisteredTexture() {
    FakeCuda fake;
    const unsigned tex = makeTexture(64, 48);
    {
      CudaGlCapture cap(fake.deps());
      QVERIFY(!cap.capture());  // never attached: nothing to do, nothing reported
      QCOMPARE(cap.status(), Status::NotReady);
      QCOMPARE(attachUntilDecided(cap, tex, 64, 48), Status::Ok);
      cap.detach(true);
      QVERIFY(!cap.capture());  // attached once, detached since
      QCOMPARE(cap.status(), Status::Failed);
      QVERIFY(!cap.reason().isEmpty());
    }
    deleteTexture(tex);
    waitForPool();
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }

  void everyFakeCounterIsBalancedAfterAFullRun() {
    FakeCuda fake;
    const unsigned tex = makeTexture(48, 32);
    {
      CudaGlCapture cap(fake.deps());
      QCOMPARE(attachUntilDecided(cap, tex, 48, 32), Status::Ok);
      for (int i = 0; i < 30; ++i) {
        std::shared_ptr<AVFrame> f = cap.capture();
        QVERIFY(f);
      }
      cap.detach(true);
    }
    deleteTexture(tex);
    waitForPool();
    const FakeCuda::Counters c = fake.counters();
    QCOMPARE(c.maps, 30);
    QCOMPARE(c.unmaps, 30);
    QCOMPARE(c.copies, 30);
    QCOMPARE(c.registrations, c.unregistrations);
    QCOMPARE(c.contextsCreated, c.contextsDestroyed);
    QCOMPARE(c.streamsCreated, c.streamsDestroyed);
    QCOMPARE(c.pushes, c.pops);
    QCOMPARE(FakeCuda::liveFrames(), 0);
    QString why;
    QVERIFY2(fake.balanced(&why), qPrintable(why));
  }
};

int main(int argc, char** argv) {
  fbtest::prepareProcess();
  QGuiApplication app(argc, argv);
  GlEnv env;
  if (!env.create()) {
#ifdef Q_OS_LINUX
    const bool required = qEnvironmentVariableIsSet("CI");  // GitHub Actions sets CI=true
#else
    const bool required = false;
#endif
    qWarning("No OpenGL 3.3 core context available (platform %s)", qPrintable(QGuiApplication::platformName()));
    return required ? 1 : 77;
  }
  g_env = &env;
  CudaGlCaptureTest t;
  const int rc = QTest::qExec(&t, argc, argv);
  g_env = nullptr;
  return rc;
}

#include "cuda_gl_capture_test.moc"
