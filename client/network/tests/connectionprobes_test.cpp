// Network probes of the connection check against in-process fakes on loopback: a fake Hub, fake STUN servers and a fake TURN
// server. Nothing leaves this machine.
#include <QUdpSocket>
#include <QtTest>

#include "connectionprobes.h"
#include "fakehub.h"

using namespace uzume;

namespace {

// Fake STUN/TURN server on 127.0.0.1. Modes: Silent (reads, never answers), Stun (Binding), Turn (Allocate with long-term credentials).
class FakeUdpServer : public QObject {
 public:
  enum class Mode { Silent, Stun, Turn };
  explicit FakeUdpServer(Mode mode, QObject* parent = nullptr) : QObject(parent), mode_(mode) {
    QVERIFY(sock_.bind(QHostAddress::LocalHost, 0));
    connect(&sock_, &QUdpSocket::readyRead, this, [this]() { onReadyRead(); });
  }
  quint16 port() const { return sock_.localPort(); }
  int portShift = 0;  // Stun: reported port = real port + shift (simulates a symmetric NAT)
  QString user = QStringLiteral("1700000000:max"), password = QStringLiteral("pw-for-test");
  int allocations = 0, releases = 0, binding = 0;

 private:
  void onReadyRead() {
    while (sock_.hasPendingDatagrams()) {
      QByteArray d(static_cast<int>(sock_.pendingDatagramSize()), 0);
      QHostAddress from;
      quint16 fromPort = 0;
      sock_.readDatagram(d.data(), d.size(), &from, &fromPort);
      const auto m = stun::parse(d);
      if (!m || mode_ == Mode::Silent) continue;
      if (mode_ == Mode::Stun && m->type == stun::kBindingRequest) {
        ++binding;
        const QByteArray reply = stun::build(stun::kBindingSuccess, m->txid,
                                             {{stun::kAttrXorMapped, stun::xorAddressValue(from, static_cast<quint16>(fromPort + portShift))}});
        sock_.writeDatagram(reply, from, fromPort);
      } else if (mode_ == Mode::Turn && m->type == stun::kAllocateRequest) {
        if (!m->has(stun::kAttrUsername)) {
          reply401(*m, from, fromPort);
          continue;
        }
        const QByteArray key = stun::longTermKey(user, QStringLiteral("fb-test"), password);
        if (QString::fromUtf8(m->attr(stun::kAttrUsername)) != user || !stun::verifyIntegrity(d, key)) {
          reply401(*m, from, fromPort);
          continue;
        }
        ++allocations;
        QByteArray lifetime(4, '\0');
        lifetime[3] = 60;
        sock_.writeDatagram(stun::build(stun::kAllocateSuccess, m->txid,
                                        {{stun::kAttrXorRelayed, stun::xorAddressValue(QHostAddress(QStringLiteral("203.0.113.9")), 49160)},
                                         {stun::kAttrLifetime, lifetime}}, key),
                            from, fromPort);
      } else if (mode_ == Mode::Turn && m->type == stun::kRefreshRequest) {
        ++releases;
      }
    }
  }
  void reply401(const stun::Message& m, const QHostAddress& to, quint16 port) {
    QByteArray err("\0\0\x04\x01Unauthorized", 16);
    sock_.writeDatagram(stun::build(stun::kAllocateError, m.txid,
                                    {{stun::kAttrError, err}, {stun::kAttrRealm, "fb-test"}, {stun::kAttrNonce, "nonce-1"}}),
                        to, port);
  }

  Mode mode_;
  QUdpSocket sock_;
};

ConnectionTarget baseTarget() {
  ConnectionTarget t;
  t.hubName = QStringLiteral("Test Hub");
  return t;
}

TurnServer turnAt(quint16 port, const QString& user, const QString& pw, const QString& transport = QStringLiteral("udp")) {
  TurnServer s;
  s.urls = {QStringLiteral("turn:127.0.0.1:%1?transport=%2").arg(port).arg(transport)};
  s.username = user;
  s.credential = pw;
  return s;
}

ProbeTimings fast() {
  ProbeTimings t;
  t.hubTimeoutMs = 3000;
  t.stunTimeoutMs = 600;
  t.turnTimeoutMs = 600;
  return t;
}

}  // namespace

