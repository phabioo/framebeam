#pragma once
// HwRenderContext: offscreen OpenGL context + Player-owned FBO for libretro hardware rendering.
//
// One instance per LibretroBackend. Thread rules (ADR 0013):
//  - prepareSurface() runs on the GUI thread (QOffscreenSurface must be created there);
//  - createContext()/makeCurrent()/readback()/destroyContext() run on the emulation thread.
// The context is NOT shared with Qt Quick; each hardware frame is read back into an XRGB8888 QImage. An optional
// GpuEncodeTarget additionally receives the Session frame as a GL texture (setEncodeTarget, ADR 0019).

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

#include "gpu_encode_target.h"

namespace framebeam::emu {

class HwRenderContext {
 public:
  HwRenderContext();
  ~HwRenderContext();
  Q_DISABLE_COPY(HwRenderContext)

  // true: a QGuiApplication exists and FRAMEBEAM_DISABLE_HW_RENDER is not "1" (read on every call).
  static bool allowed();

  // GUI thread only; no-op when not allowed() or already created.
  void prepareSurface();
  // allowed() and a usable surface exists (or can still be created because we are on the GUI thread).
  bool available();

  // Emulation thread. core = RETRO_HW_CONTEXT_OPENGL_CORE (core profile, at least 3.2) or compatibility
  // context; creates the context, makes it current and builds the FBO (initial size, grown later).
  bool createContext(bool coreProfile, unsigned major, unsigned minor, bool depth, bool stencil, QString* error);
  void destroyContext();  // emulation thread; releases FBO and context (surface is kept)
  bool hasContext() const;

  bool makeCurrent();
  void doneCurrent();

  // Grows the FBO to at least w x h (the FBO id stays stable). Context must be current.
  bool ensureSize(int w, int h);
  quintptr framebuffer() const;
  using ProcAddress = void (*)();
  ProcAddress procAddress(const char* name) const;
  QString glInfo() const;  // "vendor / renderer / version" for logs
  // Diagnostics overlay: GL_RENDERER / GL_VERSION of the live context (empty before createContext) and its profile.
  QString glRenderer() const;
  QString glVersion() const;
  bool isCoreProfile() const;
  // Pure helpers for the overlay: "4.6.0 NVIDIA 555.1" + core -> "OpenGL 4.6 Core"; renderer + version ->
  // "<renderer> · Driver <rest of the version string>" (profile hints in parentheses are dropped).
  static QString describeApi(const QString& glVersion, bool coreProfile);
  static QString describeGpu(const QString& glRenderer, const QString& glVersion);

  // Reads the w x h region of the FBO into a new RGB32 image (alpha forced to 0xFF); null on failure.
  // Asynchronous via two pixel-pack buffers: the first call returns its own frame, every later call returns the
  // PREVIOUS call's frame (one frame of latency, no GPU stall). Falls back to a synchronous glReadPixels when
  // PBOs or mapping fail or FRAMEBEAM_SYNC_READBACK=1 (read at createContext).
  //
  // maxSize (width/height in pixels, empty or non-positive component = no limit): when the w x h frame is larger,
  // it is first downscaled on the GPU (aspect ratio kept, never upscaled) and only that is read back: exact 2:1
  // GL_LINEAR halvings (box filter) while the result stays >= the target, then one final GL_LINEAR blit with a
  // ratio below 2:1, so no texel is skipped and nothing aliases. The vertical flip is done by the first blit. The
  // chain's FBOs are cached per size; if one cannot be created or a blit fails, the full-size frame is read back
  // (and scaling stays off).
  QImage readback(int w, int h, bool bottomLeftOrigin, const QSize& maxSize = {});
  // Size readback() uses for a w x h frame under maxSize (pure; no upscaling, aspect kept, never above maxSize).
  static QSize scaledReadbackSize(int w, int h, const QSize& maxSize);
  bool asyncReadback() const;  // PBO path currently in use

  // Session encoding from the GPU (ADR 0019). Emulation thread, context current. With a target set, each hardware frame
  // is also scaled into a separate GL_RGBA8 encode texture, which the target copies out at the start of the next frame;
  // the readback above is unchanged. Without a target none of this costs a GL call.
  //
  // Replaces the target: the old one is detached and the encode GL objects are freed (nullptr = none).
  void setEncodeTarget(std::shared_ptr<GpuEncodeTarget> target);
  bool hasEncodeTarget() const;
  // After readback(): when target->wanted(), downscales (aspect kept, never upscaled, even, within maxSize()) and flips
  // (bottomLeftOrigin) the w x h frame into the encode texture, then glFlush(). Never waits for the GPU.
  void encodeBlit(int w, int h, bool bottomLeftOrigin);
  // Duplicate frame (video_refresh with NULL): the encode texture still holds the last frame; capture it again.
  void encodeRepeat();
  // Start of the next frame, before the core renders again: target->capture(), which waits for its own copy.
  // true = a frame was captured; false = nothing pending, not wanted, or the target failed (then it was dropped).
  bool captureEncode();
  QSize encodeSize() const;  // current encode texture size (tests, logs); empty without one
  // Encode texture size for a w x h frame: scaledReadbackSize(), then rounded down to even, at least 2 x 2.
  static QSize encodeSizeFor(int w, int h, const QSize& maxSize);

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

}  // namespace framebeam::emu
