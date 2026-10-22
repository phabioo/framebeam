#pragma once
// Qt implementation of the network probes of the connection check (steps 1 to 3) and the small STUN/TURN codec they use
// (RFC 5389 binding, RFC 5766 allocate with the long-term credential mechanism the Hub's TURN uses). Step 5 (WebRTC) is
// not here: media/connectiontest.h provides it, QtConnectionProbes takes it as a function so this library needs no libdatachannel.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <functional>
#include <memory>
#include <optional>

#include "connectioncheck.h"

namespace uzume {

namespace stun {

inline constexpr quint32 kMagicCookie = 0x2112A442;
inline constexpr quint16 kBindingRequest = 0x0001, kBindingSuccess = 0x0101;
inline constexpr quint16 kAllocateRequest = 0x0003, kAllocateSuccess = 0x0103, kAllocateError = 0x0113;
inline constexpr quint16 kRefreshRequest = 0x0004;
inline constexpr quint16 kAttrMapped = 0x0001, kAttrUsername = 0x0006, kAttrIntegrity = 0x0008, kAttrError = 0x0009, kAttrRealm = 0x0014,
                         kAttrNonce = 0x0015, kAttrXorRelayed = 0x0016, kAttrRequestedTransport = 0x0019, kAttrLifetime = 0x000D,
                         kAttrXorMapped = 0x0020;

struct Message {
  quint16 type = 0;
  QByteArray txid;  // 12 bytes
  QList<QPair<quint16, QByteArray>> attrs;
  QByteArray attr(quint16 t) const;  // first attribute of that type, empty if absent
  bool has(quint16 t) const;
};

QByteArray newTransactionId();
// Builds a message. A non-empty `integrityKey` appends MESSAGE-INTEGRITY (HMAC-SHA1 over the message so far).
QByteArray build(quint16 type, const QByteArray& txid, const QList<QPair<quint16, QByteArray>>& attrs, const QByteArray& integrityKey = {});
std::optional<Message> parse(const QByteArray& datagram);
bool verifyIntegrity(const QByteArray& datagram, const QByteArray& key);
// MD5(username:realm:password), the long-term credential key.
QByteArray longTermKey(const QString& username, const QString& realm, const QString& password);
// XOR-MAPPED-ADDRESS / XOR-RELAYED-ADDRESS (or plain MAPPED-ADDRESS) as "ip:port"; empty when absent or malformed (IPv4 only).
QString addressOf(const Message& m, quint16 xorAttr);
// ERROR-CODE value as number (401, 438, ...), 0 when absent.
int errorCode(const Message& m);
// Test helper: XOR-MAPPED-ADDRESS attribute value for an IPv4 address.
QByteArray xorAddressValue(const QHostAddress& a, quint16 port);

}  // namespace stun

// "stun:host[:port]" / "turn:host[:port]?transport=udp" -> host and port (default 3478); false for a URL that is not usable.
struct IceEndpoint {
  QString host;
  int port = 3478;
  bool tcp = false;  // turn transport=tcp or turns:
  QString text() const { return QStringLiteral("%1:%2").arg(host).arg(port); }
};
std::optional<IceEndpoint> parseIceUrl(const QString& url);

// Function of the WebRTC test (step 5); empty = "not available in this build" (reported as a failed step).
using ConnectionTestFn = std::function<void(const ConnectionTarget&, const TestPlan&, std::function<void(const TestProbeResult&)>)>;
using ConnectionTestCancelFn = std::function<void()>;

struct ProbeTimings {
  int hubTimeoutMs = 10000;
  int stunTimeoutMs = 3000;
  int turnTimeoutMs = 4000;
};

// Real network probes. Create and use on the thread that runs the ConnectionCheck (it needs an event loop there).
std::unique_ptr<ConnectionProbes> makeQtConnectionProbes(ConnectionTestFn test, ConnectionTestCancelFn cancelTest, const ProbeTimings& timings = {});

}  // namespace uzume
