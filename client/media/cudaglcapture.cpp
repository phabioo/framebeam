#include "cudaglcapture.h"

// cuda_shim.h before the FFmpeg CUDA headers: it only stops <libavutil/hwcontext_cuda.h> from including <cuda.h>.
#include "cuda_shim.h"
#include "cudadriver.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThreadPool>
#include <atomic>
#include <cstdint>
#include <mutex>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/pixfmt.h>
}

namespace framebeam {

namespace {

constexpr int kPoolFrames = 5;                  // mailbox 1 + queue 2 + encoding 1 + capturing 1 (ADR 0019)
constexpr qint64 kTimingIntervalMs = 10'000;

QString ffmpegError(int rc) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(rc, buf, sizeof buf);  // never av_err2str: a C compound literal, does not compile as C++ on MSVC
  return QString::fromUtf8(buf);
}

// AVHWDeviceContext.free of the device made by FfmpegCudaFramePool: FFmpeg did not create our context, so it never
// destroys it; we do, when the last frame or encoder holding the device is gone (on whatever thread that is). Goes
// through the never-unloaded driver table, so it also works from static teardown.
void freeCudaDevice(AVHWDeviceContext* device) {
  auto* cu = static_cast<AVCUDADeviceContext*>(device->hwctx);
  const cuda::Api& api = cuda::Driver::instance().api();
  if (cu && cu->cuda_ctx && api.cuCtxDestroy_v2) {
    api.cuCtxDestroy_v2(cu->cuda_ctx);
    cu->cuda_ctx = nullptr;
  }
}

class FfmpegCudaFramePool final : public CudaFramePool {
 public:
  ~FfmpegCudaFramePool() override {
    av_buffer_unref(&frames_);  // frames and encoders in flight keep their own references
    av_buffer_unref(&device_);
  }

  bool init(void* cuContext, QString* why) {
    device_ = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_CUDA);
    if (!device_) {
      *why = QStringLiteral("FFmpeg has no CUDA support");
      return false;
    }
    auto* device = reinterpret_cast<AVHWDeviceContext*>(device_->data);
    auto* cu = static_cast<AVCUDADeviceContext*>(device->hwctx);
    cu->cuda_ctx = static_cast<CUcontext>(cuContext);
    cu->stream = nullptr;  // the default stream: NVENC and FFmpeg's own copies use it
    const int rc = av_hwdevice_ctx_init(device_);
    if (rc < 0) {
      // `free` is not set yet and FFmpeg never destroys a context it did not allocate: the caller keeps ownership.
      *why = QStringLiteral("av_hwdevice_ctx_init: %1").arg(ffmpegError(rc));
      av_buffer_unref(&device_);
      return false;
    }
    device->free = &freeCudaDevice;  // from here on FFmpeg owns the context
    return true;
  }

  bool configure(int width, int height, int prealloc, QString* why) override {
    av_buffer_unref(&frames_);
    size_ = QSize();
    frames_ = av_hwframe_ctx_alloc(device_);
    if (!frames_) {
      *why = QStringLiteral("av_hwframe_ctx_alloc failed");
      return false;
    }
    auto* fc = reinterpret_cast<AVHWFramesContext*>(frames_->data);
    fc->format = AV_PIX_FMT_CUDA;
    fc->sw_format = AV_PIX_FMT_RGB0;  // bytes R,G,B,X: the byte order of a GL_RGBA8 texture
    fc->width = width;
    fc->height = height;
    fc->initial_pool_size = prealloc;
    const int rc = av_hwframe_ctx_init(frames_);
    if (rc < 0) {
      *why = QStringLiteral("av_hwframe_ctx_init: %1").arg(ffmpegError(rc));
      av_buffer_unref(&frames_);
      return false;
    }
    size_ = QSize(width, height);
    return true;
  }

  AVFrame* get(QString* why) override {
    if (!frames_) {
      *why = QStringLiteral("no frames context");
      return nullptr;
    }
    AVFrame* f = av_frame_alloc();
    if (!f) {
      *why = QStringLiteral("out of memory");
      return nullptr;
    }
    const int rc = av_hwframe_get_buffer(frames_, f, 0);  // pushes and pops the CUDA context itself
    if (rc < 0) {
      *why = QStringLiteral("av_hwframe_get_buffer: %1").arg(ffmpegError(rc));
      av_frame_free(&f);
      return nullptr;
    }
    return f;
  }

