#pragma once
// HwRenderContext: offscreen OpenGL context + Player-owned FBO for libretro hardware rendering.
//
// One instance per LibretroBackend. Thread rules (ADR 0013):
//  - prepareSurface() runs on the GUI thread (QOffscreenSurface must be created there);
//  - createContext()/makeCurrent()/readback()/destroyContext() run on the emulation thread.
// The context is NOT shared with Qt Quick; each hardware frame is read back into an XRGB8888 QImage.

#include <QImage>
#include <QString>

#include <memory>

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
  QImage readback(int w, int h, bool bottomLeftOrigin);
  bool asyncReadback() const;  // PBO path currently in use

 private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

}  // namespace framebeam::emu
