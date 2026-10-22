#include "connectionprobes.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QMessageAuthenticationCode>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

#include "hubhttp.h"
#include "hubprotocol.h"

namespace framebeam {

// ------------------------------------------------------------------------------------------------------------- codec

namespace stun {

namespace {
void put16(QByteArray& b, quint16 v) {
  b.append(static_cast<char>(v >> 8));
  b.append(static_cast<char>(v & 0xff));
}
void put32(QByteArray& b, quint32 v) {
  put16(b, static_cast<quint16>(v >> 16));
  put16(b, static_cast<quint16>(v & 0xffff));
}
quint16 get16(const QByteArray& b, qsizetype p) {
  return static_cast<quint16>((static_cast<quint8>(b[p]) << 8) | static_cast<quint8>(b[p + 1]));
}
quint32 get32(const QByteArray& b, qsizetype p) { return (static_cast<quint32>(get16(b, p)) << 16) | get16(b, p + 2); }
void setLength(QByteArray& b, quint16 len) {
  b[2] = static_cast<char>(len >> 8);
  b[3] = static_cast<char>(len & 0xff);
}
}  // namespace

QByteArray Message::attr(quint16 t) const {
  for (const auto& a : attrs) {
    if (a.first == t) return a.second;
  }
  return {};
}
bool Message::has(quint16 t) const {
  for (const auto& a : attrs) {
    if (a.first == t) return true;
  }
  return false;
}

QByteArray newTransactionId() {
  QByteArray id(12, 0);
  for (int i = 0; i < 12; ++i) id[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
  return id;
}

QByteArray build(quint16 type, const QByteArray& txid, const QList<QPair<quint16, QByteArray>>& attrs, const QByteArray& integrityKey) {
  QByteArray m;
  put16(m, type);
  put16(m, 0);
  put32(m, kMagicCookie);
  m.append(txid);
  for (const auto& a : attrs) {
    put16(m, a.first);
    put16(m, static_cast<quint16>(a.second.size()));
    m.append(a.second);
    while (m.size() % 4 != 0) m.append('\0');
  }
  if (!integrityKey.isEmpty()) {
    setLength(m, static_cast<quint16>(m.size() - 20 + 24));  // the length counts MESSAGE-INTEGRITY itself
    const QByteArray mac = QMessageAuthenticationCode::hash(m, integrityKey, QCryptographicHash::Sha1);
    put16(m, kAttrIntegrity);
    put16(m, 20);
    m.append(mac);
  }
  setLength(m, static_cast<quint16>(m.size() - 20));
  return m;
}

std::optional<Message> parse(const QByteArray& d) {
  if (d.size() < 20 || (static_cast<quint8>(d[0]) & 0xc0) != 0 || get32(d, 4) != kMagicCookie) return std::nullopt;
  const qsizetype len = get16(d, 2);
  if (len % 4 != 0 || 20 + len > d.size()) return std::nullopt;
  Message m;
  m.type = get16(d, 0);
  m.txid = d.mid(8, 12);
  qsizetype p = 20;
  while (p + 4 <= 20 + len) {
    const quint16 t = get16(d, p);
    const qsizetype l = get16(d, p + 2);
    p += 4;
    if (p + l > 20 + len) return std::nullopt;
    m.attrs.append({t, d.mid(p, l)});
    p += (l + 3) & ~qsizetype(3);
  }
  return m;
}

bool verifyIntegrity(const QByteArray& d, const QByteArray& key) {
  qsizetype p = 20;
  const qsizetype end = 20 + get16(d, 2);
  while (p + 4 <= end) {
    const quint16 t = get16(d, p);
    const qsizetype l = get16(d, p + 2);
    if (t == kAttrIntegrity && l == 20 && p + 24 <= d.size()) {
      QByteArray head = d.left(p);
      setLength(head, static_cast<quint16>(p - 20 + 24));
      return QMessageAuthenticationCode::hash(head, key, QCryptographicHash::Sha1) == d.mid(p + 4, 20);
    }
    p += 4 + ((l + 3) & ~qsizetype(3));
  }
  return false;
}

QByteArray longTermKey(const QString& username, const QString& realm, const QString& password) {
  return QCryptographicHash::hash((username + QLatin1Char(':') + realm + QLatin1Char(':') + password).toUtf8(), QCryptographicHash::Md5);
}

QString addressOf(const Message& m, quint16 xorAttr) {
  QByteArray v = m.attr(xorAttr);
  bool xored = true;
  if (v.isEmpty() && xorAttr == kAttrXorMapped) {
    v = m.attr(kAttrMapped);
    xored = false;
  }
  if (v.size() < 8 || static_cast<quint8>(v[1]) != 1) return {};  // IPv4 only
  quint16 port = get16(v, 2);
  quint32 addr = get32(v, 4);
  if (xored) {
    port ^= static_cast<quint16>(kMagicCookie >> 16);
    addr ^= kMagicCookie;
  }
  return QStringLiteral("%1:%2").arg(QHostAddress(addr).toString()).arg(port);
}

int errorCode(const Message& m) {
  const QByteArray v = m.attr(kAttrError);
  if (v.size() < 4) return 0;
  return (static_cast<quint8>(v[2]) & 7) * 100 + static_cast<quint8>(v[3]);
}

QByteArray xorAddressValue(const QHostAddress& a, quint16 port) {
  QByteArray v;
  v.append('\0');
  v.append('\1');
  put16(v, static_cast<quint16>(port ^ (kMagicCookie >> 16)));
  put32(v, a.toIPv4Address() ^ kMagicCookie);
  return v;
}

}  // namespace stun

std::optional<IceEndpoint> parseIceUrl(const QString& url) {
  const int colon = url.indexOf(QLatin1Char(':'));
  if (colon <= 0) return std::nullopt;
  const QString scheme = url.left(colon).toLower();
  if (scheme != QLatin1String("stun") && scheme != QLatin1String("stuns") && scheme != QLatin1String("turn") && scheme != QLatin1String("turns")) return std::nullopt;
  QString rest = url.mid(colon + 1);
  const int q = rest.indexOf(QLatin1Char('?'));
  const QString query = q < 0 ? QString() : rest.mid(q + 1);
  if (q >= 0) rest = rest.left(q);
  IceEndpoint e;
  e.tcp = scheme == QLatin1String("turns") || scheme == QLatin1String("stuns") || query.contains(QLatin1String("transport=tcp"), Qt::CaseInsensitive);
  if (rest.startsWith(QLatin1Char('['))) return std::nullopt;  // IPv6 literals are not probed
  const int c = rest.lastIndexOf(QLatin1Char(':'));
  if (c >= 0) {
    bool ok = false;
    e.port = rest.mid(c + 1).toInt(&ok);
    if (!ok || e.port <= 0 || e.port > 65535) return std::nullopt;
    e.host = rest.left(c);
  } else {
    e.host = rest;
    e.port = (scheme == QLatin1String("turns") || scheme == QLatin1String("stuns")) ? 5349 : 3478;
  }
  if (e.host.isEmpty()) return std::nullopt;
  return e;
}

namespace {

using stun::Message;

// Base of a one-shot job: calls `done` once, then deletes itself.
template <typename R>
class Job : public QObject {
 public:
  Job(std::function<void(const R&)> done, QObject* parent) : QObject(parent), done_(std::move(done)) { clock_.start(); }
  int elapsedMs() const { return static_cast<int>(clock_.elapsed()); }

