#pragma once
// Fake CUDA driver API and a host-memory frame pool for the tests of GPU-direct encoding (ADR 0019). Header-only and
// self-contained (a test that uses it links framebeam_media and Qt6::Gui): the real CudaGlCapture runs against it on
// any machine with a GL context, e.g. Mesa llvmpipe under xvfb.
//
// FakeCuda fills a cuda::Api with static functions over one global instance (one FakeCuda at a time) and enforces the
// interop rules the real driver relies on. A broken rule is recorded in violations() and answered with the error the
// driver would give:
//   - GL current on the calling thread: cuGLGetDevices_v2, register, map, unmap, unregister;
//   - our context current (pushed) on the calling thread: everything but cuInit/cuGLGetDevices/context management;
//   - the texture is a GL_TEXTURE_2D with internal format GL_RGBA8 when registered, still a texture when unregistered;
//   - one registration per texture; map only when unmapped; the mapped array and the copy only while mapped (a stale
//     array from an earlier map is refused); unmap before unregister;
//   - pushes and pops per thread are balanced (a pop on an empty stack is a violation).
// The copy reads the texture through a temporary FBO + glReadPixels at map time and writes the rows to dstDevice with
// dstPitch, so row order, byte order and pitch bugs show in the frame.
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSize>
#include <QString>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "cuda_shim.h"
#include "cudaglcapture.h"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
}

namespace fbtest {

class FakeCuda {
 public:
  struct Counters {
    int inits = 0, deviceQueries = 0;
    int contextsCreated = 0, contextsDestroyed = 0;
    int streamsCreated = 0, streamsDestroyed = 0;
    int registrations = 0, unregistrations = 0;
    int maps = 0, unmaps = 0;
    int pushes = 0, pops = 0;  // cuCtxCreate_v2 pushes too
    int copies = 0, syncs = 0;
  };

  FakeCuda() {
    if (s_instance != nullptr) {
      qFatal("only one FakeCuda at a time");
    }
#define FB_FAKE_CUDA_BIND(name, params) api_.name = &FakeCuda::fake_##name;
    FB_CUDA_SYMBOLS(FB_FAKE_CUDA_BIND)
#undef FB_FAKE_CUDA_BIND
    s_instance = this;
  }
  ~FakeCuda() {
    s_instance = nullptr;
    for (Array* a : arrays_) delete a;
    for (Stream* st : streams_) delete st;
    for (Ctx* c : contexts_) delete c;
  }
  FakeCuda(const FakeCuda&) = delete;
  FakeCuda& operator=(const FakeCuda&) = delete;

  const framebeam::cuda::Api* api() const { return &api_; }

  // Deps for CudaGlCapture / GpuEncodeBridge: this API and HostFramePool as the frame pool.
  framebeam::CudaGlCapture::Deps deps();

  // The next `times` calls of `symbol` (a cuda::Api member name, e.g. "cuGraphicsMapResources") return `code`
  // without doing anything.
  void failNext(const char* symbol, int code, int times = 1) {
    const std::lock_guard<std::mutex> lock(m_);
    injected_[symbol] = {code, times};
  }
  void setContextCreateDelayMs(int ms) { contextDelayMs_ = ms; }
  void setGlDeviceCount(unsigned n) { glDevices_ = n; }
  // The pool factory fails (nullptr, why) / HostFramePool::configure fails with `why`; empty: works again.
  void setPoolFailure(const QString& why) {
    const std::lock_guard<std::mutex> lock(m_);
    poolFailure_ = why;
  }
  void setConfigureFailure(const QString& why) {
    const std::lock_guard<std::mutex> lock(m_);
    configureFailure_ = why;
  }

