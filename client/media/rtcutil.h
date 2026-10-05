#pragma once

// Helpers shared by SessionHost and SessionViewer (libdatachannel callbacks run on its own threads).

#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <rtc/rtc.hpp>
#include <string>
#include <vector>

namespace framebeam {

// Posts work to the Qt thread of an object from foreign threads; after detach() nothing is posted any more
// (waits for a post in flight), so callbacks never touch a destroyed object.
class ThreadBridge {
 public:
  explicit ThreadBridge(QObject* target) : target_(target) {}
  void detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    target_ = nullptr;
  }
  template <typename F>
  void post(F&& fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (target_ != nullptr) {
      QMetaObject::invokeMethod(target_, std::forward<F>(fn), Qt::QueuedConnection);
    }
  }

 private:
  std::mutex mutex_;
  QObject* target_;
};

// Wraps a libdatachannel callback: an exception escaping into a library thread would terminate the process
// (seen as a silent abort on Windows CI), so it is logged instead.
template <typename F>
auto guarded(const char* what, F fn) {
  return [what, fn = std::move(fn)](auto&&... args) mutable {
    try {
      fn(std::forward<decltype(args)>(args)...);
    } catch (const std::exception& e) {
      qWarning("[media] exception in %s callback: %s", what, e.what());
    } catch (...) {
      qWarning("[media] unknown exception in %s callback", what);
    }
  };
}

inline rtc::binary toBinary(const uint8_t* data, size_t size) {
  rtc::binary b(size);
  if (size > 0) {
    std::memcpy(b.data(), data, size);
  }
  return b;
}

// host | srflx | relay of the selected candidate pair; "unknown" while none is selected.
inline QString connectionTypeOf(rtc::PeerConnection& pc) {
  rtc::Candidate local, remote;
  if (!pc.getSelectedCandidatePair(&local, &remote)) {
    return QStringLiteral("unknown");
  }
  using T = rtc::Candidate::Type;
  const T a = local.type(), b = remote.type();
  if (a == T::Relayed || b == T::Relayed) {
    return QStringLiteral("relay");
  }
  if (a == T::ServerReflexive || b == T::ServerReflexive || a == T::PeerReflexive || b == T::PeerReflexive) {
    return QStringLiteral("srflx");
  }
  return QStringLiteral("host");
}

inline std::optional<double> rttOf(rtc::PeerConnection& pc) {
  const auto r = pc.rtt();
  if (!r) {
    return std::nullopt;
  }
  return static_cast<double>(r->count());
}

inline rtc::Configuration makeRtcConfig(const QStringList& iceServers) {
  rtc::Configuration cfg;  // no servers: host candidates only (ADR 0006 D4)
  for (const QString& s : iceServers) {
    if (s.startsWith(QLatin1String("stun:")) || s.startsWith(QLatin1String("stuns:"))) {
      cfg.iceServers.emplace_back(s.toStdString());
    }
  }
  return cfg;
}

}  // namespace framebeam
