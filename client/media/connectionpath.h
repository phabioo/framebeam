#pragma once

// Pure helpers for the ICE path of one connection: candidate counts per type and the Direct/Relayed classification with
// the reason for a relayed path (0.10.x "Connections"). No libdatachannel and no Qt GUI dependency, so unit tests and the
// UI can use it.

#include <QString>

namespace framebeam {

// Candidates of one side by type (host / server-reflexive / peer-reflexive / relayed).
struct CandidateCounts {
  int host = 0;
  int srflx = 0;
  int prflx = 0;
  int relay = 0;
  int total() const { return host + srflx + prflx + relay; }
  // Compact log form: "host=2 srflx=1 prflx=0 relay=1".
  QString text() const { return QStringLiteral("host=%1 srflx=%2 prflx=%3 relay=%4").arg(host).arg(srflx).arg(prflx).arg(relay); }
};

// Why a connection runs over the TURN relay instead of directly. None for a direct or not yet known path.
enum class RelayReason {
  None,
  Forced,         // --force-relay / FRAMEBEAM_FORCE_RELAY=1: only relayed candidates are used
  NoLocalSrflx,   // this Player got no server-reflexive candidate: no STUN server reachable or none configured
  NoRemoteSrflx,  // the other Player offered no server-reflexive candidate
  DirectFailed    // both sides had srflx candidates, direct checks still failed: symmetric/carrier-grade NAT or a firewall
};

// `connectionType` is the text of the selected pair ("direct (...)", "relay (udp|tcp)", empty = no pair selected yet).
// Relayed = relay on either side of the pair. Empty type: not known yet, reason None.
inline bool isRelayedType(const QString& connectionType) { return connectionType.startsWith(QLatin1String("relay")); }

inline RelayReason classifyRelayReason(const QString& connectionType, bool forcedRelay, const CandidateCounts& local,
                                       const CandidateCounts& remote) {
  if (!isRelayedType(connectionType)) {
    return RelayReason::None;
  }
  if (forcedRelay) {
    return RelayReason::Forced;
  }
  if (local.srflx == 0) {
    return RelayReason::NoLocalSrflx;
  }
  if (remote.srflx == 0) {
    return RelayReason::NoRemoteSrflx;
  }
  return RelayReason::DirectFailed;
}

// Stable token for logs and tests ("forced", "no_local_srflx", ...); empty for None.
inline QString relayReasonToken(RelayReason r) {
  switch (r) {
    case RelayReason::Forced: return QStringLiteral("forced");
    case RelayReason::NoLocalSrflx: return QStringLiteral("no_local_srflx");
    case RelayReason::NoRemoteSrflx: return QStringLiteral("no_remote_srflx");
    case RelayReason::DirectFailed: return QStringLiteral("direct_failed");
    case RelayReason::None: break;
  }
  return {};
}

}  // namespace framebeam
