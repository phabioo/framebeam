// GPU-direct encoding (ADR 0019) on real hardware: the real CudaGlCapture (CUDA-GL interop) feeding the real h264_nvenc
// with CUDA frames, checked through the decoder. This needs an NVIDIA GPU whose driver serves the game's OpenGL context;
// it ends with 77 (SKIP) without a GL 3.3 context, without an NVIDIA GL_VENDOR or without a CUDA driver, also on CI.
//
// How to run: build, then start test_media_cuda_gl_hw (test_media_cuda_gl_hw.exe on Windows) directly from the build
// directory, or in a Remote Control session. ctest forces QT_QPA_PLATFORM=offscreen, which can work on Windows but is
// less reliable than the plain executable. It prints the context creation time and the capture timings.
//
// It answers: does CUDA-GL interop work with this platform's GL context and QOffscreenSurface (risk 1), is the byte
// order R,G,B (risk 3), is row 0 the image top, does NVENC deliver one packet per frame without stale frames, and may
// a frame outlive the GL context (design I2).
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QtTest>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <vector>

#include "cudadriver.h"
#include "cudaglcapture.h"
#include "processguard.h"
#include "videodecoder.h"
#include "videoencoder.h"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

using namespace framebeam;
using Status = CudaGlCapture::Status;

namespace {

struct GlEnv {
  QOffscreenSurface surface;
  std::unique_ptr<QOpenGLContext> context;

  bool create() {
    surface.create();
    if (!surface.isValid()) return false;
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRedBufferSize(8);
    fmt.setGreenBufferSize(8);
    fmt.setBlueBufferSize(8);
    fmt.setAlphaBufferSize(8);
    context = std::make_unique<QOpenGLContext>();
    context->setFormat(fmt);
    return context->create() && context->makeCurrent(&surface);
  }
};
GlEnv* g_env = nullptr;

QOpenGLExtraFunctions* gl() { return QOpenGLContext::currentContext()->extraFunctions(); }

struct Color {
  int r, g, b;
};
constexpr Color kTopLeft{220, 40, 40}, kTopRight{40, 220, 40}, kBottomLeft{40, 40, 220}, kBottomRight{220, 220, 40};
constexpr int kCounterBase = 10, kCounterStep = 7;  // the counter square's red value is 10 + 7 * frame number

// The texture is the image: GL row 0 is its top row. Four coloured quadrants and an 8x8 counter square at (4,4) whose
// red value is the frame number, green and blue stay mid-range so 4:2:0 and clipping do not distort it.
struct Target {
  GLuint tex = 0, fbo = 0;
  int w = 0, h = 0;
};

Target makeTarget(int w, int h) {
  QOpenGLExtraFunctions* f = gl();
  Target t;
  t.w = w;
  t.h = h;
  f->glGenTextures(1, &t.tex);
  f->glBindTexture(GL_TEXTURE_2D, t.tex);
  f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  f->glGenFramebuffers(1, &t.fbo);
  f->glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
  const GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
  f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    qFatal("framebuffer incomplete: 0x%x", status);
  }
  return t;
}

void freeTarget(Target& t) {
  QOpenGLExtraFunctions* f = gl();
  f->glDeleteFramebuffers(1, &t.fbo);
  f->glDeleteTextures(1, &t.tex);
  t = Target();
}

