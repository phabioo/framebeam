#include "gpuencodebridge.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <utility>

#include "d3d11encodecapture.h"
#include "d3d11glbridge.h"

namespace framebeam::ui {

GpuEncodePlan planGpuEncode(bool shared, bool hwGame, bool encoderRunning, bool gpuAllowed, bool gpuActive) {
  GpuEncodePlan p;
  p.keep = shared && hwGame && gpuAllowed;
  p.create = p.keep && encoderRunning;
  p.wanted = p.create;
  p.shareSize = shared ? (gpuActive ? QSize() : kShareEncodeMax) : QSize();
  return p;
}

namespace {

GpuCapture::Status fromCuda(CudaGlCapture::Status s) {
  switch (s) {
    case CudaGlCapture::Status::Ok:
      return GpuCapture::Status::Ok;
    case CudaGlCapture::Status::NotReady:
      return GpuCapture::Status::NotReady;
    case CudaGlCapture::Status::Unavailable:
      return GpuCapture::Status::Unavailable;
    case CudaGlCapture::Status::Failed:
      break;
  }
  return GpuCapture::Status::Failed;
}

GpuCapture::Status fromD3d11(D3d11EncodeCapture::Status s) {
  switch (s) {
    case D3d11EncodeCapture::Status::Ok:
      return GpuCapture::Status::Ok;
    case D3d11EncodeCapture::Status::NotReady:
      return GpuCapture::Status::NotReady;
    case D3d11EncodeCapture::Status::Unavailable:
      return GpuCapture::Status::Unavailable;
    case D3d11EncodeCapture::Status::Failed:
      break;
  }
  return GpuCapture::Status::Failed;
}

class CudaCapture final : public GpuCapture {
 public:
  explicit CudaCapture(CudaGlCapture::Deps deps) : capture_(std::move(deps)) {}
  CudaCapture() = default;
  Status attach(unsigned tex, int w, int h) override { return fromCuda(capture_.attach(tex, w, h)); }
  void detach(bool glCurrent) override { capture_.detach(glCurrent); }
  std::shared_ptr<AVFrame> capture() override { return capture_.capture(); }
  QString reason() const override { return capture_.reason(); }
  Backend backend() const override { return Backend::Cuda; }

 private:
  CudaGlCapture capture_;
};

class D3d11Capture final : public GpuCapture {
 public:
  explicit D3d11Capture(D3d11EncodeCapture::BridgeProvider provider) : capture_(std::move(provider)) {}
  Status attach(unsigned tex, int w, int h) override { return fromD3d11(capture_.attach(tex, w, h)); }
  void detach(bool glCurrent) override { capture_.detach(glCurrent); }
  std::shared_ptr<AVFrame> capture() override { return capture_.capture(); }
  QString reason() const override { return capture_.reason(); }
  Backend backend() const override { return capture_.kind() == D3d11EncodeCapture::Kind::Qsv ? Backend::Qsv : Backend::Amf; }

