// GPU-direct encoding (ADR 0019), the parts that need neither a GL context nor a GPU: the CUDA driver loader and its
// error table, the shim layout, the VideoEncoder helpers and the guards of the CUDA input path. Runs on every CI
// platform. The interop call sequence is covered by media_cuda_gl_capture, real hardware by media_cuda_gl_hw.
#include <QByteArray>
#include <QtTest>

#include "cuda_shim.h"  // before any FFmpeg CUDA header
#include "cudadriver.h"
#include "cudaglcapture.h"
#include "processguard.h"
#include "videoencoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

using namespace framebeam;

namespace {

// Sets or removes an environment variable for one scope.
class EnvGuard {
 public:
  EnvGuard(const char* name, const char* value) : name_(name), had_(qEnvironmentVariableIsSet(name)), old_(qgetenv(name)) {
    if (value) {
      qputenv(name, value);
    } else {
      qunsetenv(name);
    }
  }
  ~EnvGuard() {
    if (had_) {
      qputenv(name_, old_);
    } else {
      qunsetenv(name_);
    }
  }

 private:
  const char* name_;
  bool had_;
  QByteArray old_;
};

// A zeroed AVHWFramesContext with the given fields: enough for the guards, never reaches NVENC.
AVBufferRef* fakeFramesContext(AVPixelFormat format, AVPixelFormat swFormat, int width, int height) {
  AVBufferRef* ref = av_buffer_allocz(sizeof(AVHWFramesContext));
  auto* fc = reinterpret_cast<AVHWFramesContext*>(ref->data);
  fc->format = format;
  fc->sw_format = swFormat;
  fc->width = width;
  fc->height = height;
  return ref;
}

}  // namespace

class CudaTest : public QObject {
  Q_OBJECT
 private slots:
  void resolveFindsNoSuchLibrary() {
    cuda::Api api;
    QString why;
    QVERIFY(!cuda::Driver::resolve("framebeam-no-such-cuda", &api, &why));
    QVERIFY2(why.contains(QLatin1String("not found")), qPrintable(why));
  }

  void resolveNamesTheFirstMissingSymbol() {
#ifdef _WIN32
    const char* library = "kernel32.dll";
#else
    const char* library = "libc.so.6";
#endif
    cuda::Api api;
    QString why;
    QVERIFY(!cuda::Driver::resolve(library, &api, &why));
    QCOMPARE(why, QStringLiteral("cuInit missing"));
    QVERIFY(api.cuInit == nullptr);  // nothing is half-filled
  }

  void fatalCodes() {
    for (int code : {226, 700, 702, 714, 715, 716, 717, 718, 719, 721, 810, 911}) {
      QVERIFY2(cuda::Driver::isFatal(code), qPrintable(QString::number(code)));
    }
    for (int code : {0, 1, 2, 4, 100, 201, 205, 211, 219, 304, 600, 999, 12345}) {
      QVERIFY2(!cuda::Driver::isFatal(code), qPrintable(QString::number(code)));
    }
  }

  void describeNamesTheCodes() {
    QCOMPARE(cuda::Driver::describe(700), QStringLiteral("CUDA_ERROR_ILLEGAL_ADDRESS (700)"));
    QCOMPARE(cuda::Driver::describe(0), QStringLiteral("CUDA_SUCCESS (0)"));
    QCOMPARE(cuda::Driver::describe(219), QStringLiteral("CUDA_ERROR_INVALID_GRAPHICS_CONTEXT (219)"));
    QCOMPARE(cuda::Driver::describe(34), QStringLiteral("CUDA_ERROR_STUB_LIBRARY (34)"));
    QCOMPARE(cuda::Driver::describe(12345), QStringLiteral("CUDA error 12345"));
  }

