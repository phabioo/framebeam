#pragma once
// GpuDisplayTarget: producer of the zero-copy game view (ADR 0027). framebeam_emulation only defines the interface;
// the implementation (a platform display ring behind framebeam::DisplayProducer) lives outside and is injected by the UI.
// Unlike GpuEncodeTarget the target owns the GL texture that receives the frame; HwRenderContext only attaches it to an
// FBO, blits into it and never deletes it.
#include <QSize>
#include <QString>

namespace framebeam::emu {

class GpuDisplayTarget {
 public:
  // Ok: *glTexture (GL_TEXTURE_2D, RGBA8, *texSize >= w x h) is locked for GL until present()/abort(). Skip: no free
  // slot this frame, nothing to draw (not an error). NotReady: ask again on a later frame. Unavailable: not applicable
  // here, the target already reports that itself; the caller releases it quietly. Failed: unusable; the caller drops it.
  enum class Acquire { Ok, Skip, NotReady, Unavailable, Failed };
  virtual ~GpuDisplayTarget() = default;
  // Any thread, cheap (atomics). false: nothing is blitted.
  virtual bool wanted() const = 0;
  // Emulation thread, GL current. Image top row = GL row 0, drawn into the top-left w x h region of the texture.
  virtual Acquire acquire(int w, int h, unsigned* glTexture, QSize* texSize) = 0;
  // After Ok and a complete blit (glFlush done): unlocks and publishes the w x h frame.
  virtual void present(int w, int h) = 0;
  // After Ok when the blit failed: unlocks without publishing.
  virtual void abort() = 0;
  // Emulation thread. Releases every GL registration; glCurrent == false: the context is lost, they are leaked.
  // Idempotent.
  virtual void release(bool glCurrent) = 0;
  // Any thread, idempotent, the first reason wins. The producer gave up (GL-side failure); consumers fall back.
  virtual void fail(const QString& reason) = 0;
};

}  // namespace framebeam::emu