class ConnectionProbesTest : public QObject {
  Q_OBJECT
 private slots:
  void codecRoundTripAndIntegrity() {
    const QByteArray txid = stun::newTransactionId();
    QCOMPARE(txid.size(), 12);
    const QByteArray key = stun::longTermKey(QStringLiteral("u"), QStringLiteral("r"), QStringLiteral("p"));
    QCOMPARE(key.size(), 16);
    const QByteArray raw = stun::build(stun::kAllocateRequest, txid, {{stun::kAttrUsername, "alice"}, {stun::kAttrRealm, "r"}}, key);
    QCOMPARE(raw.size() % 4, 0);
    const auto m = stun::parse(raw);
    QVERIFY(m);
    QCOMPARE(m->type, stun::kAllocateRequest);
    QCOMPARE(m->txid, txid);
    QCOMPARE(m->attr(stun::kAttrUsername), QByteArray("alice"));
    QVERIFY(m->has(stun::kAttrIntegrity));
    QVERIFY(stun::verifyIntegrity(raw, key));
    QVERIFY(!stun::verifyIntegrity(raw, QByteArray(16, 'x')));
    QByteArray tampered = raw;
    tampered[25] = static_cast<char>(tampered[25] ^ 1);
    QVERIFY(!stun::verifyIntegrity(tampered, key));
    QVERIFY(!stun::parse(QByteArray("short")));
    QVERIFY(!stun::parse(QByteArray(24, '\xff')));
    const auto mapped = stun::parse(stun::build(stun::kBindingSuccess, txid, {{stun::kAttrXorMapped, stun::xorAddressValue(QHostAddress(QStringLiteral("192.0.2.1")), 32853)}}));
    QVERIFY(mapped);
    QCOMPARE(stun::addressOf(*mapped, stun::kAttrXorMapped), QString("192.0.2.1:32853"));
  }

  void iceUrlParsing() {
    QCOMPARE(parseIceUrl("stun:stun.example.org")->text(), QString("stun.example.org:3478"));
    QCOMPARE(parseIceUrl("stun:stun.example.org:19302")->port, 19302);
    const auto t = parseIceUrl("turn:hub.example.com:3478?transport=tcp");
    QVERIFY(t && t->tcp);
    QCOMPARE(parseIceUrl("turns:hub.example.com")->port, 5349);
    QVERIFY(!parseIceUrl("http://x"));
    QVERIFY(!parseIceUrl("stun:"));
    QVERIFY(!parseIceUrl("stun:host:99999"));
  }