  QSize size() const override { return size_; }

 private:
  AVBufferRef* device_ = nullptr;
  AVBufferRef* frames_ = nullptr;
  QSize size_;
};

// Pushes a CUDA context for the scope. result() != 0: the push failed and nothing is popped.
class CtxScope {
 public:
  CtxScope(const cuda::Api* api, CUcontext ctx) : api_(api), result_(api->cuCtxPushCurrent_v2(ctx)) {}
  ~CtxScope() {
    if (result_ == cuda::kSuccess) {
      CUcontext popped = nullptr;
      api_->cuCtxPopCurrent_v2(&popped);
    }
  }
  CtxScope(const CtxScope&) = delete;
  CtxScope& operator=(const CtxScope&) = delete;
  CUresult result() const { return result_; }

 private:
  const cuda::Api* api_;
  CUresult result_;
};

// Releases what the context creation made, in reverse order: the stream (with its context pushed), then the pool,
// which owns the context (AVHWDeviceContext.free), or, without a pool, the context itself.
void releaseCreated(const cuda::Api* api, CUcontext ctx, CUstream stream, std::unique_ptr<CudaFramePool> pool) {
  if (stream && ctx) {
    CtxScope scope(api, ctx);
    if (scope.result() == cuda::kSuccess) {
      api->cuStreamDestroy_v2(stream);
    }
  }
  if (pool) {
    pool.reset();
  } else if (ctx) {
    api->cuCtxDestroy_v2(ctx);
  }
}

// Context, stream and frames made on a QThreadPool thread (cuCtxCreate takes tens of ms; never on the emulation thread).
struct CreateJob {
  std::mutex m;  // guards everything below `api` once the task runs
  bool done = false;
  bool ok = false;
  int error = 0;  // CUresult of the failing call, 0 for FFmpeg errors
  QString step, why;
  qint64 ms = 0;
  const cuda::Api* api = nullptr;
  CUdevice dev = 0;
  int w = 0, h = 0;
  std::function<std::unique_ptr<CudaFramePool>(void*, QString*)> makePool;
  CUcontext ctx = nullptr;
  CUstream stream = nullptr;
  std::unique_ptr<CudaFramePool> pool;

  ~CreateJob() { releaseCreated(api, ctx, stream, std::move(pool)); }  // whatever the capture did not take
};

void failJob(CreateJob& job, int error, const char* step, const QString& why) {
  const std::lock_guard<std::mutex> lock(job.m);
  job.error = error;
  job.step = QLatin1String(step);
  job.why = why;
  job.done = true;
}

void runCreateJob(CreateJob& job) {
  QElapsedTimer timer;
  timer.start();
  const cuda::Api& api = *job.api;
  CUcontext ctx = nullptr;
  CUstream stream = nullptr;
  CUresult r = api.cuCtxCreate_v2(&ctx, cuda::kCtxSchedBlockingSync, job.dev);  // also pushes it on this thread
  if (r != cuda::kSuccess) {
    failJob(job, r, "context", cuda::Driver::describe(r));
    return;
  }
  r = api.cuStreamCreate(&stream, cuda::kStreamNonBlocking);
  CUcontext popped = nullptr;
  api.cuCtxPopCurrent_v2(&popped);  // the context floats from here on and can be pushed from any thread
  if (r != cuda::kSuccess) {
    api.cuCtxDestroy_v2(ctx);
    failJob(job, r, "stream", cuda::Driver::describe(r));
    return;
  }
  QString why;
  std::unique_ptr<CudaFramePool> pool = job.makePool(ctx, &why);  // takes ownership of ctx only on success
  if (!pool) {
    releaseCreated(&api, ctx, stream, nullptr);
    failJob(job, 0, "frames context", why);
    return;
  }
  if (!pool->configure(job.w, job.h, kPoolFrames, &why)) {
    releaseCreated(&api, ctx, stream, std::move(pool));
    failJob(job, 0, "frames context", why);
    return;
  }
  const std::lock_guard<std::mutex> lock(job.m);
  job.ctx = ctx;
  job.stream = stream;
  job.pool = std::move(pool);
  job.ms = timer.elapsed();
  job.ok = true;
  job.done = true;
}

}  // namespace