 protected:
  void finish(const R& r) {
    if (finished_) return;
    finished_ = true;
    auto cb = std::move(done_);
    deleteLater();
    if (cb) cb(r);
  }

 private:
  std::function<void(const R&)> done_;
  QElapsedTimer clock_;
  bool finished_ = false;
};

// ------------------------------------------------------------------------------------------------------- 1 Hub

class HubJob : public Job<HubProbeResult> {
 public:
  HubJob(const ConnectionTarget& t, int timeoutMs, std::function<void(const HubProbeResult&)> done, QObject* parent)
      : Job(std::move(done), parent) {
    const QUrl url(t.hubAddress.contains(QLatin1String("://")) ? t.hubAddress : QStringLiteral("https://") + t.hubAddress);
    if (!url.isValid() || url.host().isEmpty() || !HubHttp::isSchemeAllowed(url, t.allowHttp)) {
      HubProbeResult r;
      r.errorKind = QStringLiteral("other");
      r.error = QStringLiteral("not a usable Hub address");
      QTimer::singleShot(0, this, [this, r]() { finish(r); });
      return;
    }
    http_ = new HubHttp(url, t.pinnedFingerprint, this);
    reply_ = http_->get(QStringLiteral("/.well-known/framebeam"), {}, {}, /*allowUnpinned=*/t.pinnedFingerprint.isEmpty());
    connect(reply_, &QNetworkReply::finished, this, [this]() { onFinished(); });
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this]() {
      // A failure Qt already knows (for example a refused connection that Windows reports only after its SYN retries) is not a
      // timeout: let its finished() signal classify it instead of aborting the reply first.
      if (reply_ == nullptr || reply_->error() != QNetworkReply::NoError) return;
      timedOut_ = true;
      reply_->abort();
    });
    timer_.start(timeoutMs);
  }

