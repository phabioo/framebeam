#pragma once

#include <QLoggingCategory>
#include <QString>
#include <atomic>
#include <memory>
#include <mutex>

namespace framebeam {

Q_DECLARE_LOGGING_CATEGORY(lcGpu)  // framebeam.gpuencode: GPU-direct encoding (ADR 0019)

namespace cuda {

struct Api;

// Process-wide loader of the CUDA driver (nvcuda.dll / libcuda.so.1) for GPU-direct encoding (ADR 0019). The driver is
// loaded at runtime and never unloaded; no CUDA SDK is needed (cuda_shim.h declares the 18 functions used).
class Driver {
 public:
  enum class State { Idle, Loading, Ready, Unavailable, Dead };

  // Process-wide; created on first use and intentionally never destroyed (no CUDA call in static destructors; the
  // library is never unloaded while contexts may exist).
  static Driver& instance();
  // Any thread; the first call runs load() on QThreadPool::globalInstance(). Later calls do nothing.
  void loadAsync();
  // The same load on the calling thread (std::call_once shared with loadAsync); true = Ready.
  bool loadBlocking();
  State state() const { return state_.load(std::memory_order_acquire); }  // atomic
  QString reason() const;                                                 // why Unavailable/Dead (mutex)
  // Valid in Ready and Dead (cleanup calls still go through it); members may be null when the driver never loaded.
  const Api& api() const;
  // Fatal CUresult: GPU-direct encoding and h264_nvenc are off for the process. Logs once. Thread-safe; works in
  // any state.
  void markDead(int result, const char* where);
  static bool isFatal(int result);      // 226, 700, 702, 714-719, 721, 810, 911
  static QString describe(int result);  // "CUDA_ERROR_ILLEGAL_ADDRESS (700)"; unknown: "CUDA error 12345"
  // Pure loader (tests): opens `library` the same way load() does, resolves all symbols into *api, no cuInit. The
  // library handle is never closed. *why: "<library> not found" or "<symbol> missing".
  static bool resolve(const char* library, Api* api, QString* why);
  // Tests only: state back to what it was before markDead; never in product code.
  void resetForTest();

 private:
  Driver();
  ~Driver();
  void load();

  std::atomic<State> state_{State::Idle};
  std::once_flag once_;
  mutable std::mutex mutex_;  // guards reason_, savedReason_, savedState_
  QString reason_;
  State savedState_ = State::Idle;
  QString savedReason_;
  std::unique_ptr<Api> api_;  // written by load() before Ready is published
};

}  // namespace cuda
}  // namespace framebeam