  void shimLayout() {
    using cuda::Memcpy2D;
    QCOMPARE(sizeof(Memcpy2D), size_t(128));
    QCOMPARE(offsetof(Memcpy2D, srcMemoryType), size_t(16));
    QCOMPARE(offsetof(Memcpy2D, srcHost), size_t(24));
    QCOMPARE(offsetof(Memcpy2D, srcDevice), size_t(32));
    QCOMPARE(offsetof(Memcpy2D, srcArray), size_t(40));
    QCOMPARE(offsetof(Memcpy2D, srcPitch), size_t(48));
    QCOMPARE(offsetof(Memcpy2D, dstXInBytes), size_t(56));
    QCOMPARE(offsetof(Memcpy2D, dstMemoryType), size_t(72));
    QCOMPARE(offsetof(Memcpy2D, dstHost), size_t(80));
    QCOMPARE(offsetof(Memcpy2D, dstDevice), size_t(88));
    QCOMPARE(offsetof(Memcpy2D, dstArray), size_t(96));
    QCOMPARE(offsetof(Memcpy2D, dstPitch), size_t(104));
    QCOMPARE(offsetof(Memcpy2D, WidthInBytes), size_t(112));
    QCOMPARE(offsetof(Memcpy2D, Height), size_t(120));
    int symbols = 0;
#define FB_COUNT_SYMBOL(name, params) ++symbols;
    FB_CUDA_SYMBOLS(FB_COUNT_SYMBOL)
#undef FB_COUNT_SYMBOL
    QCOMPARE(symbols, 18);
    QCOMPARE(sizeof(cuda::Api), 18 * sizeof(void*));
  }

  void driverIsUnavailableWithoutNvidia() {
    cuda::Driver& driver = cuda::Driver::instance();
    if (driver.loadBlocking()) {
      QSKIP("a CUDA driver is present on this machine");
    }
    QCOMPARE(driver.state(), cuda::Driver::State::Unavailable);
    QVERIFY(!driver.reason().isEmpty());
    qInfo().noquote() << "driver unavailable:" << driver.reason();
    driver.loadAsync();  // later calls change nothing
    QVERIFY(!driver.loadBlocking());
    QCOMPARE(driver.state(), cuda::Driver::State::Unavailable);
  }

  void markDeadIsStickyUntilReset() {
    cuda::Driver& driver = cuda::Driver::instance();
    driver.loadBlocking();
    const cuda::Driver::State before = driver.state();
    driver.markDead(700, "map");
    QCOMPARE(driver.state(), cuda::Driver::State::Dead);
    QVERIFY2(driver.reason().contains(QLatin1String("CUDA_ERROR_ILLEGAL_ADDRESS (700) at map")), qPrintable(driver.reason()));
    driver.markDead(719, "copy");  // logged once, the first reason stays
    QVERIFY(driver.reason().contains(QLatin1String("700")));
    QVERIFY(!driver.loadBlocking());
    driver.resetForTest();
    QCOMPARE(driver.state(), before);
    driver.resetForTest();  // no-op outside Dead
    QCOMPARE(driver.state(), before);
  }

  void effectiveOrderDropsNvencWhenCudaIsDead() {
    QCOMPARE(VideoEncoder::effectiveOrder({}, false), VideoEncoder::preferredEncoders());
    const QStringList without = VideoEncoder::effectiveOrder({}, true);
    QVERIFY(!without.contains(QLatin1String("h264_nvenc")));
    QVERIFY(!without.isEmpty());
    QCOMPARE(VideoEncoder::effectiveOrder({QStringLiteral("h264_nvenc"), QStringLiteral("libx264")}, true),
             QStringList{QStringLiteral("libx264")});
    // Never empty: a forced h264_nvenc stays and simply fails to open.
    QCOMPARE(VideoEncoder::effectiveOrder({QStringLiteral("h264_nvenc")}, true), QStringList{QStringLiteral("h264_nvenc")});
    QCOMPARE(VideoEncoder::effectiveOrder({QStringLiteral("h264_nvenc")}, false), QStringList{QStringLiteral("h264_nvenc")});
  }

  void gpuInputConfiguredFollowsTheEnvironment() {
    {
      EnvGuard disable("FRAMEBEAM_DISABLE_GPU_ENCODE", nullptr);
      EnvGuard forced("FRAMEBEAM_H264_ENCODER", nullptr);
      QString why;
      QVERIFY(VideoEncoder::gpuInputConfigured(&why));
      QVERIFY(VideoEncoder::gpuInputConfigured());
    }
    {
      EnvGuard disable("FRAMEBEAM_DISABLE_GPU_ENCODE", "1");
      EnvGuard forced("FRAMEBEAM_H264_ENCODER", nullptr);
      QString why;
      QVERIFY(!VideoEncoder::gpuInputConfigured(&why));
      QCOMPARE(why, QStringLiteral("FRAMEBEAM_DISABLE_GPU_ENCODE=1"));
      QVERIFY(!VideoEncoder::gpuInputConfigured());  // why is optional
    }
    {
      EnvGuard disable("FRAMEBEAM_DISABLE_GPU_ENCODE", "0");  // only "1" is the kill switch
      EnvGuard forced("FRAMEBEAM_H264_ENCODER", nullptr);
      QVERIFY(VideoEncoder::gpuInputConfigured());
    }
    for (const char* encoder : {"libx264", "h264_nvenc"}) {  // even the NVENC encoder keeps the readback path
      EnvGuard disable("FRAMEBEAM_DISABLE_GPU_ENCODE", nullptr);
      EnvGuard forced("FRAMEBEAM_H264_ENCODER", encoder);
      QString why;
      QVERIFY(!VideoEncoder::gpuInputConfigured(&why));
      QCOMPARE(why, QStringLiteral("FRAMEBEAM_H264_ENCODER=%1 forces the readback path").arg(QLatin1String(encoder)));
    }
  }