  Counters counters() const {
    const std::lock_guard<std::mutex> lock(m_);
    return c_;
  }
  std::vector<std::thread::id> contextCreateThreads() const {
    const std::lock_guard<std::mutex> lock(m_);
    return createThreads_;
  }
  std::vector<QSize> configureCalls() const {  // HostFramePool::configure sizes, in order
    const std::lock_guard<std::mutex> lock(m_);
    return configures_;
  }
  QStringList violations() const {
    const std::lock_guard<std::mutex> lock(m_);
    return violations_;
  }
  unsigned lastRegisterFlags() const { return lastRegisterFlags_; }
  unsigned lastContextFlags() const { return lastContextFlags_; }
  unsigned lastStreamFlags() const { return lastStreamFlags_; }
  int liveContexts() const {
    const std::lock_guard<std::mutex> lock(m_);
    return static_cast<int>(contexts_.size());
  }
  int mappedNow() const {
    const std::lock_guard<std::mutex> lock(m_);
    int n = 0;
    for (const auto& r : resources_) n += r.second->mapped ? 1 : 0;
    return n;
  }
  // Every pair of counters matches, nothing is mapped or registered or alive, no rule was broken and the calling
  // thread's context stack is empty. leakedRegistrationsOk: registrations may outlive the test (detach(false)).
  bool balanced(QString* why = nullptr, bool leakedRegistrationsOk = false) const {
    const std::lock_guard<std::mutex> lock(m_);
    QStringList problems;
    const auto pair = [&problems](const char* what, int a, int b) {
      if (a != b) problems << QStringLiteral("%1: %2 vs %3").arg(QLatin1String(what)).arg(a).arg(b);
    };
    pair("contexts", c_.contextsCreated, c_.contextsDestroyed);
    pair("streams", c_.streamsCreated, c_.streamsDestroyed);
    if (!leakedRegistrationsOk) pair("registrations", c_.registrations, c_.unregistrations);
    pair("maps", c_.maps, c_.unmaps);
    pair("pushes", c_.pushes, c_.pops);
    if (!contexts_.empty()) problems << QStringLiteral("%1 contexts alive").arg(contexts_.size());
    if (!streams_.empty()) problems << QStringLiteral("%1 streams alive").arg(streams_.size());
    if (!leakedRegistrationsOk && !resources_.empty()) problems << QStringLiteral("%1 resources registered").arg(resources_.size());
    if (!stack().empty()) problems << QStringLiteral("the calling thread still has %1 contexts pushed").arg(stack().size());
    if (!violations_.isEmpty()) problems << QStringLiteral("rule violations: ") + violations_.join(QStringLiteral("; "));
    if (why) *why = problems.join(QStringLiteral("; "));
    return problems.isEmpty();
  }

  static int liveFrames() { return s_liveFrames.load(); }
  static void frameCreated() { ++s_liveFrames; }
  static void frameReleased() { --s_liveFrames; }

 private:
  friend class HostFramePool;

  struct Ctx {};
  struct Stream {
    Ctx* ctx;
  };
  struct Array {};
  struct Res {
    unsigned tex = 0;
    int w = 0, h = 0;
    Ctx* ctx = nullptr;
    bool mapped = false;
    Array* array = nullptr;  // valid while mapped
    std::vector<uint8_t> snapshot;
  };

  static constexpr unsigned kGlRgba8 = 0x8058, kTexWidth = 0x1000, kTexHeight = 0x1001, kTexInternalFormat = 0x1003;

  static FakeCuda* self() { return s_instance; }
  static std::vector<CUcontext>& stack() {
    thread_local std::vector<CUcontext> s;
    return s;
  }
  static CUcontext top() { return stack().empty() ? nullptr : stack().back(); }

  void violate(const QString& what) {
    const std::lock_guard<std::mutex> lock(m_);
    violations_ << what;
  }
  int inject(const char* symbol) {
    const std::lock_guard<std::mutex> lock(m_);
    const auto it = injected_.find(symbol);
    if (it == injected_.end() || it->second.second <= 0) return 0;
    --it->second.second;
    return it->second.first;
  }
  // GL current on this thread, else the driver's error for a call without the GL context.
  int needGl(const char* fn) {
    if (QOpenGLContext::currentContext() != nullptr) return 0;
    violate(QStringLiteral("%1 without a current GL context").arg(QLatin1String(fn)));
    return 219;  // CUDA_ERROR_INVALID_GRAPHICS_CONTEXT
  }
  int needCtx(const char* fn, CUcontext expected) {
    if (top() != nullptr && (expected == nullptr || top() == expected)) return 0;
    violate(QStringLiteral("%1 without the right CUDA context pushed").arg(QLatin1String(fn)));
    return 201;  // CUDA_ERROR_INVALID_CONTEXT
  }