std::unique_ptr<CudaFramePool> makeFfmpegCudaFramePool(void* cuContext, QString* why) {
  QString reason;
  if (!why) {
    why = &reason;
  }
  if (cuda::Driver::instance().state() != cuda::Driver::State::Ready) {
    *why = QStringLiteral("the CUDA driver is not loaded");  // also keeps FFmpeg's own loader (and its log noise) away
    return nullptr;
  }
  auto pool = std::make_unique<FfmpegCudaFramePool>();
  if (!pool->init(cuContext, why)) {
    return nullptr;
  }
  return pool;
}

// ---------------------------------------------------------------------------------------------------------------------

struct CudaGlCapture::Impl {
  explicit Impl(Deps d) : deps(std::move(d)), realMode(deps.api == nullptr), api(deps.api) {
    if (!deps.makePool) {
      deps.makePool = &makeFfmpegCudaFramePool;
    }
  }

  Deps deps;
  const bool realMode;
  const cuda::Api* api;  // real mode: set when the driver is Ready
  std::atomic<Status> status{Status::NotReady};
  mutable std::mutex mutex;  // guards reason, deviceName
  QString reason;
  QString deviceName;
  QString pendingDeviceName;
  bool vendorChecked = false;

  std::shared_ptr<CreateJob> job;
  CUcontext ctx = nullptr;
  CUstream stream = nullptr;
  std::unique_ptr<CudaFramePool> pool;
  CUgraphicsResource res = nullptr;
  unsigned texture = 0;
  int width = 0, height = 0;

  // 10 s timing line
  QElapsedTimer timingClock;
  int timedCaptures = 0;
  double sumMs = 0, maxMs = 0, mapMaxMs = 0, syncMaxMs = 0;

  Status unavailable(const QString& why) {
    {
      const std::lock_guard<std::mutex> lock(mutex);
      reason = why;
    }
    qCInfo(lcGpu).noquote() << QStringLiteral("GPU-direct capture unavailable: %1").arg(why);
    status = Status::Unavailable;
    return Status::Unavailable;
  }

  // `step` is one of context, stream, frames context, register, frame buffer, map, mapped array, copy, unmap,
  // synchronize. A fatal CUresult also marks the driver dead for the process.
  Status fail(const char* step, const QString& detail, int code = 0) {
    if (code != 0 && cuda::Driver::isFatal(code)) {
      cuda::Driver::instance().markDead(code, step);
    }
    {
      const std::lock_guard<std::mutex> lock(mutex);
      reason = QStringLiteral("%1: %2").arg(QLatin1String(step), detail);
    }
    qCWarning(lcGpu).noquote() << QStringLiteral("GPU-direct capture failed at %1: %2").arg(QLatin1String(step), detail);
    status = Status::Failed;
    return Status::Failed;
  }

  Status attach(unsigned tex, int w, int h);
  void detach(bool glCurrent);
  std::shared_ptr<AVFrame> capture();
  Status takeJob();
  void recordTiming(double totalMs, double mapMs, double syncMs);
};

CudaGlCapture::Status CudaGlCapture::Impl::takeJob() {
  {
    const std::lock_guard<std::mutex> lock(job->m);
    if (!job->done) {
      return Status::NotReady;
    }
  }
  // The pool thread has finished with the job; only this thread touches it now.
  if (!job->ok) {
    const int error = job->error;
    const QString step = job->step;
    const QString why = job->why;
    job.reset();
    return fail(step.toLatin1().constData(), why, error);
  }
  ctx = job->ctx;
  stream = job->stream;
  pool = std::move(job->pool);
  job->ctx = nullptr;
  job->stream = nullptr;
  const qint64 ms = job->ms;
  job.reset();
  QString name;
  {
    const std::lock_guard<std::mutex> lock(mutex);
    deviceName = pendingDeviceName;
    name = deviceName;
  }
  qCInfo(lcGpu).noquote() << QStringLiteral("CUDA-GL interop on \"%1\": context ready in %2 ms (off the emulation thread)")
                                 .arg(name)
                                 .arg(ms);
  return Status::Ok;
}