void fillRect(QOpenGLExtraFunctions* f, int x, int y, int w, int h, Color c) {
  f->glScissor(x, y, w, h);
  f->glClearColor(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
  f->glClear(GL_COLOR_BUFFER_BIT);
}

void drawPattern(const Target& t, int counter) {
  QOpenGLExtraFunctions* f = gl();
  f->glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  f->glViewport(0, 0, t.w, t.h);
  f->glEnable(GL_SCISSOR_TEST);
  const int hw = t.w / 2, hh = t.h / 2;
  fillRect(f, 0, 0, hw, hh, kTopLeft);  // GL row 0 = image top
  fillRect(f, hw, 0, t.w - hw, hh, kTopRight);
  fillRect(f, 0, hh, hw, t.h - hh, kBottomLeft);
  fillRect(f, hw, hh, t.w - hw, t.h - hh, kBottomRight);
  fillRect(f, 4, 4, 8, 8, Color{kCounterBase + kCounterStep * counter, 128, 128});
  f->glDisable(GL_SCISSOR_TEST);
  f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
  f->glFlush();  // capture()'s map orders the rest
}

// Sample points inside each quadrant, away from the counter square.
struct Sample {
  int x, y;
  Color expected;
};
std::vector<Sample> samples(int w, int h) {
  const int x = w * 5 / 32, y = h * 5 / 32;
  return {{x, y, kTopLeft}, {w - x, y, kTopRight}, {x, h - y, kBottomLeft}, {w - x, h - y, kBottomRight}};
}

bool within(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }

QString checkImage(const QImage& img, int w, int h, int tolerance, int counter) {
  if (img.size() != QSize(w, h)) return QStringLiteral("decoded %1x%2, expected %3x%4").arg(img.width()).arg(img.height()).arg(w).arg(h);
  for (const Sample& s : samples(w, h)) {
    const QRgb p = img.pixel(s.x, s.y);
    if (!within(qRed(p), s.expected.r, tolerance) || !within(qGreen(p), s.expected.g, tolerance) ||
        !within(qBlue(p), s.expected.b, tolerance)) {
      return QStringLiteral("(%1,%2) is %3,%4,%5, expected %6,%7,%8")
          .arg(s.x).arg(s.y).arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p)).arg(s.expected.r).arg(s.expected.g).arg(s.expected.b);
    }
  }
  if (counter >= 0) {
    const int want = kCounterBase + kCounterStep * counter;
    const int got = qRed(img.pixel(8, 8));
    if (!within(got, want, 8)) return QStringLiteral("frame counter reads %1, expected %2 (+-8): a stale or wrong frame").arg(got).arg(want);
  }
  return {};
}

// Exact check of the transferred CPU frame: bytes R,G,B,X per pixel, row 0 = image top.
QString checkTransferred(const AVFrame* cpu, int w, int h, int counter) {
  if (cpu->format != AV_PIX_FMT_RGB0 || cpu->width != w || cpu->height != h) return QStringLiteral("unexpected transfer result");
  for (const Sample& s : samples(w, h)) {
    const uint8_t* p = cpu->data[0] + static_cast<size_t>(s.y) * cpu->linesize[0] + static_cast<size_t>(s.x) * 4;
    if (p[0] != s.expected.r || p[1] != s.expected.g || p[2] != s.expected.b) {
      return QStringLiteral("(%1,%2) bytes are %3,%4,%5, expected R,G,B = %6,%7,%8 (top row first)")
          .arg(s.x).arg(s.y).arg(p[0]).arg(p[1]).arg(p[2]).arg(s.expected.r).arg(s.expected.g).arg(s.expected.b);
    }
  }
  const uint8_t* c = cpu->data[0] + static_cast<size_t>(8) * cpu->linesize[0] + 8 * 4;
  if (c[0] != kCounterBase + kCounterStep * counter || c[1] != 128 || c[2] != 128) return QStringLiteral("counter square bytes %1,%2,%3").arg(c[0]).arg(c[1]).arg(c[2]);
  return {};
}

Status attachUntilDecided(CudaGlCapture& cap, const Target& t, double* firstMs = nullptr, Status* first = nullptr) {
  QElapsedTimer timer;
  timer.start();
  Status st = cap.attach(t.tex, t.w, t.h);
  if (first) *first = st;
  if (firstMs) *firstMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
  while (st == Status::NotReady && timer.elapsed() < 2000) {
    QThread::msleep(2);
    st = cap.attach(t.tex, t.w, t.h);
  }
  return st;
}

}  // namespace

class CudaGlHwTest : public QObject {
  Q_OBJECT

  Target target_;
  std::unique_ptr<CudaGlCapture> capture_;
  VideoEncoder encoder_;
  VideoDecoder decoder_;
  double contextMs_ = 0;
  std::vector<double> captureMs_;

 private slots:
  void init() { QVERIFY(g_env->context->makeCurrent(&g_env->surface)); }