 private:
  void onFinished() {
    timer_.stop();
    HubProbeResult out;
    out.latencyMs = elapsedMs();
    const QNetworkReply::NetworkError err = reply_->error();
    if (timedOut_) {  // aborted by our timer: nothing to read from the closed reply
      reply_->deleteLater();
      reply_ = nullptr;
      out.errorKind = QStringLiteral("timeout");
      out.error = QStringLiteral("no answer");
      finish(out);
      return;
    }
    const HttpResult r = HubHttp::resultOf(reply_);
    reply_->deleteLater();
    reply_ = nullptr;
    if (r.certMismatch) {
      out.errorKind = QStringLiteral("certificate");
      out.error = QStringLiteral("certificate does not match the pinned fingerprint");
    } else if (err == QNetworkReply::TimeoutError || err == QNetworkReply::OperationCanceledError) {
      out.errorKind = QStringLiteral("timeout");
      out.error = QStringLiteral("no answer");
    } else if (err == QNetworkReply::HostNotFoundError) {
      out.errorKind = QStringLiteral("dns");
      out.error = r.errorString;
    } else if (err == QNetworkReply::ConnectionRefusedError) {
      out.errorKind = QStringLiteral("refused");
      out.error = r.errorString;
    } else if (r.networkError) {
      out.errorKind = QStringLiteral("other");
      out.error = r.errorString;
    } else if (r.status != 200 || !parseHubInfo(r.json())) {
      out.errorKind = QStringLiteral("http");
      out.error = QStringLiteral("HTTP %1, not a Hub info document").arg(r.status);
    } else {
      out.ok = true;
    }
    finish(out);
  }

  HubHttp* http_ = nullptr;
  QNetworkReply* reply_ = nullptr;
  QTimer timer_;
  bool timedOut_ = false;
};

// ------------------------------------------------------------------------------------------------------- 2 STUN

class StunJob : public Job<StunProbeResult> {
 public:
  StunJob(const QList<IceEndpoint>& dests, int timeoutMs, std::function<void(const StunProbeResult&)> done, QObject* parent)
      : Job(std::move(done), parent), timeoutMs_(timeoutMs) {
    for (const IceEndpoint& e : dests) {
      Dest d;
      d.ep = e;
      dests_.append(d);
    }
    if (dests_.isEmpty()) {
      QTimer::singleShot(0, this, [this]() { finish({}); });
      return;
    }
    pendingLookups_ = static_cast<int>(dests_.size());
    for (int i = 0; i < dests_.size(); ++i) {
      QHostInfo::lookupHost(dests_[i].ep.host, this, [this, i](const QHostInfo& info) {
        for (const QHostAddress& a : info.addresses()) {
          if (a.protocol() == QAbstractSocket::IPv4Protocol) {
            dests_[i].addr = a;
            break;
          }
        }
        if (--pendingLookups_ == 0) begin();
      });
    }
  }