CudaGlCapture::Status CudaGlCapture::Impl::attach(unsigned tex, int w, int h) {
  const Status current = status.load();
  if (current == Status::Unavailable || current == Status::Failed) {
    return current;
  }
  QOpenGLContext* gl = QOpenGLContext::currentContext();
  if (!gl) {
    return unavailable(QStringLiteral("no current OpenGL context"));
  }

  if (realMode && !api) {
    if (!vendorChecked) {
      const auto* vendor = reinterpret_cast<const char*>(gl->functions()->glGetString(GL_VENDOR));
      const QString v = QString::fromLatin1(vendor ? vendor : "");
      if (!v.contains(QLatin1String("NVIDIA"), Qt::CaseInsensitive)) {
        return unavailable(QStringLiteral("the game's OpenGL context runs on '%1', not an NVIDIA GPU").arg(v));
      }
      vendorChecked = true;
    }
    cuda::Driver& driver = cuda::Driver::instance();
    driver.loadAsync();
    switch (driver.state()) {
      case cuda::Driver::State::Idle:
      case cuda::Driver::State::Loading:
        return Status::NotReady;
      case cuda::Driver::State::Unavailable:
      case cuda::Driver::State::Dead:
        return unavailable(driver.reason());
      case cuda::Driver::State::Ready:
        api = &driver.api();
        break;
    }
  }

  if (res) {  // attached already: same texture is a no-op, a different one replaces the registration
    if (tex == texture && w == width && h == height) {
      return Status::Ok;
    }
    detach(true);
  }

  // Device and context, once. cuGLGetDevices needs the GL context current, so it runs here; the creation does not.
  if (!job && !ctx) {
    unsigned count = 0;
    CUdevice devices[4] = {};
    const CUresult r = api->cuGLGetDevices_v2(&count, devices, 4, cuda::kGlDeviceListAll);
    if (r != cuda::kSuccess || count == 0) {
      if (r != cuda::kSuccess && cuda::Driver::isFatal(r)) {
        cuda::Driver::instance().markDead(r, "device query");
      }
      return unavailable(QStringLiteral("the OpenGL context is not on a CUDA device (%1)")
                             .arg(r != cuda::kSuccess ? cuda::Driver::describe(r) : QStringLiteral("no device")));
    }
    if (count > 1) {
      qCInfo(lcGpu).noquote() << QStringLiteral("GPU-direct: the OpenGL context spans %1 CUDA devices; using the first").arg(count);
    }
    char name[128] = {};
    api->cuDeviceGetName(name, static_cast<int>(sizeof name), devices[0]);
    pendingDeviceName = QString::fromUtf8(name);

    job = std::make_shared<CreateJob>();
    job->api = api;
    job->dev = devices[0];
    job->w = w;
    job->h = h;
    job->makePool = deps.makePool;
    QThreadPool::globalInstance()->start([j = job] { runCreateJob(*j); });
    return Status::NotReady;
  }
  if (job) {
    const Status st = takeJob();
    if (st != Status::Ok) {
      return st;
    }
  }

  if (pool->size() != QSize(w, h)) {  // an encode-size change; never per frame
    QString why;
    if (!pool->configure(w, h, kPoolFrames, &why)) {
      return fail("frames context", why);
    }
  }

  CtxScope scope(api, ctx);
  if (scope.result() != cuda::kSuccess) {
    return fail("context", cuda::Driver::describe(scope.result()), scope.result());
  }
  CUgraphicsResource registered = nullptr;
  const CUresult r = api->cuGraphicsGLRegisterImage(&registered, tex, cuda::kGlTexture2D, cuda::kRegisterReadOnly);
  if (r != cuda::kSuccess) {
    return fail("register", cuda::Driver::describe(r), r);
  }
  res = registered;
  texture = tex;
  width = w;
  height = h;
  status = Status::Ok;
  return Status::Ok;
}