  // 1.-3. The chain texture -> CUDA frame -> CPU frame: context off the calling thread, byte order, orientation.
  void interopDeliversTheTextureAsACudaFrame() {
    target_ = makeTarget(256, 192);
    drawPattern(target_, 5);
    capture_ = std::make_unique<CudaGlCapture>();
    QElapsedTimer total;
    total.start();
    double firstMs = 0;
    Status first = Status::Ok;
    const Status st = attachUntilDecided(*capture_, target_, &firstMs, &first);
    contextMs_ = static_cast<double>(total.nsecsElapsed()) / 1e6;
    QVERIFY2(st == Status::Ok, qPrintable(QStringLiteral("attach: %1").arg(capture_->reason())));
    // The context is made on a pool thread: the first attach asks for it and returns, it does not wait for it.
    QCOMPARE(first, Status::NotReady);
    qInfo().noquote() << QStringLiteral("CUDA-GL interop on \"%1\": first attach %2 ms, ready after %3 ms")
                             .arg(capture_->deviceName()).arg(firstMs, 0, 'f', 2).arg(contextMs_, 0, 'f', 1);

    std::shared_ptr<AVFrame> frame = capture_->capture();
    QVERIFY2(frame, qPrintable(capture_->reason()));
    QCOMPARE(frame->format, int(AV_PIX_FMT_CUDA));
    QVERIFY(frame->hw_frames_ctx);
    const auto* fc = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
    QCOMPARE(int(fc->sw_format), int(AV_PIX_FMT_RGB0));
    QCOMPARE(fc->width, 256);
    QCOMPARE(fc->height, 192);

    AVFrame* cpu = av_frame_alloc();
    cpu->format = AV_PIX_FMT_RGB0;
    const int rc = av_hwframe_transfer_data(cpu, frame.get(), 0);
    QVERIFY2(rc >= 0, "av_hwframe_transfer_data failed");
    const QString problem = checkTransferred(cpu, 256, 192, 5);
    av_frame_free(&cpu);
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
  }

