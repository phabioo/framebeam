#pragma once
// GpuEncodeTarget: consumer of the Session encode texture (ADR 0019). framebeam_emulation only defines the interface;
// the implementation (CUDA-GL interop for h264_nvenc) lives outside and is injected by the UI.
#include <QSize>
#include <QString>

namespace framebeam::emu {

class GpuEncodeTarget {
 public:
  // Unavailable: GPU-direct encoding is not applicable here (e.g. the GL context is not on an NVIDIA GPU); the target
  // already reports that itself, so the caller releases it quietly (no fail(), no warning). Failed: unusable, a failure.
  enum class Attach { Ok, NotReady, Unavailable, Failed };
  virtual ~GpuEncodeTarget() = default;
  // Any thread, cheap (atomics). false: nothing is blitted or captured.
  virtual bool wanted() const = 0;
  // Any thread, constant. The encode texture is the frame fitted into it (aspect kept, never upscaled, even).
  virtual QSize maxSize() const = 0;
  // Emulation thread, GL current. Registers `texture` (GL_TEXTURE_2D, sized GL_RGBA8, width x height, image top row =
  // GL row 0). NotReady: nothing registered, ask again on a later frame. Unavailable: not applicable here, the caller
  // releases it quietly. Failed: unusable; the caller drops it.
  virtual Attach attach(unsigned texture, int width, int height) = 0;
  // Emulation thread. Releases the registration; called before the texture is deleted and before the target is
  // dropped. Idempotent. glCurrent == false: the GL context is lost; the registration is leaked, never released
  // without GL.
  virtual void detach(bool glCurrent) = 0;
  // Emulation thread, GL current. Copies the texture into a new frame and publishes it. Returns after the copy has
  // finished on the GPU, so the texture may be overwritten afterwards. false: failed; the caller drops the target.
  virtual bool capture() = 0;
  // Any thread, idempotent, the first reason wins. The producer gave up (GL-side failure); consumers fall back.
  virtual void fail(const QString& reason) = 0;
};

}  // namespace framebeam::emu