void CudaGlCapture::Impl::detach(bool glCurrent) {
  if (!res) {
    return;
  }
  if (glCurrent && QOpenGLContext::currentContext() && api && ctx) {
    CtxScope scope(api, ctx);
    if (scope.result() == cuda::kSuccess) {
      const CUresult r = api->cuGraphicsUnregisterResource(res);
      if (r != cuda::kSuccess) {
        qCWarning(lcGpu).noquote() << QStringLiteral("GPU-direct: unregistering the texture failed: %1").arg(cuda::Driver::describe(r));
        if (cuda::Driver::isFatal(r)) {
          cuda::Driver::instance().markDead(r, "unregister");
        }
      }
    } else {
      qCWarning(lcGpu).noquote() << QStringLiteral("GPU-direct: the CUDA context could not be made current to unregister: %1")
                                        .arg(cuda::Driver::describe(scope.result()));
    }
  } else {
    // Unregistering needs the GL context. A registration that cannot be released properly is leaked (a few bytes of
    // driver state, freed with the CUDA context), never released without GL.
    qCWarning(lcGpu).noquote() << QStringLiteral("GPU-direct: GL context lost; leaking the CUDA registration");
  }
  res = nullptr;
  texture = 0;
}

void CudaGlCapture::Impl::recordTiming(double totalMs, double mapMs, double syncMs) {
  if (!timingClock.isValid()) {
    timingClock.start();
  }
  ++timedCaptures;
  sumMs += totalMs;
  maxMs = qMax(maxMs, totalMs);
  mapMaxMs = qMax(mapMaxMs, mapMs);
  syncMaxMs = qMax(syncMaxMs, syncMs);
  if (timingClock.elapsed() >= kTimingIntervalMs) {
    const auto fmt = [](double v) { return QString::number(v, 'f', 2); };
    qCInfo(lcGpu).noquote() << QStringLiteral("GPU copy timing (10 s): %1 captures, mean %2 ms, max %3 ms (map max %4 ms, sync max %5 ms)")
                                   .arg(timedCaptures)
                                   .arg(fmt(sumMs / timedCaptures), fmt(maxMs), fmt(mapMaxMs), fmt(syncMaxMs));
    timedCaptures = 0;
    sumMs = maxMs = mapMaxMs = syncMaxMs = 0;
    timingClock.restart();
  }
}

std::shared_ptr<AVFrame> CudaGlCapture::Impl::capture() {
  if (status.load() != Status::Ok) {
    return nullptr;
  }
  if (!res) {
    fail("register", QStringLiteral("capture without a registered texture"));
    return nullptr;
  }
  QElapsedTimer total;
  total.start();
  QString why;
  AVFrame* f = pool->get(&why);
  if (!f) {
    fail("frame buffer", why);
    return nullptr;
  }
  if (!f->data[0] || f->linesize[0] < width * 4) {
    const int pitch = f->linesize[0];
    av_frame_free(&f);
    fail("frame buffer", QStringLiteral("unusable frame (pitch %1 for %2 pixels)").arg(pitch).arg(width));
    return nullptr;
  }

  CUresult error = cuda::kSuccess;
  const char* step = nullptr;
  const auto note = [&](CUresult r, const char* name) {
    if (r != cuda::kSuccess && error == cuda::kSuccess) {
      error = r;
      step = name;
    }
  };
  double mapMs = 0, syncMs = 0;
  {
    CtxScope scope(api, ctx);
    note(scope.result(), "context");
    if (error == cuda::kSuccess) {
      CUgraphicsResource resource = res;
      const CUresult mapped = api->cuGraphicsMapResources(1, &resource, stream);  // orders the earlier GL work first
      mapMs = static_cast<double>(total.nsecsElapsed()) / 1e6;
      note(mapped, "map");
      if (mapped == cuda::kSuccess) {
        CUarray array = nullptr;
        CUresult r = api->cuGraphicsSubResourceGetMappedArray(&array, resource, 0, 0);  // fresh after every map
        note(r, "mapped array");
        if (r == cuda::kSuccess) {
          cuda::Memcpy2D copy{};
          copy.srcMemoryType = cuda::kMemoryTypeArray;
          copy.srcArray = array;
          copy.dstMemoryType = cuda::kMemoryTypeDevice;
          copy.dstDevice = static_cast<CUdeviceptr>(reinterpret_cast<uintptr_t>(f->data[0]));
          copy.dstPitch = static_cast<size_t>(f->linesize[0]);
          copy.WidthInBytes = static_cast<size_t>(width) * 4;
          copy.Height = static_cast<size_t>(height);
          note(api->cuMemcpy2DAsync_v2(&copy, stream), "copy");
        }
        // Always after a successful map, so nothing stays mapped or in flight whatever happened above.
        note(api->cuGraphicsUnmapResources(1, &resource, stream), "unmap");
        QElapsedTimer sync;
        sync.start();
        note(api->cuStreamSynchronize(stream), "synchronize");
        syncMs = static_cast<double>(sync.nsecsElapsed()) / 1e6;
      }
    }
  }  // pop
  if (error != cuda::kSuccess) {
    av_frame_free(&f);
    fail(step, cuda::Driver::describe(error), error);
    return nullptr;
  }
  recordTiming(static_cast<double>(total.nsecsElapsed()) / 1e6, mapMs, syncMs);
  return std::shared_ptr<AVFrame>(f, [](AVFrame* p) { av_frame_free(&p); });
}