  // 4.-5. NVENC with CUDA frames: one packet per frame, no stale frames, decodes to the same picture.
  void nvencEncodesTheFramesWithoutDelayOrStaleness() {
    QVERIFY(capture_);
    std::shared_ptr<AVFrame> first = capture_->capture();
    QVERIFY(first);
    QString why;
    if (!encoder_.openGpu(first->hw_frames_ctx, 60, 4'000'000, &why)) {
      if (why.contains(QLatin1String("Function not implemented")) || why.contains(QLatin1String("external library"))) {
        QSKIP(qPrintable(QStringLiteral("h264_nvenc does not take CUDA frames here (driver too old?): %1").arg(why)));
      }
      QFAIL(qPrintable(QStringLiteral("openGpu: %1").arg(why)));
    }
    QVERIFY(encoder_.isGpuInput());
    QCOMPARE(encoder_.name(), QStringLiteral("h264_nvenc"));
    QVERIFY(encoder_.gpuFramesKey() == first->hw_frames_ctx->data);

    QVERIFY(decoder_.open());
    constexpr int kFrames = 30;
    captureMs_.clear();
    for (int n = 0; n < kFrames; ++n) {
      drawPattern(target_, n);
      QElapsedTimer t;
      t.start();
      std::shared_ptr<AVFrame> frame = capture_->capture();
      captureMs_.push_back(static_cast<double>(t.nsecsElapsed()) / 1e6);
      QVERIFY2(frame, qPrintable(QStringLiteral("capture %1: %2").arg(n).arg(capture_->reason())));
      std::vector<EncodedVideoPacket> packets;
      QVERIFY2(encoder_.encodeGpu(frame.get(), n, packets), qPrintable(QStringLiteral("encodeGpu %1").arg(n)));
      QVERIFY2(packets.size() == 1, qPrintable(QStringLiteral("frame %1 gave %2 packets (delay=0 expects 1)").arg(n).arg(packets.size())));
      if (n == 0) QVERIFY2(packets[0].keyframe, "the first packet must be a keyframe");
      QCOMPARE(packets[0].pts, int64_t(n));
      std::vector<QImage> images;
      QVERIFY2(decoder_.decode(packets[0].data.data(), packets[0].data.size(), images), qPrintable(QStringLiteral("decode %1").arg(n)));
      QVERIFY2(images.size() == 1, qPrintable(QStringLiteral("packet %1 decoded to %2 pictures").arg(n).arg(images.size())));
      const QString problem = checkImage(images[0], 256, 192, 40, n);
      QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("frame %1: %2").arg(n).arg(problem)));
    }
    const double mean = std::accumulate(captureMs_.begin(), captureMs_.end(), 0.0) / captureMs_.size();
    const double worst = *std::max_element(captureMs_.begin(), captureMs_.end());
    qInfo().noquote() << QStringLiteral("capture timing: mean %1 ms, max %2 ms over %3 frames").arg(mean, 0, 'f', 3).arg(worst, 0, 'f', 3).arg(kFrames);
  }

  // 6. A smaller encode size: new texture, new registration, new frames context, the encoder is reopened.
  void aSizeChangeNeedsANewFramesContextAndAReopen() {
    QVERIFY(capture_);
    std::shared_ptr<AVFrame> old = capture_->capture();
    QVERIFY(old);
    capture_->detach(true);
    freeTarget(target_);
    target_ = makeTarget(128, 96);
    drawPattern(target_, 3);
    QCOMPARE(attachUntilDecided(*capture_, target_), Status::Ok);
    std::shared_ptr<AVFrame> frame = capture_->capture();
    QVERIFY2(frame, qPrintable(capture_->reason()));
    QVERIFY(frame->hw_frames_ctx->data != old->hw_frames_ctx->data);  // a new frames context for the new size

    std::vector<EncodedVideoPacket> packets;
    QVERIFY2(!encoder_.encodeGpu(frame.get(), 100, packets), "a frame of another frames context must be refused");
    QString why;
    QVERIFY2(encoder_.openGpu(frame->hw_frames_ctx, 60, 4'000'000, &why), qPrintable(why));
    QVERIFY(encoder_.gpuFramesKey() == frame->hw_frames_ctx->data);
    QVERIFY(!encoder_.encodeGpu(old.get(), 101, packets));  // and the old size is refused now
    QVERIFY2(encoder_.encodeGpu(frame.get(), 102, packets), "encodeGpu after the reopen");
    QCOMPARE(packets.size(), size_t(1));
    QVERIFY(packets[0].keyframe);  // every open starts with an IDR
    VideoDecoder fresh;
    QVERIFY(fresh.open());
    std::vector<QImage> images;
    QVERIFY(fresh.decode(packets[0].data.data(), packets[0].data.size(), images));
    QCOMPARE(images.size(), size_t(1));
    const QString problem = checkImage(images[0], 128, 96, 40, 3);
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
  }

  // 7. design I2: a frame outlives the capture, the registration, the texture and the GL context.
  void aFrameOutlivesTheGlContext() {
    QVERIFY(capture_);
    drawPattern(target_, 9);
    std::shared_ptr<AVFrame> held = capture_->capture();
    QVERIFY(held);
    QVERIFY(encoder_.gpuFramesKey() == held->hw_frames_ctx->data);  // same frames context as the encoder's

    capture_->detach(true);  // unregister with GL current, before the texture goes
    freeTarget(target_);
    capture_.reset();
    g_env->context->doneCurrent();
    g_env->context.reset();  // the GL context is gone

    std::vector<EncodedVideoPacket> packets;
    QVERIFY2(encoder_.encodeGpu(held.get(), 200, packets), "encoding a held frame after the GL context is gone");
    QCOMPARE(packets.size(), size_t(1));
    held.reset();  // the last user of the CUDA context goes, on this thread
    encoder_.close();
    QVERIFY(encoder_.gpuFramesKey() == nullptr);
  }
};

int main(int argc, char** argv) {
  fbtest::prepareProcess();
  QGuiApplication app(argc, argv);
  GlEnv env;
  if (!env.create()) {
    qWarning("SKIP: no OpenGL 3.3 core context (platform %s)", qPrintable(QGuiApplication::platformName()));
    return 77;
  }
  const auto* vendorChars = reinterpret_cast<const char*>(env.context->functions()->glGetString(GL_VENDOR));
  const QString vendor = QString::fromLatin1(vendorChars ? vendorChars : "");
  if (!vendor.contains(QLatin1String("NVIDIA"), Qt::CaseInsensitive)) {
    qWarning().noquote() << "SKIP: the OpenGL context is not on an NVIDIA GPU (vendor" << vendor << ")";
    return 77;
  }
  if (!cuda::Driver::instance().loadBlocking()) {
    qWarning().noquote() << "SKIP: no usable CUDA driver:" << cuda::Driver::instance().reason();
    return 77;
  }
  g_env = &env;
  CudaGlHwTest t;
  const int rc = QTest::qExec(&t, argc, argv);
  g_env = nullptr;
  return rc;
}

#include "cuda_gl_hw_test.moc"