 private:
  struct Dest {
    IceEndpoint ep;
    QHostAddress addr;
    QByteArray txid;
    StunMapping m;
    QElapsedTimer clock;
  };

  void begin() {
    if (!sock_.bind(QHostAddress::AnyIPv4, 0)) {
      done();
      return;
    }
    connect(&sock_, &QUdpSocket::readyRead, this, [this]() { onReadyRead(); });
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this]() { done(); });
    timeout_.start(timeoutMs_);
    retry_.setInterval(500);
    connect(&retry_, &QTimer::timeout, this, [this]() { sendPending(); });
    retry_.start();
    sendPending();
  }

  void sendPending() {
    if (++rounds_ > 4) return;
    for (Dest& d : dests_) {
      if (d.addr.isNull() || d.m.answered()) continue;
      if (d.txid.isEmpty()) d.txid = stun::newTransactionId();
      sock_.writeDatagram(stun::build(stun::kBindingRequest, d.txid, {}), d.addr, static_cast<quint16>(d.ep.port));
      if (!d.clock.isValid()) d.clock.start();
    }
  }

  void onReadyRead() {
    while (sock_.hasPendingDatagrams()) {
      QByteArray data(static_cast<int>(sock_.pendingDatagramSize()), 0);
      sock_.readDatagram(data.data(), data.size());
      const auto msg = stun::parse(data);
      if (!msg || msg->type != stun::kBindingSuccess) continue;
      for (Dest& d : dests_) {
        if (d.txid != msg->txid || d.m.answered()) continue;
        d.m.mapped = stun::addressOf(*msg, stun::kAttrXorMapped);
        d.m.rttMs = d.clock.isValid() ? static_cast<int>(d.clock.elapsed()) : 0;
      }
    }
    for (const Dest& d : dests_) {
      if (!d.addr.isNull() && !d.m.answered()) return;
    }
    done();
  }

  void done() {
    if (doneCalled_) return;
    doneCalled_ = true;
    timeout_.stop();
    retry_.stop();
    QStringList local;
    for (const QHostAddress& a : QNetworkInterface::allAddresses()) local << a.toString();
    StunProbeResult r;
    for (Dest& d : dests_) {
      d.m.server = d.ep.text();
      d.m.local = QStringLiteral("0.0.0.0:%1").arg(sock_.localPort());
      if (d.m.answered()) d.m.behindNat = !(local.contains(d.m.mapped.section(QLatin1Char(':'), 0, -2)) && d.m.mapped.section(QLatin1Char(':'), -1).toInt() == sock_.localPort());
      r.mappings.append(d.m);
    }
    finish(r);
  }

  QList<Dest> dests_;
  QUdpSocket sock_;
  QTimer timeout_, retry_;
  int timeoutMs_;
  int pendingLookups_ = 0;
  int rounds_ = 0;
  bool doneCalled_ = false;
};

// ------------------------------------------------------------------------------------------------------- 3 TURN

