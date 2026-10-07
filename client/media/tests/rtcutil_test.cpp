// PeerConnection configuration (TURN servers, forced relay), connection type naming and runtime encoder bitrate
// (ADR 0012 D5). No network.
#include <QtTest>

#include "processguard.h"

#include "rtcutil.h"
#include "sessionhost.h"
#include "sessionviewer.h"
#include "videoencoder.h"

using namespace framebeam;

namespace {
TurnServer turn(const QStringList& urls) { return TurnServer{urls, QStringLiteral("1700000000:dev"), QStringLiteral("secret"), QStringLiteral("2026-10-08T00:00:00Z")}; }
}  // namespace

class RtcUtilTest : public QObject {
  Q_OBJECT
 private slots:
  void noServersMeansHostOnly() {
    const rtc::Configuration cfg = makeRtcConfig({});
    QVERIFY(cfg.iceServers.empty());
    QVERIFY(cfg.iceTransportPolicy == rtc::TransportPolicy::All);
  }

  void stunAndTurnServers() {
    const rtc::Configuration cfg = makeRtcConfig(
        {QStringLiteral("stun:hub.example.org:3478"), QStringLiteral("http://ignored")},
        {turn({QStringLiteral("turn:hub.example.org:3478?transport=udp"), QStringLiteral("turn:hub.example.org:3479?transport=tcp")})});
    QCOMPARE(cfg.iceServers.size(), size_t(3));
    QVERIFY(cfg.iceServers[0].type == rtc::IceServer::Type::Stun);
    QCOMPARE(QString::fromStdString(cfg.iceServers[0].hostname), QStringLiteral("hub.example.org"));
    const rtc::IceServer& udp = cfg.iceServers[1];
    QVERIFY(udp.type == rtc::IceServer::Type::Turn);
    QVERIFY(udp.relayType == rtc::IceServer::RelayType::TurnUdp);
    QCOMPARE(QString::fromStdString(udp.hostname), QStringLiteral("hub.example.org"));
    QCOMPARE(int(udp.port), 3478);
    QCOMPARE(QString::fromStdString(udp.username), QStringLiteral("1700000000:dev"));
    QCOMPARE(QString::fromStdString(udp.password), QStringLiteral("secret"));
    const rtc::IceServer& tcp = cfg.iceServers[2];
    QVERIFY(tcp.relayType == rtc::IceServer::RelayType::TurnTcp);
    QCOMPARE(int(tcp.port), 3479);
    QCOMPARE(QString::fromStdString(tcp.password), QStringLiteral("secret"));
  }

  void turnUrlForms() {
    const auto noTransport = turnIceServer(QStringLiteral("turn:10.0.0.1"), QStringLiteral("u"), QStringLiteral("p"));
    QVERIFY(noTransport.has_value());
    QCOMPARE(int(noTransport->port), 3478);
    QVERIFY(noTransport->relayType == rtc::IceServer::RelayType::TurnUdp);
    const auto v6 = turnIceServer(QStringLiteral("turn:[2001:db8::1]:3478?transport=tcp"), QStringLiteral("u"), QStringLiteral("p"));
    QVERIFY(v6.has_value());
    QCOMPARE(QString::fromStdString(v6->hostname), QStringLiteral("2001:db8::1"));
    QVERIFY(v6->relayType == rtc::IceServer::RelayType::TurnTcp);
    const auto tls = turnIceServer(QStringLiteral("turns:hub.example.org"), QStringLiteral("u"), QStringLiteral("p"));
    QVERIFY(tls.has_value());
    QCOMPARE(int(tls->port), 5349);
    QVERIFY(tls->relayType == rtc::IceServer::RelayType::TurnTls);
    QVERIFY(!turnIceServer(QStringLiteral("stun:hub:3478"), QStringLiteral("u"), QStringLiteral("p")).has_value());
    QVERIFY(!turnIceServer(QStringLiteral("turn:host:notaport"), QStringLiteral("u"), QStringLiteral("p")).has_value());
    QVERIFY(!turnIceServer(QStringLiteral("turn::3478"), QStringLiteral("u"), QStringLiteral("p")).has_value());
    QVERIFY(!turnIceServer(QStringLiteral("turn:host:70000"), QStringLiteral("u"), QStringLiteral("p")).has_value());
  }

  void forcedRelaySetsTransportPolicy() {
    const QList<TurnServer> t{turn({QStringLiteral("turn:127.0.0.1:3478?transport=udp")})};
    QVERIFY(makeRtcConfig({}, t, true).iceTransportPolicy == rtc::TransportPolicy::Relay);
    QVERIFY(makeRtcConfig({}, t, false).iceTransportPolicy == rtc::TransportPolicy::All);
    QVERIFY(makeRtcConfig({}, t).iceTransportPolicy == rtc::TransportPolicy::All);
  }

  void forceRelayEnvironment() {
    qunsetenv("FRAMEBEAM_FORCE_RELAY");
    QVERIFY(!forceRelayFromEnv());
    qputenv("FRAMEBEAM_FORCE_RELAY", "1");
    QVERIFY(forceRelayFromEnv());
    qputenv("FRAMEBEAM_FORCE_RELAY", "0");
    QVERIFY(!forceRelayFromEnv());
    qunsetenv("FRAMEBEAM_FORCE_RELAY");
  }

