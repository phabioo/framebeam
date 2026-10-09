#include "cudadriver.h"

#include "cuda_shim.h"

#include <QElapsedTimer>
#include <QThreadPool>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace framebeam {

Q_LOGGING_CATEGORY(lcGpu, "framebeam.gpuencode")

namespace cuda {

namespace {

#ifdef _WIN32
constexpr const char* kLibrary = "nvcuda.dll";
#else
constexpr const char* kLibrary = "libcuda.so.1";
#endif

// Names from cuda.h 12.x for the codes the driver reports in this feature's paths.
struct CodeName {
  int code;
  const char* name;
};
constexpr CodeName kCodeNames[] = {
    {0, "CUDA_SUCCESS"},
    {1, "CUDA_ERROR_INVALID_VALUE"},
    {2, "CUDA_ERROR_OUT_OF_MEMORY"},
    {3, "CUDA_ERROR_NOT_INITIALIZED"},
    {4, "CUDA_ERROR_DEINITIALIZED"},
    {34, "CUDA_ERROR_STUB_LIBRARY"},
    {46, "CUDA_ERROR_DEVICE_UNAVAILABLE"},
    {100, "CUDA_ERROR_NO_DEVICE"},
    {101, "CUDA_ERROR_INVALID_DEVICE"},
    {200, "CUDA_ERROR_INVALID_IMAGE"},
    {201, "CUDA_ERROR_INVALID_CONTEXT"},
    {202, "CUDA_ERROR_CONTEXT_ALREADY_CURRENT"},
    {205, "CUDA_ERROR_MAP_FAILED"},
    {206, "CUDA_ERROR_UNMAP_FAILED"},
    {207, "CUDA_ERROR_ARRAY_IS_MAPPED"},
    {208, "CUDA_ERROR_ALREADY_MAPPED"},
    {209, "CUDA_ERROR_NO_BINARY_FOR_GPU"},
    {210, "CUDA_ERROR_ALREADY_ACQUIRED"},
    {211, "CUDA_ERROR_NOT_MAPPED"},
    {212, "CUDA_ERROR_NOT_MAPPED_AS_ARRAY"},
    {214, "CUDA_ERROR_ECC_UNCORRECTABLE"},
    {219, "CUDA_ERROR_INVALID_GRAPHICS_CONTEXT"},
    {226, "CUDA_ERROR_CONTAINED"},
    {304, "CUDA_ERROR_OPERATING_SYSTEM"},
    {400, "CUDA_ERROR_INVALID_HANDLE"},
    {401, "CUDA_ERROR_ILLEGAL_STATE"},
    {600, "CUDA_ERROR_NOT_READY"},
    {700, "CUDA_ERROR_ILLEGAL_ADDRESS"},
    {702, "CUDA_ERROR_LAUNCH_TIMEOUT"},
    {708, "CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE"},
    {709, "CUDA_ERROR_CONTEXT_IS_DESTROYED"},
    {714, "CUDA_ERROR_HARDWARE_STACK_ERROR"},
    {715, "CUDA_ERROR_ILLEGAL_INSTRUCTION"},
    {716, "CUDA_ERROR_MISALIGNED_ADDRESS"},
    {717, "CUDA_ERROR_INVALID_ADDRESS_SPACE"},
    {718, "CUDA_ERROR_INVALID_PC"},
    {719, "CUDA_ERROR_LAUNCH_FAILED"},
    {721, "CUDA_ERROR_TENSOR_MEMORY_LEAK"},
    {801, "CUDA_ERROR_NOT_SUPPORTED"},
    {802, "CUDA_ERROR_SYSTEM_NOT_READY"},
    {803, "CUDA_ERROR_SYSTEM_DRIVER_MISMATCH"},
    {804, "CUDA_ERROR_COMPAT_NOT_SUPPORTED_ON_DEVICE"},
    {810, "CUDA_ERROR_MPS_CLIENT_TERMINATED"},
    {911, "CUDA_ERROR_EXTERNAL_DEVICE"},
    {999, "CUDA_ERROR_UNKNOWN"},
};

// The handle is never closed: FFmpeg's own loader later gets the same, already loaded module.
void* openLibrary(const char* name) {
#ifdef _WIN32
  // System32 only: the app directory must never supply nvcuda.dll (DLL planting).
  return static_cast<void*>(LoadLibraryExW(QString::fromUtf8(name).toStdWString().c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
#else
  return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

template <typename Fn>
bool loadSymbol(void* lib, const char* name, Fn* out) {
#ifdef _WIN32
  *out = reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
  *out = reinterpret_cast<Fn>(dlsym(lib, name));
#endif
  return *out != nullptr;
}

enum class Resolved { Ok, NotFound, Missing };

Resolved resolveAll(const char* library, Api* api, QString* why) {
  void* lib = openLibrary(library);
  if (!lib) {
    *why = QStringLiteral("%1 not found").arg(QLatin1String(library));
    return Resolved::NotFound;
  }
  Api found;
#define FB_CUDA_RESOLVE(name, params)                                              \
  if (!loadSymbol(lib, #name, &found.name)) {                                      \
    *why = QStringLiteral("%1 missing").arg(QLatin1String(#name));                 \
    return Resolved::Missing;                                                      \
  }
  FB_CUDA_SYMBOLS(FB_CUDA_RESOLVE)
#undef FB_CUDA_RESOLVE
  *api = found;
  return Resolved::Ok;
}

}  // namespace

Driver::Driver() : api_(std::make_unique<Api>()) {}
Driver::~Driver() = default;

Driver& Driver::instance() {
  static Driver* const inst = new Driver;  // never destroyed, see the header
  return *inst;
}

void Driver::loadAsync() {
  State expected = State::Idle;
  if (!state_.compare_exchange_strong(expected, State::Loading)) {
    return;
  }
  QThreadPool::globalInstance()->start([this] { std::call_once(once_, [this] { load(); }); });
}

bool Driver::loadBlocking() {
  std::call_once(once_, [this] { load(); });
  return state() == State::Ready;
}

QString Driver::reason() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return reason_;
}

const Api& Driver::api() const { return *api_; }

void Driver::load() {
  State expected = State::Idle;
  state_.compare_exchange_strong(expected, State::Loading);  // loadAsync() did this already
  const auto finish = [this](State to, const QString& why) {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      reason_ = why;
    }
    State loading = State::Loading;
    state_.compare_exchange_strong(loading, to, std::memory_order_release);  // stays Dead when markDead() came first
  };
  const auto unavailable = [&finish](const QString& why) {
    qCInfo(lcGpu).noquote() << QStringLiteral("CUDA unavailable: %1").arg(why);
    finish(State::Unavailable, why);
  };

  QString why;
  Api api;
  switch (resolveAll(kLibrary, &api, &why)) {
    case Resolved::NotFound:
      unavailable(QStringLiteral("no NVIDIA CUDA driver (%1 not found)").arg(QLatin1String(kLibrary)));
      return;
    case Resolved::Missing:
      unavailable(QStringLiteral("CUDA driver too old: %1").arg(why));
      return;
    case Resolved::Ok:
      break;
  }
  *api_ = api;

  QElapsedTimer timer;
  timer.start();
  const CUresult init = api.cuInit(0);
  const qint64 initMs = timer.elapsed();
  if (init != kSuccess) {
    unavailable(QStringLiteral("cuInit failed: %1").arg(describe(init)));
    return;
  }
  int version = 0;
  api.cuDriverGetVersion(&version);
  qCInfo(lcGpu).noquote() << QStringLiteral("CUDA driver ready: %1.%2 (cuInit %3 ms)")
                                 .arg(version / 1000)
                                 .arg((version % 1000) / 10)
                                 .arg(initMs);
  finish(State::Ready, QString());
}

void Driver::markDead(int result, const char* where) {
  const QString step = QLatin1String(where ? where : "?");
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.load() == State::Dead) {
      return;  // logged once
    }
    savedState_ = state_.load();
    savedReason_ = reason_;
    reason_ = QStringLiteral("%1 at %2").arg(describe(result), step);
    state_.store(State::Dead, std::memory_order_release);
  }
  qCWarning(lcGpu).noquote() << QStringLiteral(
                                    "CUDA error %1 at %2 is fatal; GPU-direct encoding and h264_nvenc stay off until the "
                                    "Player restarts")
                                    .arg(describe(result), step);
}

void Driver::resetForTest() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_.load() != State::Dead) {
    return;
  }
  reason_ = savedReason_;
  state_.store(savedState_ == State::Loading ? State::Idle : savedState_, std::memory_order_release);
}

bool Driver::isFatal(int result) {
  switch (result) {
    case 226:
    case 700:
    case 702:
    case 714:
    case 715:
    case 716:
    case 717:
    case 718:
    case 719:
    case 721:
    case 810:
    case 911:
      return true;
    default:
      return false;
  }
}

QString Driver::describe(int result) {
  for (const CodeName& n : kCodeNames) {
    if (n.code == result) {
      return QStringLiteral("%1 (%2)").arg(QLatin1String(n.name)).arg(result);
    }
  }
  return QStringLiteral("CUDA error %1").arg(result);
}

bool Driver::resolve(const char* library, Api* api, QString* why) {
  QString reason;
  return resolveAll(library, api, why ? why : &reason) == Resolved::Ok;
}

}  // namespace cuda
}  // namespace framebeam