class TurnJob : public Job<TurnProbeResult> {
 public:
  TurnJob(const ConnectionTarget& t, int timeoutMs, std::function<void(const TurnProbeResult&)> done, QObject* parent)
      : Job(std::move(done), parent) {
    // First server with a usable URL; UDP preferred.
    for (int pass = 0; pass < 2 && !ep_; ++pass) {
      for (const TurnServer& s : t.turnServers) {
        for (const QString& u : s.urls) {
          const auto e = parseIceUrl(u);
          if (e && e->tcp == (pass == 1) && u.startsWith(QLatin1String("turn"))) {
            ep_ = *e;
            user_ = s.username;
            pass_ = s.credential;
            break;
          }
        }
        if (ep_) break;
      }
    }
    if (!ep_) {
      TurnProbeResult r;
      r.errorKind = QStringLiteral("other");
      r.error = QStringLiteral("no usable TURN URL");
      QTimer::singleShot(0, this, [this, r]() { finish(r); });
      return;
    }
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this]() { fail(QStringLiteral("unreachable"), QStringLiteral("no answer from %1").arg(ep_->text())); });
    timeout_.start(timeoutMs);
    QHostInfo::lookupHost(ep_->host, this, [this](const QHostInfo& info) {
      for (const QHostAddress& a : info.addresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol) {
          addr_ = a;
          break;
        }
      }
      if (addr_.isNull()) {
        fail(QStringLiteral("unreachable"), QStringLiteral("cannot resolve %1").arg(ep_->host));
        return;
      }
      ep_->tcp ? startTcp() : startUdp();
    });
  }

 private:
  void fail(const QString& kind, const QString& why) {
    TurnProbeResult r;
    r.server = ep_ ? ep_->text() : QString();
    r.errorKind = kind;
    r.error = why;
    timeout_.stop();
    finish(r);
  }

  // TCP-only relay: the port answering is all that can be said without speaking TURN over TCP.
  void startTcp() {
    connect(&tcp_, &QTcpSocket::connected, this, [this]() {
      TurnProbeResult r;
      r.ok = true;
      r.server = ep_->text();
      r.rttMs = elapsedMs();
      timeout_.stop();
      finish(r);
    });
    connect(&tcp_, &QTcpSocket::errorOccurred, this, [this]() { fail(QStringLiteral("unreachable"), tcp_.errorString()); });
    tcp_.connectToHost(addr_, static_cast<quint16>(ep_->port));
  }

  QList<QPair<quint16, QByteArray>> baseAttrs() const {
    QList<QPair<quint16, QByteArray>> a;
    a.append({stun::kAttrRequestedTransport, QByteArray("\x11\x00\x00\x00", 4)});  // UDP
    return a;
  }

  void startUdp() {
    if (!sock_.bind(QHostAddress::AnyIPv4, 0)) {
      fail(QStringLiteral("unreachable"), QStringLiteral("cannot open a UDP socket"));
      return;
    }
    connect(&sock_, &QUdpSocket::readyRead, this, [this]() { onReadyRead(); });
    retry_.setInterval(700);
    connect(&retry_, &QTimer::timeout, this, [this]() { send(); });
    retry_.start();
    txid_ = stun::newTransactionId();
    send();
  }

  void send() {
    QByteArray msg;
    if (stage_ == 0) {
      msg = stun::build(stun::kAllocateRequest, txid_, baseAttrs());
    } else {
      auto attrs = baseAttrs();
      attrs.append({stun::kAttrUsername, user_.toUtf8()});
      attrs.append({stun::kAttrRealm, realm_});
      attrs.append({stun::kAttrNonce, nonce_});
      msg = stun::build(stun::kAllocateRequest, txid_, attrs, stun::longTermKey(user_, QString::fromUtf8(realm_), pass_));
      if (!authClock_.isValid()) authClock_.start();
    }
    sock_.writeDatagram(msg, addr_, static_cast<quint16>(ep_->port));
  }

  void onReadyRead() {
    while (sock_.hasPendingDatagrams()) {
      QByteArray data(static_cast<int>(sock_.pendingDatagramSize()), 0);
      sock_.readDatagram(data.data(), data.size());
      const auto msg = stun::parse(data);
      if (!msg || msg->txid != txid_) continue;
      if (msg->type == stun::kAllocateError) {
        const int code = stun::errorCode(*msg);
        if (stage_ == 0 && code == 401 && msg->has(stun::kAttrRealm) && msg->has(stun::kAttrNonce)) {
          realm_ = msg->attr(stun::kAttrRealm);
          nonce_ = msg->attr(stun::kAttrNonce);
          stage_ = 1;
          txid_ = stun::newTransactionId();
          send();
        } else {
          fail(QStringLiteral("auth"), QStringLiteral("relay answered error %1").arg(code));
        }
      } else if (msg->type == stun::kAllocateSuccess) {
        retry_.stop();
        TurnProbeResult r;
        r.ok = true;
        r.server = ep_->text();
        r.rttMs = authClock_.isValid() ? static_cast<int>(authClock_.elapsed()) : elapsedMs();
        r.relayed = stun::addressOf(*msg, stun::kAttrXorRelayed);
        release(*msg);
        timeout_.stop();
        finish(r);
      }
    }
  }

  // Frees the allocation (REFRESH with lifetime 0) so a check leaves nothing behind on the Hub.
  void release(const Message& ok) {
    QByteArray nonce = ok.has(stun::kAttrNonce) ? ok.attr(stun::kAttrNonce) : nonce_;
    QList<QPair<quint16, QByteArray>> attrs;
    attrs.append({stun::kAttrLifetime, QByteArray(4, '\0')});
    attrs.append({stun::kAttrUsername, user_.toUtf8()});
    attrs.append({stun::kAttrRealm, realm_});
    attrs.append({stun::kAttrNonce, nonce});
    sock_.writeDatagram(stun::build(stun::kRefreshRequest, stun::newTransactionId(), attrs, stun::longTermKey(user_, QString::fromUtf8(realm_), pass_)),
                        addr_, static_cast<quint16>(ep_->port));
    sock_.flush();
  }

  std::optional<IceEndpoint> ep_;
  QString user_, pass_;
  QHostAddress addr_;
  QUdpSocket sock_;
  QTcpSocket tcp_;
  QTimer timeout_, retry_;
  QByteArray txid_, realm_, nonce_;
  QElapsedTimer authClock_;
  int stage_ = 0;
};