  static std::vector<uint8_t> readTexture(unsigned tex, int w, int h) {
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    GLint prevRead = 0, prevDraw = 0, prevAlign = 4, prevPackBuffer = 0;
    f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    f->glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
    f->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPackBuffer);
    GLuint fbo = 0;
    f->glGenFramebuffers(1, &fbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    if (f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
      f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
      f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());  // row 0 = GL row 0 = image top
    }
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevRead));
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(prevPackBuffer));
    f->glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
    f->glDeleteFramebuffers(1, &fbo);
    return px;
  }

  // ---- the fake driver -------------------------------------------------------------------------------------------
  static CUresult FB_CUDAAPI fake_cuInit(unsigned) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuInit")) return e;
    const std::lock_guard<std::mutex> lock(f->m_);
    ++f->c_.inits;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuDriverGetVersion(int* version) {
    if (version) *version = 12080;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuDeviceGetName(char* name, int len, CUdevice) {
    if (name && len > 0) std::snprintf(name, static_cast<size_t>(len), "Fake NVIDIA GPU");
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuGLGetDevices_v2(unsigned* count, CUdevice* devices, unsigned deviceCount, int) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuGLGetDevices_v2")) return e;
    if (int e = f->needGl("cuGLGetDevices_v2")) return e;
    {
      const std::lock_guard<std::mutex> lock(f->m_);
      ++f->c_.deviceQueries;
    }
    const unsigned n = f->glDevices_;
    for (unsigned i = 0; i < n && i < deviceCount; ++i) devices[i] = static_cast<CUdevice>(i);
    *count = n < deviceCount ? n : deviceCount;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuCtxCreate_v2(CUcontext* ctx, unsigned flags, CUdevice) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuCtxCreate_v2")) return e;
    const int delay = f->contextDelayMs_;
    if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    auto* c = new Ctx;
    {
      const std::lock_guard<std::mutex> lock(f->m_);
      f->contexts_.insert(c);
      f->createThreads_.push_back(std::this_thread::get_id());
      f->lastContextFlags_ = flags;
      ++f->c_.contextsCreated;
      ++f->c_.pushes;  // creating a context makes it current on the creating thread
    }
    stack().push_back(reinterpret_cast<CUcontext>(c));
    *ctx = reinterpret_cast<CUcontext>(c);
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuCtxDestroy_v2(CUcontext ctx) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuCtxDestroy_v2")) return e;
    auto* c = reinterpret_cast<Ctx*>(ctx);
    for (CUcontext pushed : stack()) {
      if (pushed == ctx) {
        f->violate(QStringLiteral("cuCtxDestroy_v2 of a context pushed on this thread"));
      }
    }
    const std::lock_guard<std::mutex> lock(f->m_);
    if (f->contexts_.erase(c) == 0) {
      f->violations_ << QStringLiteral("cuCtxDestroy_v2 of an unknown or destroyed context");
      return 201;
    }
    ++f->c_.contextsDestroyed;
    delete c;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuCtxPushCurrent_v2(CUcontext ctx) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuCtxPushCurrent_v2")) return e;
    {
      const std::lock_guard<std::mutex> lock(f->m_);
      if (f->contexts_.count(reinterpret_cast<Ctx*>(ctx)) == 0) {
        f->violations_ << QStringLiteral("cuCtxPushCurrent_v2 of an unknown or destroyed context");
        return 201;
      }
      ++f->c_.pushes;
    }
    stack().push_back(ctx);
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuCtxPopCurrent_v2(CUcontext* ctx) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuCtxPopCurrent_v2")) return e;
    if (stack().empty()) {
      f->violate(QStringLiteral("cuCtxPopCurrent_v2 with nothing pushed"));
      return 201;
    }
    if (ctx) *ctx = stack().back();
    stack().pop_back();
    const std::lock_guard<std::mutex> lock(f->m_);
    ++f->c_.pops;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuCtxSynchronize() {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuCtxSynchronize")) return e;
    return f->needCtx("cuCtxSynchronize", nullptr);
  }
  static CUresult FB_CUDAAPI fake_cuStreamCreate(CUstream* stream, unsigned flags) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuStreamCreate")) return e;
    if (int e = f->needCtx("cuStreamCreate", nullptr)) return e;
    auto* s = new Stream{reinterpret_cast<Ctx*>(top())};
    const std::lock_guard<std::mutex> lock(f->m_);
    f->streams_.insert(s);
    f->lastStreamFlags_ = flags;
    ++f->c_.streamsCreated;
    *stream = reinterpret_cast<CUstream>(s);
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuStreamDestroy_v2(CUstream stream) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuStreamDestroy_v2")) return e;
    auto* s = reinterpret_cast<Stream*>(stream);
    if (int e = f->needCtx("cuStreamDestroy_v2", f->streamCtx(s))) return e;
    const std::lock_guard<std::mutex> lock(f->m_);
    if (f->streams_.erase(s) == 0) {
      f->violations_ << QStringLiteral("cuStreamDestroy_v2 of an unknown or destroyed stream");
      return 400;
    }
    ++f->c_.streamsDestroyed;
    delete s;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuStreamSynchronize(CUstream stream) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuStreamSynchronize")) return e;
    auto* s = reinterpret_cast<Stream*>(stream);
    if (int e = f->needCtx("cuStreamSynchronize", f->streamCtx(s))) return e;
    const std::lock_guard<std::mutex> lock(f->m_);
    ++f->c_.syncs;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuGraphicsGLRegisterImage(CUgraphicsResource* res, unsigned image, unsigned target, unsigned flags) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuGraphicsGLRegisterImage")) return e;
    if (int e = f->needGl("cuGraphicsGLRegisterImage")) return e;
    if (int e = f->needCtx("cuGraphicsGLRegisterImage", nullptr)) return e;
    QOpenGLExtraFunctions* gl = QOpenGLContext::currentContext()->extraFunctions();
    if (target != framebeam::cuda::kGlTexture2D || gl->glIsTexture(image) == GL_FALSE) {
      f->violate(QStringLiteral("cuGraphicsGLRegisterImage of something that is not a GL_TEXTURE_2D"));
      return 1;
    }
    GLint prev = 0, format = 0, w = 0, h = 0;
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    gl->glBindTexture(GL_TEXTURE_2D, image);
    gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, kTexInternalFormat, &format);
    gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, kTexWidth, &w);
    gl->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, kTexHeight, &h);
    gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
    if (static_cast<unsigned>(format) != kGlRgba8) {
      f->violate(QStringLiteral("cuGraphicsGLRegisterImage of a texture with internal format 0x%1, not GL_RGBA8").arg(format, 0, 16));
      return 1;
    }
    const std::lock_guard<std::mutex> lock(f->m_);
    for (const auto& r : f->resources_) {
      if (r.second->tex == image) {
        f->violations_ << QStringLiteral("texture %1 registered twice").arg(image);
        return 1;
      }
    }
    auto* r = new Res;
    r->tex = image;
    r->w = w;
    r->h = h;
    r->ctx = reinterpret_cast<Ctx*>(top());
    f->resources_[reinterpret_cast<CUgraphicsResource>(r)].reset(r);
    f->lastRegisterFlags_ = flags;
    ++f->c_.registrations;
    *res = reinterpret_cast<CUgraphicsResource>(r);
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuGraphicsUnregisterResource(CUgraphicsResource res) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuGraphicsUnregisterResource")) return e;
    if (int e = f->needGl("cuGraphicsUnregisterResource")) return e;
    Res* r = f->resource(res);
    if (!r) {
      f->violate(QStringLiteral("cuGraphicsUnregisterResource of an unknown resource"));
      return 400;
    }
    if (int e = f->needCtx("cuGraphicsUnregisterResource", reinterpret_cast<CUcontext>(r->ctx))) return e;
    if (r->mapped) f->violate(QStringLiteral("cuGraphicsUnregisterResource while mapped"));
    if (QOpenGLContext::currentContext()->extraFunctions()->glIsTexture(r->tex) == GL_FALSE) {
      f->violate(QStringLiteral("cuGraphicsUnregisterResource after the texture was deleted"));
    }
    const std::lock_guard<std::mutex> lock(f->m_);
    ++f->c_.unregistrations;
    f->resources_.erase(res);
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuGraphicsMapResources(unsigned count, CUgraphicsResource* res, CUstream stream) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuGraphicsMapResources")) return e;
    if (int e = f->needGl("cuGraphicsMapResources")) return e;
    if (count != 1) {
      f->violate(QStringLiteral("cuGraphicsMapResources with count != 1"));
      return 1;
    }
    Res* r = f->resource(*res);
    if (!r) {
      f->violate(QStringLiteral("cuGraphicsMapResources of an unknown resource"));
      return 400;
    }
    if (int e = f->needCtx("cuGraphicsMapResources", reinterpret_cast<CUcontext>(r->ctx))) return e;
    if (f->streamCtx(reinterpret_cast<Stream*>(stream)) != reinterpret_cast<CUcontext>(r->ctx)) {
      f->violate(QStringLiteral("cuGraphicsMapResources on a stream of another context"));
    }
    if (r->mapped) {
      f->violate(QStringLiteral("cuGraphicsMapResources of a mapped resource"));
      return 208;  // CUDA_ERROR_ALREADY_MAPPED
    }
    r->snapshot = readTexture(r->tex, r->w, r->h);
    r->mapped = true;
    r->array = new Array;
    const std::lock_guard<std::mutex> lock(f->m_);
    f->arrays_.insert(r->array);
    ++f->c_.maps;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuGraphicsSubResourceGetMappedArray(CUarray* array, CUgraphicsResource res, unsigned index, unsigned level) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuGraphicsSubResourceGetMappedArray")) return e;
    Res* r = f->resource(res);
    if (!r) {
      f->violate(QStringLiteral("cuGraphicsSubResourceGetMappedArray of an unknown resource"));
      return 400;
    }
    if (int e = f->needCtx("cuGraphicsSubResourceGetMappedArray", reinterpret_cast<CUcontext>(r->ctx))) return e;
    if (!r->mapped) {
      f->violate(QStringLiteral("cuGraphicsSubResourceGetMappedArray of an unmapped resource"));
      return 211;  // CUDA_ERROR_NOT_MAPPED
    }
    if (index != 0 || level != 0) return 1;
    *array = reinterpret_cast<CUarray>(r->array);
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuGraphicsUnmapResources(unsigned count, CUgraphicsResource* res, CUstream stream) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuGraphicsUnmapResources")) return e;
    if (int e = f->needGl("cuGraphicsUnmapResources")) return e;
    if (count != 1) {
      f->violate(QStringLiteral("cuGraphicsUnmapResources with count != 1"));
      return 1;
    }
    Res* r = f->resource(*res);
    if (!r) {
      f->violate(QStringLiteral("cuGraphicsUnmapResources of an unknown resource"));
      return 400;
    }
    if (int e = f->needCtx("cuGraphicsUnmapResources", reinterpret_cast<CUcontext>(r->ctx))) return e;
    if (f->streamCtx(reinterpret_cast<Stream*>(stream)) != reinterpret_cast<CUcontext>(r->ctx)) {
      f->violate(QStringLiteral("cuGraphicsUnmapResources on a stream of another context"));
    }
    if (!r->mapped) {
      f->violate(QStringLiteral("cuGraphicsUnmapResources of an unmapped resource"));
      return 211;
    }
    r->mapped = false;
    r->array = nullptr;  // the Array object stays in arrays_: copying from it later is a violation
    const std::lock_guard<std::mutex> lock(f->m_);
    ++f->c_.unmaps;
    return 0;
  }
  static CUresult FB_CUDAAPI fake_cuMemcpy2DAsync_v2(const framebeam::cuda::Memcpy2D* copy, CUstream stream) {
    FakeCuda* f = self();
    if (!f) return 0;
    if (int e = f->inject("cuMemcpy2DAsync_v2")) return e;
    if (int e = f->needCtx("cuMemcpy2DAsync_v2", f->streamCtx(reinterpret_cast<Stream*>(stream)))) return e;
    Res* r = nullptr;
    {
      const std::lock_guard<std::mutex> lock(f->m_);
      for (const auto& entry : f->resources_) {
        if (entry.second->mapped && reinterpret_cast<CUarray>(entry.second->array) == copy->srcArray) r = entry.second.get();
      }
    }
    if (!r) {
      f->violate(QStringLiteral("cuMemcpy2DAsync_v2 from an array that is not mapped (stale or unknown)"));
      return 211;
    }
    if (copy->srcMemoryType != framebeam::cuda::kMemoryTypeArray || copy->dstMemoryType != framebeam::cuda::kMemoryTypeDevice ||
        copy->dstDevice == 0 || copy->srcXInBytes != 0 || copy->srcY != 0 || copy->dstXInBytes != 0 || copy->dstY != 0) {
      f->violate(QStringLiteral("cuMemcpy2DAsync_v2 with an unexpected source/destination description"));
      return 1;
    }
    const size_t rowBytes = static_cast<size_t>(r->w) * 4;
    if (copy->WidthInBytes > rowBytes || copy->Height > static_cast<size_t>(r->h) || copy->dstPitch < copy->WidthInBytes) {
      f->violate(QStringLiteral("cuMemcpy2DAsync_v2 beyond the texture or the pitch"));
      return 1;
    }
    auto* dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(copy->dstDevice));
    for (size_t y = 0; y < copy->Height; ++y) {
      std::memcpy(dst + y * copy->dstPitch, r->snapshot.data() + y * rowBytes, copy->WidthInBytes);
    }
    const std::lock_guard<std::mutex> lock(f->m_);
    ++f->c_.copies;
    return 0;
  }

  Res* resource(CUgraphicsResource handle) {
    const std::lock_guard<std::mutex> lock(m_);
    const auto it = resources_.find(handle);
    return it == resources_.end() ? nullptr : it->second.get();
  }
  CUcontext streamCtx(Stream* s) {
    const std::lock_guard<std::mutex> lock(m_);
    return streams_.count(s) != 0 ? reinterpret_cast<CUcontext>(s->ctx) : nullptr;
  }

  inline static FakeCuda* s_instance = nullptr;
  inline static std::atomic<int> s_liveFrames{0};

  framebeam::cuda::Api api_;
  mutable std::mutex m_;
  Counters c_;
  std::map<std::string, std::pair<int, int>> injected_;
  std::set<Ctx*> contexts_;
  std::set<Stream*> streams_;
  std::map<CUgraphicsResource, std::unique_ptr<Res>> resources_;
  std::set<Array*> arrays_;  // every array ever handed out, freed with the fake
  std::vector<std::thread::id> createThreads_;
  std::vector<QSize> configures_;
  QStringList violations_;
  QString poolFailure_, configureFailure_;
  std::atomic<int> contextDelayMs_{0};
  std::atomic<unsigned> glDevices_{1};
  std::atomic<unsigned> lastRegisterFlags_{0}, lastContextFlags_{0}, lastStreamFlags_{0};
};

