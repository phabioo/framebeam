#pragma once
// Zero-copy display (ADR 0027): platform-neutral producer/consumer interfaces. The Windows implementation is the D3D11
// display ring (d3d11displayring.h); other platforms have none and keep the readback path.
#include <QSize>
#include <QString>
#include <QtGlobal>

namespace framebeam {

// Producer side. Emulation thread only, with the game's GL context current.
class DisplayProducer {
 public:
  enum class Status { Ok, Skip, NotReady, Unavailable, Failed };
  virtual ~DisplayProducer() = default;
  // Hands out a GL_TEXTURE_2D (RGBA8, *texSize >= w x h) to write a w x h image into: top-left aligned, image top row =
  // GL row 0. Ok: the texture is locked for GL until present()/abort(). Skip: no free slot this frame (draw nothing,
  // not an error). NotReady: ask again on a later frame. Unavailable: not applicable here (reason()). Failed: unusable.
  virtual Status acquire(int w, int h, unsigned* glTexture, QSize* texSize) = 0;
  // After a successful acquire() and a complete blit (glFlush done): unlocks and publishes the w x h frame.
  virtual void present(int w, int h) = 0;
  // After a successful acquire() when the blit failed: unlocks without publishing.
  virtual void abort() = 0;
  // Releases every GL/interop registration (glCurrent == false: GL is gone, registrations are leaked). Idempotent.
  virtual void release(bool glCurrent) = 0;
  virtual QString reason() const = 0;       // any thread; why Unavailable/Failed
  virtual QString adapterName() const = 0;  // any thread; e.g. "AMD Radeon RX 7800 XT" once open, else empty
};

}  // namespace framebeam