  void stunMappingsFromTwoServersMeanNoNatOnLoopback() {
    FakeUdpServer a(FakeUdpServer::Mode::Stun), b(FakeUdpServer::Mode::Stun);
    ConnectionTarget t = baseTarget();
    t.stunUrls = {QStringLiteral("stun:127.0.0.1:%1").arg(a.port()), QStringLiteral("stun:127.0.0.1:%1").arg(b.port())};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<StunProbeResult> r;
    probes->probeStun(t, [&r](const StunProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QCOMPARE(r->mappings.size(), 2);
    for (const StunMapping& m : r->mappings) {
      QVERIFY2(m.answered(), qPrintable(m.server));
      QVERIFY(m.mapped.startsWith("127.0.0.1:"));
      QVERIFY(!m.behindNat);  // the mapped address is the socket's own
    }
    QCOMPARE(r->mappings[0].mapped, r->mappings[1].mapped);  // one socket for all servers
    QCOMPARE(classifyNat(r->mappings), NatType::None);
  }

  void stunDifferentMappingsAreSymmetric() {
    FakeUdpServer a(FakeUdpServer::Mode::Stun), b(FakeUdpServer::Mode::Stun);
    b.portShift = 7;  // the "NAT" maps this destination to another port
    ConnectionTarget t = baseTarget();
    t.stunUrls = {QStringLiteral("stun:127.0.0.1:%1").arg(a.port()), QStringLiteral("stun:127.0.0.1:%1").arg(b.port())};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<StunProbeResult> r;
    probes->probeStun(t, [&r](const StunProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QVERIFY(!r->mappings[0].behindNat);
    QVERIFY(r->mappings[1].behindNat);  // no longer the socket's own address:port for server b
    QCOMPARE(classifyNat(r->mappings), NatType::Symmetric);
  }

  void stunAlsoAsksTheRelaysOwnStunAndSurvivesASilentServer() {
    FakeUdpServer ok(FakeUdpServer::Mode::Stun), silent(FakeUdpServer::Mode::Silent), turnStun(FakeUdpServer::Mode::Stun);
    ConnectionTarget t = baseTarget();
    t.stunUrls = {QStringLiteral("stun:127.0.0.1:%1").arg(silent.port()), QStringLiteral("stun:127.0.0.1:%1").arg(ok.port())};
    t.turnServers = {turnAt(turnStun.port(), QStringLiteral("u"), QStringLiteral("p"))};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<StunProbeResult> r;
    probes->probeStun(t, [&r](const StunProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QCOMPARE(r->mappings.size(), 3);
    QVERIFY(!r->mappings[0].answered());
    QVERIFY(r->mappings[1].answered());
    QVERIFY(r->mappings[2].answered());
    QVERIFY(turnStun.binding >= 1);
  }

  void stunWithNothingConfiguredReturnsEmpty() {
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<StunProbeResult> r;
    probes->probeStun(baseTarget(), [&r](const StunProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 2000);
    QVERIFY(r->mappings.isEmpty());
  }

  void turnAllocatesWithTheHandedCredentialsAndReleases() {
    FakeUdpServer turn(FakeUdpServer::Mode::Turn);
    ConnectionTarget t = baseTarget();
    t.turnServers = {turnAt(turn.port(), turn.user, turn.password)};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<TurnProbeResult> r;
    probes->probeTurn(t, [&r](const TurnProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QVERIFY2(r->ok, qPrintable(r->error));
    QCOMPARE(r->relayed, QString("203.0.113.9:49160"));
    QVERIFY(r->rttMs >= 0);
    QCOMPARE(turn.allocations, 1);
    QTRY_COMPARE_WITH_TIMEOUT(turn.releases, 1, 2000);  // the allocation is freed again
  }

  void turnWithWrongCredentialsIsAnAuthFailure() {
    FakeUdpServer turn(FakeUdpServer::Mode::Turn);
    ConnectionTarget t = baseTarget();
    t.turnServers = {turnAt(turn.port(), turn.user, QStringLiteral("wrong"))};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<TurnProbeResult> r;
    probes->probeTurn(t, [&r](const TurnProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QVERIFY(!r->ok);
    QCOMPARE(r->errorKind, QString("auth"));
    QVERIFY(!r->error.contains("wrong"));
    QCOMPARE(turn.allocations, 0);
  }

  void turnWithoutAnswerIsUnreachable() {
    FakeUdpServer silent(FakeUdpServer::Mode::Silent);
    ConnectionTarget t = baseTarget();
    t.turnServers = {turnAt(silent.port(), QStringLiteral("u"), QStringLiteral("p"))};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<TurnProbeResult> r;
    probes->probeTurn(t, [&r](const TurnProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QVERIFY(!r->ok);
    QCOMPARE(r->errorKind, QString("unreachable"));
  }

  void tcpOnlyRelayChecksThePortAnswers() {
    QTcpServer srv;
    QVERIFY(srv.listen(QHostAddress::LocalHost, 0));
    ConnectionTarget t = baseTarget();
    t.turnServers = {turnAt(srv.serverPort(), QStringLiteral("u"), QStringLiteral("p"), QStringLiteral("tcp"))};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<TurnProbeResult> r;
    probes->probeTurn(t, [&r](const TurnProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 3000);
    QVERIFY2(r->ok, qPrintable(r->error));
  }

  void hubProbeAnswersAndClassifiesFailures() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    auto probes = makeQtConnectionProbes({}, {}, fast());

    ConnectionTarget t = baseTarget();
    t.hubAddress = hub.address();
    t.pinnedFingerprint = hub.fingerprint();
    std::optional<HubProbeResult> r;
    probes->probeHub(t, [&r](const HubProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 5000);
    QVERIFY2(r->ok, qPrintable(r->error));
    QVERIFY(r->latencyMs >= 0);

    // A different certificate than the pinned one is never accepted.
    r.reset();
    t.pinnedFingerprint = QStringLiteral("AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99");
    probes->probeHub(t, [&r](const HubProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 5000);
    QVERIFY(!r->ok);
    QCOMPARE(r->errorKind, QString("certificate"));

    // Nothing listens: refused. Windows reports it only after its connect retries (about 2 s, more under load), so this probe gets
    // the real 10 s limit instead of the short test one.
    ProbeTimings slow = fast();
    slow.hubTimeoutMs = 10000;
    auto slowProbes = makeQtConnectionProbes({}, {}, slow);
    r.reset();
    ConnectionTarget closed = baseTarget();
    closed.hubAddress = QStringLiteral("http://127.0.0.1:1");
    closed.allowHttp = true;
    slowProbes->probeHub(closed, [&r](const HubProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 15000);
    QVERIFY(!r->ok);
    QCOMPARE(r->errorKind, QString("refused"));
  }

  void hubProbeRejectsAnAnswerThatIsNoHub() {
    QTcpServer srv;  // accepts and says HTTP 404
    QVERIFY(srv.listen(QHostAddress::LocalHost, 0));
    connect(&srv, &QTcpServer::newConnection, &srv, [&srv]() {
      QTcpSocket* s = srv.nextPendingConnection();
      connect(s, &QTcpSocket::readyRead, s, [s]() {
        s->readAll();
        s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        s->disconnectFromHost();
      });
    });
    auto probes = makeQtConnectionProbes({}, {}, fast());
    ConnectionTarget t = baseTarget();
    t.hubAddress = QStringLiteral("http://127.0.0.1:%1").arg(srv.serverPort());
    t.allowHttp = true;
    std::optional<HubProbeResult> r;
    probes->probeHub(t, [&r](const HubProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 5000);
    QVERIFY(!r->ok);
    QCOMPARE(r->errorKind, QString("http"));
  }

  void hubProbeTimesOutOnASilentPort() {
    QTcpServer srv;  // accepts, never answers
    QVERIFY(srv.listen(QHostAddress::LocalHost, 0));
    ProbeTimings tm = fast();
    tm.hubTimeoutMs = 300;
    auto probes = makeQtConnectionProbes({}, {}, tm);
    ConnectionTarget t = baseTarget();
    t.hubAddress = QStringLiteral("http://127.0.0.1:%1").arg(srv.serverPort());
    t.allowHttp = true;
    std::optional<HubProbeResult> r;
    probes->probeHub(t, [&r](const HubProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 5000);
    QVERIFY(!r->ok);
    QCOMPARE(r->errorKind, QString("timeout"));
    QVERIFY(r->latencyMs >= 250);
  }

  void cancelDropsPendingCallbacks() {
    FakeUdpServer silent(FakeUdpServer::Mode::Silent);
    ConnectionTarget t = baseTarget();
    t.stunUrls = {QStringLiteral("stun:127.0.0.1:%1").arg(silent.port())};
    auto probes = makeQtConnectionProbes({}, {}, fast());
    bool called = false;
    probes->probeStun(t, [&called](const StunProbeResult&) { called = true; });
    probes->cancel();
    QTest::qWait(900);
    QVERIFY(!called);
  }

  void missingWebRtcTestIsReportedNotHidden() {
    auto probes = makeQtConnectionProbes({}, {}, fast());
    std::optional<TestProbeResult> r;
    probes->probeConnection(baseTarget(), {}, [&r](const TestProbeResult& x) { r = x; });
    QVERIFY(r.has_value());
    QVERIFY(!r->ok);
    QVERIFY(r->error.contains("WebRTC"));
  }
};

QTEST_GUILESS_MAIN(ConnectionProbesTest)
#include "connectionprobes_test.moc"
