#pragma once
// GPU-direct encoding of the own Session (ADR 0019, ADR 0027 D6): the adapter between the emulation thread and the UI
// thread. framebeam_emulation only knows the interface emu::GpuEncodeTarget; framebeam_media implements the captures
// (CudaGlCapture for NVIDIA, D3d11EncodeCapture for AMD AMF and Intel QSV); framebeam_ui, which links both, joins them here.

#include <QSize>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "cudaglcapture.h"
#include "gpu_encode_target.h"

struct AVFrame;

namespace framebeam {
class GlD3d11Bridge;
}

namespace framebeam::ui {

// Largest frame the Session encoder is fed from the local game (the encoder opens at the frame size it gets). While
// shared, the GPU readback keeps at least this much of a larger hardware frame (the view's own size may need more), and
// the GPU-direct encode texture is the frame fitted into it.
inline constexpr QSize kShareEncodeMax(1280, 1920);

struct GpuEncodePlan {
  bool keep = false;    // a bridge may exist (else drop it)
  bool create = false;  // a bridge must exist now
  bool wanted = false;  // the bridge produces frames
  QSize shareSize;      // GameSession::setShareSize
};
// Pure; unit-tested as a truth table.
//   keep      = shared && hwGame && gpuAllowed
//   create    = keep && encoderRunning
//   wanted    = create
//   shareSize = shared ? (gpuActive ? QSize() : kShareEncodeMax) : QSize()
GpuEncodePlan planGpuEncode(bool shared, bool hwGame, bool encoderRunning, bool gpuAllowed, bool gpuActive);

// The capture seam: what the bridge needs from CudaGlCapture and D3d11EncodeCapture. All calls on the emulation thread
// with the game's GL current, like emu::GpuEncodeTarget.
class GpuCapture {
 public:
  enum class Status { Ok, NotReady, Unavailable, Failed };
  enum class Backend { Cuda, Amf, Qsv };
  virtual ~GpuCapture() = default;
  virtual Status attach(unsigned glTexture2D, int width, int height) = 0;
  virtual void detach(bool glCurrent) = 0;
  virtual std::shared_ptr<AVFrame> capture() = 0;  // null: failed
  virtual QString reason() const = 0;
  virtual Backend backend() const = 0;  // valid once attach() returned Ok
};

// Which backends SessionHost::gpuBackendsAllowed() permits (computed on the UI thread, fixed for the bridge's life).
struct GpuEncodeAllowed {
  bool cuda = false;
  bool amf = false;
  bool qsv = false;
  bool any() const { return cuda || amf || qsv; }
};

struct GpuEncodeOptions {
  GpuEncodeAllowed allowed;
  // Emulation thread: the GL-D3D11 bridge of the game's display, or null/empty (then the D3D11 capture opens its own).
  std::function<std::shared_ptr<GlD3d11Bridge>()> displayBridge;
  // Seams for tests; empty = the real captures. makeD3d11 gets displayBridge.
  std::function<std::unique_ptr<GpuCapture>()> makeCuda;
  std::function<std::unique_ptr<GpuCapture>(std::function<std::shared_ptr<GlD3d11Bridge>()>)> makeD3d11;
  std::function<QString()> glVendor;  // empty: GL_VENDOR of the current context
};

// emu::GpuEncodeTarget on top of a capture (chosen lazily at the first attach) plus a one-slot mailbox for the UI thread.
//  Backend choice, emulation thread, first attach: the D3D11 capture first when AMF or QSV is allowed and the game's GL
//  context is not on an NVIDIA GPU (with CUDA allowed; NVIDIA GL contexts never touch D3D11, so ADR 0019 behaves exactly as
//  before there): taken when it reports Ok for a backend that is allowed. Otherwise CUDA when allowed. Otherwise
//  Unavailable (Failed when the D3D11 capture failed for real).
class GpuEncodeBridge final : public emu::GpuEncodeTarget {
 public:
  enum class State { Running, Unavailable, Failed };
  // Constructors instead of `Deps deps = {}`: a default argument may not use Deps's member initializers before the end of
  // the enclosing class (GCC/Clang reject it; CudaGlCapture has the same pair).
  explicit GpuEncodeBridge(const QSize& maxSize);  // CUDA only: the real driver and the FFmpeg pool
  GpuEncodeBridge(const QSize& maxSize, CudaGlCapture::Deps deps);  // CUDA only, test deps
  GpuEncodeBridge(const QSize& maxSize, GpuEncodeOptions options);
  ~GpuEncodeBridge() override;  // any thread

  // UI thread
  void setWanted(bool on);                  // false also empties the mailbox (frame released outside the lock)
  std::shared_ptr<AVFrame> takeFrame();     // newest captured frame or null
  State state() const;                      // atomic
  QString reason() const;                   // mutex; set together with Unavailable/Failed

  // emu::GpuEncodeTarget
  bool wanted() const override;             // wanted_ && state_ == Running
  QSize maxSize() const override;
  Attach attach(unsigned texture, int width, int height) override;  // Ok/NotReady; Unavailable/Failed also set the state
  void detach(bool glCurrent) override;     // chosen capture's detach(glCurrent)
  bool capture() override;                  // stores the frame in the mailbox, only while wanted
  void fail(const QString& reason) override;  // Running -> Failed, first reason wins; never overrides Unavailable

 private:
  // Running -> `to`, with the reason; a no-op in any other state (the first reason wins).
  void transition(State to, const QString& reason);

  emu::GpuEncodeTarget::Attach finish(GpuCapture::Status status, const QString& reason);
  emu::GpuEncodeTarget::Attach selectAndAttach(unsigned texture, int width, int height);

  const GpuEncodeOptions options_;
  std::unique_ptr<GpuCapture> capture_;  // emulation thread: chosen at the first attach, null before
  std::unique_ptr<GpuCapture> pendingD3d11_;  // a D3D11 capture whose bridge was NotReady; the choice is made again
  const QSize maxSize_;
  std::atomic<bool> wanted_{false};
  std::atomic<State> state_{State::Running};
  mutable std::mutex mutex_;                // guards latest_ and reason_
  std::shared_ptr<AVFrame> latest_;
  QString reason_;
};

}  // namespace framebeam::ui