// Frames in host memory with the pitch rounded up to 256 bytes (so pitch bugs show), poisoned with 0xCD. The last
// release of the pool or of any of its frames destroys the context, like the device of the FFmpeg pool (design I2).
class HostFramePool final : public framebeam::CudaFramePool {
 public:
  HostFramePool(const framebeam::cuda::Api* api, CUcontext ctx, FakeCuda* fake = nullptr)
      : state_(std::make_shared<State>(*api, ctx)), fake_(fake) {}

  bool configure(int width, int height, int prealloc, QString* why) override {
    if (fake_) {
      const std::lock_guard<std::mutex> lock(fake_->m_);
      fake_->configures_.push_back(QSize(width, height));
      if (!fake_->configureFailure_.isEmpty()) {
        *why = fake_->configureFailure_;
        return false;
      }
    }
    (void)prealloc;
    size_ = QSize(width, height);
    pitch_ = ((width * 4 + 255) / 256) * 256;
    return true;
  }

  AVFrame* get(QString* why) override {
    if (size_.isEmpty()) {
      *why = QStringLiteral("pool not configured");
      return nullptr;
    }
    AVFrame* f = av_frame_alloc();
    const size_t bytes = static_cast<size_t>(pitch_) * static_cast<size_t>(size_.height());
    auto* mem = static_cast<uint8_t*>(av_malloc(bytes));
    if (!f || !mem) {
      av_frame_free(&f);
      av_free(mem);
      *why = QStringLiteral("out of memory");
      return nullptr;
    }
    std::memset(mem, 0xCD, bytes);
    auto* ref = new std::shared_ptr<State>(state_);
    f->buf[0] = av_buffer_create(mem, bytes, &HostFramePool::release, ref, 0);
    f->data[0] = mem;
    f->linesize[0] = pitch_;
    f->width = size_.width();
    f->height = size_.height();
    f->format = AV_PIX_FMT_CUDA;
    FakeCuda::frameCreated();
    return f;
  }