 private:
  D3d11EncodeCapture capture_;
};

QString currentGlVendor() {
  QOpenGLContext* ctx = QOpenGLContext::currentContext();
  const auto* v = ctx ? reinterpret_cast<const char*>(ctx->functions()->glGetString(GL_VENDOR)) : nullptr;
  return QString::fromLatin1(v ? v : "");
}

}  // namespace

GpuEncodeBridge::GpuEncodeBridge(const QSize& maxSize) : GpuEncodeBridge(maxSize, GpuEncodeOptions{GpuEncodeAllowed{true, false, false}, {}, {}, {}, {}}) {}

GpuEncodeBridge::GpuEncodeBridge(const QSize& maxSize, CudaGlCapture::Deps deps)
    : GpuEncodeBridge(maxSize, GpuEncodeOptions{GpuEncodeAllowed{true, false, false},
                                                {},
                                                [deps]() -> std::unique_ptr<GpuCapture> { return std::make_unique<CudaCapture>(deps); },
                                                {},
                                                {}}) {}

GpuEncodeBridge::GpuEncodeBridge(const QSize& maxSize, GpuEncodeOptions options) : options_(std::move(options)), maxSize_(maxSize) {}

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

emu::GpuEncodeTarget::Attach GpuEncodeBridge::finish(GpuCapture::Status status, const QString& reason) {
  switch (status) {
    case GpuCapture::Status::Ok:
      return Attach::Ok;
    case GpuCapture::Status::NotReady:
      return Attach::NotReady;
    case GpuCapture::Status::Unavailable:
      transition(State::Unavailable, reason);
      return Attach::Unavailable;
    case GpuCapture::Status::Failed:
      transition(State::Failed, reason);
      return Attach::Failed;
  }
  return Attach::Failed;
}

emu::GpuEncodeTarget::Attach GpuEncodeBridge::selectAndAttach(unsigned texture, int width, int height) {
  const GpuEncodeAllowed& allowed = options_.allowed;
  const bool d3d11Allowed = allowed.amf || allowed.qsv;
  bool tryD3d11 = d3d11Allowed;
  if (tryD3d11 && allowed.cuda) {
    // An NVIDIA GL context goes to CUDA without touching D3D11 (ADR 0019 stays as it was there).
    const QString vendor = options_.glVendor ? options_.glVendor() : currentGlVendor();
    tryD3d11 = !vendor.contains(QLatin1String("NVIDIA"), Qt::CaseInsensitive);
  }
  QString d3d11Reason;
  bool d3d11Failed = false;
  if (tryD3d11) {
    std::unique_ptr<GpuCapture> d3d11 = std::move(pendingD3d11_);
    if (!d3d11) {
      d3d11 = options_.makeD3d11 ? options_.makeD3d11(options_.displayBridge) : std::make_unique<D3d11Capture>(options_.displayBridge);
    }
    const GpuCapture::Status st = d3d11->attach(texture, width, height);
    if (st == GpuCapture::Status::NotReady) {
      pendingD3d11_ = std::move(d3d11);  // the bridge is not open yet: the whole choice runs again on a later frame
      return Attach::NotReady;
    }
    if (st == GpuCapture::Status::Ok) {
      const GpuCapture::Backend b = d3d11->backend();
      if ((b == GpuCapture::Backend::Amf && allowed.amf) || (b == GpuCapture::Backend::Qsv && allowed.qsv)) {
        capture_ = std::move(d3d11);
        return Attach::Ok;
      }
      d3d11->detach(true);
      d3d11Reason = b == GpuCapture::Backend::Amf ? QStringLiteral("h264_amf is not available for GPU-direct encoding")
                                                  : QStringLiteral("h264_qsv is not available for GPU-direct encoding");
    } else {
      d3d11Reason = d3d11->reason();
      d3d11Failed = st == GpuCapture::Status::Failed;
      d3d11->detach(true);
    }
  }
  if (allowed.cuda) {
    capture_ = options_.makeCuda ? options_.makeCuda() : std::make_unique<CudaCapture>();
    const GpuCapture::Status st = capture_->attach(texture, width, height);
    if (st == GpuCapture::Status::Unavailable && d3d11Failed) {
      return finish(GpuCapture::Status::Failed, d3d11Reason);  // the real failure, not "not an NVIDIA GPU"
    }
    return finish(st, capture_->reason());
  }
  return finish(d3d11Failed ? GpuCapture::Status::Failed : GpuCapture::Status::Unavailable,
                d3d11Reason.isEmpty() ? QStringLiteral("no GPU-direct encoder is available") : d3d11Reason);
}

emu::GpuEncodeTarget::Attach GpuEncodeBridge::attach(unsigned texture, int width, int height) {
  if (capture_) {
    const GpuCapture::Status st = capture_->attach(texture, width, height);  // before reason(): argument order is unspecified
    return finish(st, capture_->reason());
  }
  return selectAndAttach(texture, width, height);
}

void GpuEncodeBridge::detach(bool glCurrent) {
  if (pendingD3d11_) {
    pendingD3d11_->detach(glCurrent);
    pendingD3d11_.reset();
  }
  if (capture_) {
    capture_->detach(glCurrent);
  }
}

bool GpuEncodeBridge::capture() {
  std::shared_ptr<AVFrame> frame = capture_ ? capture_->capture() : nullptr;
  if (!frame) {
    const QString why = capture_ ? capture_->reason() : QString();
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