// ---------------------------------------------------------------------------------------------------------------------

CudaGlCapture::CudaGlCapture() : CudaGlCapture(Deps{}) {}
CudaGlCapture::CudaGlCapture(Deps deps) : d_(std::make_unique<Impl>(std::move(deps))) {}

CudaGlCapture::~CudaGlCapture() {
  Impl& d = *d_;
  if (d.res) {
    qCWarning(lcGpu).noquote() << QStringLiteral("GPU-direct: destroyed while a texture is still registered; leaking the CUDA registration");
    d.res = nullptr;  // never unregistered without GL
  }
  if (d.job) {
    d.job.reset();  // the pool task finishes on its own; ~CreateJob frees what it made
    return;
  }
  if (d.api) {
    releaseCreated(d.api, d.ctx, d.stream, std::move(d.pool));
  }
}

CudaGlCapture::Status CudaGlCapture::attach(unsigned glTexture2D, int width, int height) {
  return d_->attach(glTexture2D, width, height);
}

void CudaGlCapture::detach(bool glCurrent) { d_->detach(glCurrent); }

std::shared_ptr<AVFrame> CudaGlCapture::capture() { return d_->capture(); }

CudaGlCapture::Status CudaGlCapture::status() const { return d_->status.load(); }

QString CudaGlCapture::reason() const {
  const std::lock_guard<std::mutex> lock(d_->mutex);
  return d_->reason;
}

QString CudaGlCapture::deviceName() const {
  const std::lock_guard<std::mutex> lock(d_->mutex);
  return d_->deviceName;
}

int cudaHealthCheck(AVBufferRef* framesCtx) {
  if (!framesCtx || !framesCtx->data) {
    return 0;
  }
  cuda::Driver& driver = cuda::Driver::instance();
  if (driver.state() != cuda::Driver::State::Ready) {
    return 0;
  }
  const auto* frames = reinterpret_cast<const AVHWFramesContext*>(framesCtx->data);
  if (frames->format != AV_PIX_FMT_CUDA || !frames->device_ctx || frames->device_ctx->type != AV_HWDEVICE_TYPE_CUDA) {
    return 0;
  }
  const auto* cu = static_cast<const AVCUDADeviceContext*>(frames->device_ctx->hwctx);
  if (!cu || !cu->cuda_ctx) {
    return 0;
  }
  const cuda::Api& api = driver.api();
  CtxScope scope(&api, cu->cuda_ctx);
  const CUresult r = scope.result() != cuda::kSuccess ? scope.result() : api.cuCtxSynchronize();
  if (r != cuda::kSuccess && cuda::Driver::isFatal(r)) {
    driver.markDead(r, "health check");
  }
  return r;
}

}  // namespace framebeam
