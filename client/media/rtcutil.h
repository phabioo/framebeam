#pragma once

// Helpers shared by SessionHost and SessionViewer (libdatachannel callbacks run on its own threads).

#include <QDebug>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <rtc/rtc.hpp>
#include <string>
#include <vector>

#include "sessiontypes.h"

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

// Connection type of the selected candidate pair: "direct (host)", "direct (srflx)", "direct (prflx)",
// "relay (udp)", "relay (tcp)" (ADR 0012 D5); empty while no pair is selected. The relay protocol is the transport of the
// relayed candidate when libdatachannel reports it as TCP; `relayTcpOnly` (the Player only offered TCP TURN URLs)
// covers libjuice reporting relayed candidates as UDP regardless of the client-to-TURN transport.
inline QString connectionTypeOfCandidates(const rtc::Candidate& local, const rtc::Candidate& remote, bool relayTcpOnly = false,
                                          bool forcedRelay = false) {
  using T = rtc::Candidate::Type;
  using X = rtc::Candidate::TransportType;
  const T a = local.type(), b = remote.type();
  // With the relay-only policy only relayed candidates are exchanged: a peer-reflexive/server-reflexive remote candidate
  // is then just the relay address as seen on the other side, the path is relayed.
  if (a == T::Relayed || b == T::Relayed || (forcedRelay && (a != T::Unknown || b != T::Unknown))) {
    const rtc::Candidate& relayed = a == T::Relayed ? local : (b == T::Relayed ? remote : local);
    const X x = relayed.transportType();
    const bool tcp = x == X::TcpActive || x == X::TcpPassive || x == X::TcpSo || x == X::TcpUnknown || relayTcpOnly;
    return tcp ? QStringLiteral("relay (tcp)") : QStringLiteral("relay (udp)");
  }
  if (a == T::ServerReflexive || b == T::ServerReflexive) {
    return QStringLiteral("direct (srflx)");
  }
  if (a == T::PeerReflexive || b == T::PeerReflexive) {
    return QStringLiteral("direct (prflx)");
  }
  if (a == T::Host || b == T::Host) {
    return QStringLiteral("direct (host)");
  }
  return {};
}

inline QString connectionTypeOf(rtc::PeerConnection& pc, bool relayTcpOnly = false, bool forcedRelay = false) {
  rtc::Candidate local, remote;
  if (!pc.getSelectedCandidatePair(&local, &remote)) {
    return {};
  }
  return connectionTypeOfCandidates(local, remote, relayTcpOnly, forcedRelay);
}

// Sort key for "worst" connection type over several links: relay > srflx/prflx > host > unknown.
inline int connectionTypeRank(const QString& type) {
  return type.startsWith(QLatin1String("relay")) ? 3 : (type == QLatin1String("direct (srflx)") || type == QLatin1String("direct (prflx)")) ? 2
         : type == QLatin1String("direct (host)") ? 1 : 0;
}

inline std::optional<double> rttOf(rtc::PeerConnection& pc) {
  const auto r = pc.rtt();
  if (!r) {
    return std::nullopt;
  }
  return static_cast<double>(r->count());
}

// FRAMEBEAM_FORCE_RELAY=1: only relayed candidates (tests the TURN path on a LAN, ADR 0012 D5).
inline bool forceRelayFromEnv() {
  const QString v = qEnvironmentVariable("FRAMEBEAM_FORCE_RELAY").trimmed();
  return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// "turn:host:port?transport=udp|tcp" (or turns:) to an rtc::IceServer with credentials; nullopt if malformed.
inline std::optional<rtc::IceServer> turnIceServer(const QString& url, const QString& username, const QString& credential) {
  const bool tls = url.startsWith(QLatin1String("turns:"));
  if (!tls && !url.startsWith(QLatin1String("turn:"))) {
    return std::nullopt;
  }
  QString rest = url.mid(tls ? 6 : 5);
  QString query;
  if (const qsizetype q = rest.indexOf(QLatin1Char('?')); q >= 0) {
    query = rest.mid(q + 1);
    rest.truncate(q);
  }
  QString host = rest;
  int port = tls ? 5349 : 3478;
  if (rest.startsWith(QLatin1Char('['))) {  // [v6]:port
    const qsizetype e = rest.indexOf(QLatin1Char(']'));
    if (e < 0) return std::nullopt;
    host = rest.mid(1, e - 1);
    if (e + 1 < rest.size()) {
      if (rest[e + 1] != QLatin1Char(':')) return std::nullopt;
      bool ok = false;
      port = rest.mid(e + 2).toInt(&ok);
      if (!ok) return std::nullopt;
    }
  } else if (const qsizetype c = rest.lastIndexOf(QLatin1Char(':')); c >= 0) {
    host = rest.left(c);
    bool ok = false;
    port = rest.mid(c + 1).toInt(&ok);
    if (!ok) return std::nullopt;
  }
  if (host.isEmpty() || port <= 0 || port > 65535) {
    return std::nullopt;
  }
  using R = rtc::IceServer::RelayType;
  R relay = tls ? R::TurnTls : R::TurnUdp;
  if (!tls && query.contains(QLatin1String("transport=tcp"), Qt::CaseInsensitive)) {
    relay = R::TurnTcp;
  }
  return rtc::IceServer(host.toStdString(), static_cast<uint16_t>(port), username.toStdString(), credential.toStdString(), relay);
}

// True if every TURN URL in the list uses TCP (or TLS): relayed candidates then travel over TCP to the TURN server.
inline bool turnServersTcpOnly(const QList<TurnServer>& turnServers) {
  bool any = false;
  for (const TurnServer& t : turnServers) {
    for (const QString& u : t.urls) {
      any = true;
      if (!u.startsWith(QLatin1String("turns:")) && !u.contains(QLatin1String("transport=tcp"), Qt::CaseInsensitive)) {
        return false;
      }
    }
  }
  return any;
}

// stun: URLs plus TURN servers with credentials (turn_servers); no servers: host candidates only (ADR 0006 D4).
inline rtc::Configuration makeRtcConfig(const QStringList& iceServers, const QList<TurnServer>& turnServers = {},
                                        bool forceRelay = false) {
  rtc::Configuration cfg;
  for (const QString& s : iceServers) {
    if (s.startsWith(QLatin1String("stun:")) || s.startsWith(QLatin1String("stuns:"))) {
      cfg.iceServers.emplace_back(s.toStdString());
    }
  }
  for (const TurnServer& t : turnServers) {
    for (const QString& u : t.urls) {
      if (const auto s = turnIceServer(u, t.username, t.credential)) {
        cfg.iceServers.push_back(*s);
      }
    }
  }
  if (forceRelay) {
    cfg.iceTransportPolicy = rtc::TransportPolicy::Relay;
  }
  return cfg;
}

}  // namespace framebeam