// ------------------------------------------------------------------------------------------------------- the set

class QtProbes : public ConnectionProbes {
 public:
  QtProbes(ConnectionTestFn test, ConnectionTestCancelFn cancelTest, const ProbeTimings& tm)
      : test_(std::move(test)), cancelTest_(std::move(cancelTest)), tm_(tm) {}
  ~QtProbes() override { cancel(); }

  void probeHub(const ConnectionTarget& t, std::function<void(const HubProbeResult&)> done) override {
    new HubJob(t, tm_.hubTimeoutMs, std::move(done), &owner_);
  }

  void probeStun(const ConnectionTarget& t, std::function<void(const StunProbeResult&)> done) override {
    QList<IceEndpoint> dests;
    const auto add = [&dests](const IceEndpoint& e) {
      for (const IceEndpoint& d : dests) {
        if (d.host == e.host && d.port == e.port) return;
      }
      if (dests.size() < 4) dests.append(e);
    };
    for (const QString& u : t.stunUrls) {
      if (const auto e = parseIceUrl(u); e && !e->tcp) add(*e);
    }
    // The relay's own STUN service gives the second destination NAT classification needs, and works where no external STUN is configured.
    for (const TurnServer& s : t.turnServers) {
      for (const QString& u : s.urls) {
        if (const auto e = parseIceUrl(u); e && !e->tcp) add(*e);
      }
    }
    new StunJob(dests, tm_.stunTimeoutMs, std::move(done), &owner_);
  }

  void probeTurn(const ConnectionTarget& t, std::function<void(const TurnProbeResult&)> done) override {
    new TurnJob(t, tm_.turnTimeoutMs, std::move(done), &owner_);
  }

  void probeConnection(const ConnectionTarget& t, const TestPlan& plan, std::function<void(const TestProbeResult&)> done) override {
    if (!test_) {
      TestProbeResult r;
      r.error = QStringLiteral("WebRTC is not available in this build");
      done(r);
      return;
    }
    test_(t, plan, std::move(done));
  }

  void cancel() override {
    qDeleteAll(owner_.children());
    if (cancelTest_) cancelTest_();
  }

 private:
  QObject owner_;
  ConnectionTestFn test_;
  ConnectionTestCancelFn cancelTest_;
  ProbeTimings tm_;
};

}  // namespace

std::unique_ptr<ConnectionProbes> makeQtConnectionProbes(ConnectionTestFn test, ConnectionTestCancelFn cancelTest, const ProbeTimings& timings) {
  return std::make_unique<QtProbes>(std::move(test), std::move(cancelTest), timings);
}

}  // namespace framebeam
