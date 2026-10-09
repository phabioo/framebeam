#pragma once
// GPU-direct encoding of the own Session (ADR 0019): the adapter between the emulation thread and the UI thread.
// framebeam_emulation only knows the interface emu::GpuEncodeTarget; framebeam_media implements the CUDA side
// (CudaGlCapture); framebeam_ui, which links both, joins them here.

#include <QSize>
#include <QString>
#include <atomic>
#include <memory>
#include <mutex>

#include "cudaglcapture.h"
#include "gpu_encode_target.h"

struct AVFrame;

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

// emu::GpuEncodeTarget on top of CudaGlCapture plus a one-slot mailbox for the UI thread.
class GpuEncodeBridge final : public emu::GpuEncodeTarget {
 public:
  enum class State { Running, Unavailable, Failed };
  // Two constructors instead of `CudaGlCapture::Deps deps = {}`: a default argument may not use Deps's member
  // initializers before the end of the enclosing class (GCC/Clang reject it; CudaGlCapture has the same pair).
  explicit GpuEncodeBridge(const QSize& maxSize);  // the real driver and the FFmpeg pool
  GpuEncodeBridge(const QSize& maxSize, CudaGlCapture::Deps deps);
  ~GpuEncodeBridge() override;  // any thread

  // UI thread
  void setWanted(bool on);                  // false also empties the mailbox (frame released outside the lock)
  std::shared_ptr<AVFrame> takeFrame();     // newest captured frame or null
  State state() const;                      // atomic
  QString reason() const;                   // mutex; set together with Unavailable/Failed

  // emu::GpuEncodeTarget
  bool wanted() const override;             // wanted_ && state_ == Running
  QSize maxSize() const override;
  Attach attach(unsigned texture, int width, int height) override;  // Ok/NotReady; Unavailable|Failed -> state, Failed
  void detach(bool glCurrent) override;     // capture_.detach(glCurrent)
  bool capture() override;                  // stores the frame in the mailbox, only while wanted
  void fail(const QString& reason) override;  // Running -> Failed, first reason wins; never overrides Unavailable

 private:
  // Running -> `to`, with the reason; a no-op in any other state (the first reason wins).
  void transition(State to, const QString& reason);

  CudaGlCapture capture_;
  const QSize maxSize_;
  std::atomic<bool> wanted_{false};
  std::atomic<State> state_{State::Running};
  mutable std::mutex mutex_;                // guards latest_ and reason_
  std::shared_ptr<AVFrame> latest_;
  QString reason_;
};

}  // namespace framebeam::ui