  void hostAndViewerTakeForceRelayFromEnvironment() {
    qunsetenv("FRAMEBEAM_FORCE_RELAY");
    {
      SessionHost h;
      SessionViewer v;
      QVERIFY(!h.forceRelay() && !v.forceRelay());
    }
    qputenv("FRAMEBEAM_FORCE_RELAY", "1");
    SessionHost h;
    SessionViewer v;
    QVERIFY(h.forceRelay());
    QVERIFY(v.forceRelay());
    h.setForceRelay(false);
    QVERIFY(!h.forceRelay());  // explicit setting still overrides
    qunsetenv("FRAMEBEAM_FORCE_RELAY");
  }

  void tcpOnlyDetection() {
    QVERIFY(!turnServersTcpOnly({}));
    QVERIFY(!turnServersTcpOnly({turn({QStringLiteral("turn:h:1?transport=udp"), QStringLiteral("turn:h:1?transport=tcp")})}));
    QVERIFY(turnServersTcpOnly({turn({QStringLiteral("turn:h:1?transport=tcp")})}));
  }

  void connectionTypes() {
    const rtc::Candidate host("candidate:1 1 UDP 2122317823 192.168.1.10 4000 typ host");
    const rtc::Candidate srflx("candidate:2 1 UDP 1686052607 203.0.113.5 5000 typ srflx raddr 192.168.1.10 rport 4000");
    const rtc::Candidate prflx("candidate:3 1 UDP 1686052607 203.0.113.6 5001 typ prflx raddr 0.0.0.0 rport 0");
    const rtc::Candidate relay("candidate:4 1 UDP 41885439 198.51.100.7 6000 typ relay raddr 0.0.0.0 rport 0");
    QCOMPARE(connectionTypeOfCandidates(host, host), QStringLiteral("direct (host)"));
    QCOMPARE(connectionTypeOfCandidates(host, srflx), QStringLiteral("direct (srflx)"));
    QCOMPARE(connectionTypeOfCandidates(srflx, host), QStringLiteral("direct (srflx)"));
    QCOMPARE(connectionTypeOfCandidates(host, prflx), QStringLiteral("direct (prflx)"));
    QCOMPARE(connectionTypeOfCandidates(relay, host), QStringLiteral("relay (udp)"));
    QCOMPARE(connectionTypeOfCandidates(srflx, relay), QStringLiteral("relay (udp)"));
    QCOMPARE(connectionTypeOfCandidates(relay, host, true), QStringLiteral("relay (tcp)"));
    QCOMPARE(connectionTypeOfCandidates(rtc::Candidate(), rtc::Candidate()), QString());
    // Relay-only policy: a reflexive pair is the relay seen from the other side.
    QCOMPARE(connectionTypeOfCandidates(srflx, prflx, false, true), QStringLiteral("relay (udp)"));
    QCOMPARE(connectionTypeOfCandidates(srflx, prflx, true, true), QStringLiteral("relay (tcp)"));
    QCOMPARE(connectionTypeOfCandidates(rtc::Candidate(), rtc::Candidate(), false, true), QString());
  }

  void worstConnectionRank() {
    QVERIFY(connectionTypeRank(QStringLiteral("relay (tcp)")) > connectionTypeRank(QStringLiteral("direct (srflx)")));
    QVERIFY(connectionTypeRank(QStringLiteral("relay (udp)")) > connectionTypeRank(QStringLiteral("direct (prflx)")));
    QVERIFY(connectionTypeRank(QStringLiteral("direct (srflx)")) > connectionTypeRank(QStringLiteral("direct (host)")));
    QVERIFY(connectionTypeRank(QStringLiteral("direct (host)")) > connectionTypeRank(QString()));
  }

  void encoderBitrateAtRuntime() {
    qputenv("FRAMEBEAM_H264_ENCODER", "libx264");
    VideoEncoder enc;
    if (!enc.open(128, 96, 60, 2'000'000)) {
      qunsetenv("FRAMEBEAM_H264_ENCODER");
      QSKIP("libx264 is not available in this FFmpeg build");
    }
    qunsetenv("FRAMEBEAM_H264_ENCODER");
    QVERIFY(enc.supportsRuntimeBitrate());
    QCOMPARE(enc.bitrate(), 2'000'000);
    std::vector<uint8_t> px(128 * 96 * 4, 90);
    std::vector<EncodedVideoPacket> out;
    for (int i = 0; i < 5; ++i) {
      px[size_t(i) * 7] = uint8_t(i * 40);
      QVERIFY(enc.encode(px.data(), 128 * 4, RawPixelFormat::Xrgb8888, i, out));
    }
    QVERIFY(enc.setBitrate(500'000));
    QCOMPARE(enc.bitrate(), 500'000);
    for (int i = 5; i < 15; ++i) {
      px[size_t(i) * 7] = uint8_t(i * 40);
      QVERIFY(enc.encode(px.data(), 128 * 4, RawPixelFormat::Xrgb8888, i, out));
    }
    QVERIFY(!out.empty());
  }
};

FB_TEST_MAIN(RtcUtilTest)
#include "rtcutil_test.moc"