  QSize size() const override { return size_; }
  int pitch() const { return pitch_; }

 private:
  struct State {
    State(const framebeam::cuda::Api& a, CUcontext c) : api(a), ctx(c) {}
    ~State() {
      if (ctx && api.cuCtxDestroy_v2) api.cuCtxDestroy_v2(ctx);
    }
    framebeam::cuda::Api api;
    CUcontext ctx;
  };
  static void release(void* opaque, uint8_t* data) {
    av_free(data);
    FakeCuda::frameReleased();
    delete static_cast<std::shared_ptr<State>*>(opaque);
  }

  std::shared_ptr<State> state_;
  FakeCuda* fake_;
  QSize size_;
  int pitch_ = 0;
};

inline framebeam::CudaGlCapture::Deps FakeCuda::deps() {
  framebeam::CudaGlCapture::Deps d;
  d.api = &api_;
  d.makePool = [this](void* ctx, QString* why) -> std::unique_ptr<framebeam::CudaFramePool> {
    {
      const std::lock_guard<std::mutex> lock(m_);
      if (!poolFailure_.isEmpty()) {
        *why = poolFailure_;
        return nullptr;
      }
    }
    return std::make_unique<HostFramePool>(&api_, static_cast<CUcontext>(ctx), this);
  };
  return d;
}

}  // namespace fbtest
