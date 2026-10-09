#pragma once

#include <QSize>
#include <QString>
#include <functional>
#include <memory>

struct AVBufferRef;
struct AVFrame;

namespace framebeam {

namespace cuda {
struct Api;
}

// Allocates the CUDA frames CudaGlCapture copies into. Seam for tests (host memory instead of FFmpeg CUDA frames).
class CudaFramePool {
 public:
  virtual ~CudaFramePool() = default;
  // (Re)creates the frames for width x height RGB0 (bytes R,G,B,X, top row first); `prealloc` frames up front.
  virtual bool configure(int width, int height, int prealloc, QString* why) = 0;
  // New frame, caller owns it: data[0] = device pointer (CUdeviceptr as uintptr_t), linesize[0] = row pitch.
  virtual AVFrame* get(QString* why) = 0;
  virtual QSize size() const = 0;
};

// Real pool: av_hwdevice_ctx_alloc(CUDA) with our context (stream NULL), av_hwframe_ctx (CUDA/RGB0). Takes ownership
// of `cuContext` only when it returns non-null (then AVHWDeviceContext.free destroys the context).
std::unique_ptr<CudaFramePool> makeFfmpegCudaFramePool(void* cuContext, QString* why);

// GL texture -> FFmpeg CUDA frame for h264_nvenc (ADR 0019). attach/detach/capture run on ONE thread, the one with
// the texture's GL context current (the emulation thread). status()/reason() are thread-safe. The destructor may run
// on any thread, after detach().
class CudaGlCapture {
 public:
  enum class Status { Ok, NotReady, Unavailable, Failed };
  struct Deps {
    const cuda::Api* api = nullptr;  // null: the real driver (cuda::Driver). Set: tests; no driver load, no vendor check
    std::function<std::unique_ptr<CudaFramePool>(void* cuContext, QString* why)> makePool;  // empty: FFmpeg pool
  };
  // Two constructors instead of `Deps deps = {}`: a default argument may not use Deps's member initializers before the
  // end of the enclosing class (GCC/Clang reject it).
  CudaGlCapture();  // the real driver and the FFmpeg pool
  explicit CudaGlCapture(Deps deps);
  ~CudaGlCapture();
  CudaGlCapture(const CudaGlCapture&) = delete;
  CudaGlCapture& operator=(const CudaGlCapture&) = delete;

  // Registers `glTexture2D` (GL_TEXTURE_2D, sized GL_RGBA8, width x height, image top row = GL row 0). NotReady until
  // the CUDA context exists (created off the calling thread); the caller asks again on a later frame.
  Status attach(unsigned glTexture2D, int width, int height);
  void detach(bool glCurrent);  // unregister (GL current + context pushed) or leak the registration
  // Copies the registered texture into a new frame and waits until the copy has finished on the GPU. Complete frame,
  // or null (then status() is Failed).
  std::shared_ptr<AVFrame> capture();
  Status status() const;
  QString reason() const;      // why Unavailable/Failed (already logged)
  QString deviceName() const;  // e.g. "NVIDIA GeForce RTX 4080" after the first Ok

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

// After a GPU encode failure (worker thread): if `framesCtx` is a real CUDA frames context and the driver is Ready,
// push its context, cuCtxSynchronize, pop. Returns that CUresult (0 = healthy or not checkable); a fatal code marks
// the driver Dead.
int cudaHealthCheck(AVBufferRef* framesCtx);

}  // namespace framebeam
