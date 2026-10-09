#include "gpuencodebridge.h"

#include <utility>

namespace framebeam::ui {

GpuEncodePlan planGpuEncode(bool shared, bool hwGame, bool encoderRunning, bool gpuAllowed, bool gpuActive) {
  GpuEncodePlan p;
  p.keep = shared && hwGame && gpuAllowed;
  p.create = p.keep && encoderRunning;
  p.wanted = p.create;
  p.shareSize = shared ? (gpuActive ? QSize() : kShareEncodeMax) : QSize();
  return p;
}

GpuEncodeBridge::GpuEncodeBridge(const QSize& maxSize) : maxSize_(maxSize) {}

GpuEncodeBridge::GpuEncodeBridge(const QSize& maxSize, CudaGlCapture::Deps deps) : capture_(std::move(deps)), maxSize_(maxSize) {}

GpuEncodeBridge::~GpuEncodeBridge() = default;

void GpuEncodeBridge::setWanted(bool on) {
  std::shared_ptr<AVFrame> old;  // released after the unlock: returning a pool buffer pushes the CUDA context
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wanted_ = on;
    if (!on) {
      old = std::move(latest_);
      latest_.reset();
    }
  }
}

std::shared_ptr<AVFrame> GpuEncodeBridge::takeFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(latest_, nullptr);
}

GpuEncodeBridge::State GpuEncodeBridge::state() const { return state_.load(); }

QString GpuEncodeBridge::reason() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return reason_;
}

bool GpuEncodeBridge::wanted() const { return wanted_.load() && state_.load() == State::Running; }

QSize GpuEncodeBridge::maxSize() const { return maxSize_; }

emu::GpuEncodeTarget::Attach GpuEncodeBridge::attach(unsigned texture, int width, int height) {
  switch (capture_.attach(texture, width, height)) {
    case CudaGlCapture::Status::Ok:
      return Attach::Ok;
    case CudaGlCapture::Status::NotReady:
      return Attach::NotReady;
    case CudaGlCapture::Status::Unavailable:
      transition(State::Unavailable, capture_.reason());
      return Attach::Unavailable;
    case CudaGlCapture::Status::Failed:
      transition(State::Failed, capture_.reason());
      return Attach::Failed;
  }
  return Attach::Failed;
}

void GpuEncodeBridge::detach(bool glCurrent) { capture_.detach(glCurrent); }

bool GpuEncodeBridge::capture() {
  std::shared_ptr<AVFrame> frame = capture_.capture();
  if (!frame) {
    const QString why = capture_.reason();
    transition(State::Failed, why.isEmpty() ? QStringLiteral("capture failed") : why);
    return false;
  }
  std::shared_ptr<AVFrame> old;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!wanted_.load()) {
      return true;  // not wanted any more: the frame goes back to the pool below, outside the lock
    }
    old = std::exchange(latest_, std::move(frame));
  }
  return true;  // `old` (an untaken frame) is released here, outside the lock
}

void GpuEncodeBridge::fail(const QString& reason) { transition(State::Failed, reason); }

void GpuEncodeBridge::transition(State to, const QString& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.load() != State::Running) {
    return;  // the first reason wins; Unavailable is never turned into Failed
  }
  reason_ = reason;
  state_.store(to);
}

}  // namespace framebeam::ui