  void cudaInputSupportIsReported() {
    const bool supported = VideoEncoder::cudaInputSupported();
    const bool compiledIn = avcodec_find_encoder_by_name("h264_nvenc") != nullptr;
    qInfo().noquote() << "h264_nvenc compiled in:" << compiledIn << "| CUDA input supported:" << supported;
    if (!compiledIn) {
      QVERIFY(!supported);
    }
  }

  void openGpuRejectsAnythingButCudaRgb0Frames() {
    VideoEncoder enc;
    QString why;
    QVERIFY(!enc.openGpu(nullptr, 60, 2'000'000, &why));
    QCOMPARE(why, QStringLiteral("not a CUDA RGB0 frames context"));
    QVERIFY(!enc.openGpu(nullptr, 60, 2'000'000));  // why is optional
    QVERIFY(!enc.isOpen());
    QVERIFY(!enc.isGpuInput());
    QVERIFY(enc.gpuFramesKey() == nullptr);

    struct Case {
      AVPixelFormat format, sw;
      int w, h;
      const char* what;
    };
    const Case cases[] = {
        {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P, 0, 0, "zeroed"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_BGR0, 256, 192, "wrong byte order"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_NV12, 256, 192, "wrong sw format"},
        {AV_PIX_FMT_VAAPI, AV_PIX_FMT_RGB0, 256, 192, "not CUDA"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 255, 192, "odd width"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 256, 191, "odd height"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 8, 192, "too narrow"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 256, 8, "too low"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 8194, 192, "too wide"},
        {AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 256, 8194, "too high"},
    };
    for (const Case& c : cases) {
      AVBufferRef* ref = fakeFramesContext(c.format, c.sw, c.w, c.h);
      why.clear();
      QVERIFY2(!enc.openGpu(ref, 60, 2'000'000, &why), c.what);
      QVERIFY2(!why.isEmpty(), c.what);
      QVERIFY2(!enc.isOpen(), c.what);
      av_buffer_unref(&ref);
    }
  }

  void encodeGpuNeedsAnOpenGpuEncoder() {
    VideoEncoder enc;
    std::vector<EncodedVideoPacket> out;
    AVFrame* frame = av_frame_alloc();
    QVERIFY(!enc.encodeGpu(frame, 0, out));
    QVERIFY(!enc.encodeGpu(nullptr, 0, out));
    QVERIFY(out.empty());
    av_frame_free(&frame);
  }

  void framePoolNeedsTheLoadedDriver() {
    if (cuda::Driver::instance().state() == cuda::Driver::State::Ready) {
      QSKIP("a CUDA driver is present on this machine");
    }
    QString why;
    QVERIFY(!makeFfmpegCudaFramePool(nullptr, &why));  // refused before FFmpeg's own loader logs at error level
    QVERIFY(!why.isEmpty());
    QVERIFY(!makeFfmpegCudaFramePool(nullptr, nullptr));
  }

  void healthCheckSkipsWhatItCannotCheck() {
    QCOMPARE(cudaHealthCheck(nullptr), 0);
    AVBufferRef* zeroed = av_buffer_allocz(sizeof(AVHWFramesContext));
    QCOMPARE(cudaHealthCheck(zeroed), 0);
    av_buffer_unref(&zeroed);
    AVBufferRef* cudaFrames = fakeFramesContext(AV_PIX_FMT_CUDA, AV_PIX_FMT_RGB0, 256, 192);  // no device context
    QCOMPARE(cudaHealthCheck(cudaFrames), 0);
    av_buffer_unref(&cudaFrames);
  }
};

FB_TEST_MAIN(CudaTest)

#include "cuda_test.moc"
