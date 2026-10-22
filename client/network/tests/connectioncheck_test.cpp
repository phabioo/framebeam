// Connection check logic (design 6e): step sequencing, skip logic, NAT classification, outcome selection, cancel and timeout, export.
// Fake probes only; no network.
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

#include "connectioncheck.h"

using namespace uzume;

namespace {

// Scripted probes. By default every call answers at once; `hold` keeps the Hub callback for the test to fire (or never fire).
struct Script {
  HubProbeResult hub{true, 22, {}, {}};
  StunProbeResult stun;
  TurnProbeResult turn{true, 31, QStringLiteral("hub.example.com:3478"), {}, {}, QStringLiteral("203.0.113.9:50000")};
  TestProbeResult test;
  QStringList calls;
  bool lastRelayOnly = false;
  int cancels = 0;
  bool hold = false;
  std::function<void(const HubProbeResult&)> heldHub;
};

class FakeProbes : public ConnectionProbes {
 public:
  explicit FakeProbes(Script* s) : s_(s) {}
  void probeHub(const ConnectionTarget&, std::function<void(const HubProbeResult&)> done) override {
    s_->calls << "hub";
    if (s_->hold) {
      s_->heldHub = std::move(done);
      return;
    }
    done(s_->hub);
  }
  void probeStun(const ConnectionTarget&, std::function<void(const StunProbeResult&)> done) override {
    s_->calls << "stun";
    done(s_->stun);
  }
  void probeTurn(const ConnectionTarget&, std::function<void(const TurnProbeResult&)> done) override {
    s_->calls << "turn";
    done(s_->turn);
  }
  void probeConnection(const ConnectionTarget&, const TestPlan& p, std::function<void(const TestProbeResult&)> done) override {
    s_->calls << "test";
    s_->lastRelayOnly = p.relayOnly;
    done(s_->test);
  }
  void cancel() override { ++s_->cancels; }

 private:
  Script* s_;
};

StunMapping map(const QString& server, const QString& mapped, bool behindNat = true) {
  StunMapping m;
  m.server = server;
  m.mapped = mapped;
  m.rttMs = 12;
  m.behindNat = behindNat;
  return m;
}

ConnectionTarget target(bool turn = true, bool stun = true) {
  ConnectionTarget t;
  t.hubName = QStringLiteral("Home Hub");
  t.hubAddress = QStringLiteral("https://hub.example.com:8443");
  t.deviceName = QStringLiteral("Desktop-LivingRoom");
  if (stun) t.stunUrls = {QStringLiteral("stun:stun.example.org:3478")};
  if (turn) {
    TurnServer ts;
    ts.urls = {QStringLiteral("turn:hub.example.com:3478?transport=udp")};
    ts.username = QStringLiteral("1700000000:max");
    ts.credential = QStringLiteral("SECRET-CRED-123");
    t.turnServers = {ts};
  }
  return t;
}

Script coneScript() {
  Script s;
  s.stun.mappings = {map(QStringLiteral("stun.example.org:3478"), QStringLiteral("198.51.100.4:40000")),
                     map(QStringLiteral("hub.example.com:3478"), QStringLiteral("198.51.100.4:40000"))};
  s.test.ok = true;
  s.test.rttMs = 2;
  s.test.srflxCandidates = 1;
  s.test.hostCandidates = 2;
  return s;
}

Script symmetricScript() {
  Script s = coneScript();
  s.stun.mappings[1].mapped = QStringLiteral("198.51.100.4:40001");
  s.test.srflxCandidates = 0;
  s.test.relayCandidates = 2;
  s.test.rttMs = 48;
  s.test.server = QStringLiteral("hub.example.com:3478");
  return s;
}

CheckResult run(Script& s, const ConnectionTarget& t) {
  ConnectionCheck c(std::make_unique<FakeProbes>(&s));
  c.start(t);  // fakes answer synchronously
  return c.result();
}

}  // namespace

class ConnectionCheckTest : public QObject {
  Q_OBJECT
 private slots:
  void natClassification() {
    QCOMPARE(classifyNat({}), NatType::Unknown);
    QCOMPARE(classifyNat({map("a:1", "")}), NatType::Unknown);
    QCOMPARE(classifyNat({map("a:1", "1.2.3.4:5")}), NatType::Unknown);  // one server cannot be compared
    QCOMPARE(classifyNat({map("a:1", "1.2.3.4:5"), map("b:1", "1.2.3.4:5")}), NatType::EndpointIndependent);
    QCOMPARE(classifyNat({map("a:1", "1.2.3.4:5"), map("b:1", "1.2.3.4:6")}), NatType::Symmetric);
    QCOMPARE(classifyNat({map("a:1", "1.2.3.4:5"), map("a:1", "1.2.3.4:6")}), NatType::Unknown);  // the same server twice is one server
    QCOMPARE(classifyNat({map("a:1", "1.2.3.4:5"), map("b:1", "1.2.3.4:5"), map("c:1", "1.2.3.4:9")}), NatType::Symmetric);
    QCOMPARE(classifyNat({map("a:1", "10.0.0.2:5", false), map("b:1", "10.0.0.2:5", false)}), NatType::None);
    QCOMPARE(classifyNat({map("a:1", "10.0.0.2:5", false)}), NatType::None);  // a public address needs no comparison
    QCOMPARE(classifyNat({map("a:1", "1.2.3.4:5"), map("b:1", "")}), NatType::Unknown);
  }

  void directPossibleRunsAllFiveStepsInOrder() {
    Script s = coneScript();
    ConnectionCheck c(std::make_unique<FakeProbes>(&s));
    QSignalSpy progress(&c, &ConnectionCheck::progress);
    QSignalSpy done(&c, &ConnectionCheck::finished);
    c.start(target());
    QCOMPARE(done.count(), 1);
    const CheckResult r = c.result();
    QCOMPARE(s.calls, (QStringList{"hub", "stun", "turn", "test"}));  // NAT type is derived from the STUN answers
    QCOMPARE(progress.count(), 5);
    QCOMPARE(progress.at(2).at(0).toInt(), 3);
    QCOMPARE(progress.at(2).at(1).toInt(), 5);
    QCOMPARE(r.outcome, CheckOutcome::Direct);
    QCOMPARE(r.nat, NatType::EndpointIndependent);
    for (const CheckStep& st : r.steps) QCOMPARE(st.state, CheckStepState::Ok);
    QCOMPARE(r.steps[0].result, QString("hub.example.com:8443 · 22 ms"));
    QCOMPARE(r.steps[2].result, QString("Works · 31 ms"));
    QVERIFY(!s.lastRelayOnly);
    QCOMPARE(r.summary, QString("Your network allows direct connections"));
    QVERIFY(r.summaryDetail.contains("relay is ready as a fallback"));
    QVERIFY(r.finishedAt.isValid());
    QVERIFY(!c.running());
  }

  void symmetricNatIsRelayedOnly() {
    Script s = symmetricScript();
    const CheckResult r = run(s, target());
    QCOMPARE(r.outcome, CheckOutcome::Relayed);
    QCOMPARE(r.nat, NatType::Symmetric);
    QCOMPARE(r.steps[3].state, CheckStepState::Warning);
    QCOMPARE(r.steps[3].result, QString("Symmetric NAT"));
    QVERIFY(s.lastRelayOnly);
    QCOMPARE(r.steps[4].state, CheckStepState::Warning);
    QCOMPARE(r.steps[4].result, QString("Relayed through hub.example.com:3478 · 48 ms"));
    QCOMPARE(r.steps[2].explanation, QString("Sessions will use the relay on this network."));
    QCOMPARE(r.summary, QString("Only relayed connections are possible here"));
    QVERIFY(r.summaryDetail.contains("UPnP"));
  }

  void hubFailureSkipsEverythingElse_data() {
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("resultStart");
    QTest::newRow("timeout") << "timeout" << "No answer from hub.example.com:8443 after 10 s";
    QTest::newRow("refused") << "refused" << "hub.example.com:8443 refused";
    QTest::newRow("dns") << "dns" << "The name hub.example.com cannot be found";
    QTest::newRow("certificate") << "certificate" << "The certificate of hub.example.com:8443 does not match";
    QTest::newRow("http") << "http" << "hub.example.com:8443 does not answer like a Hub";
  }
  void hubFailureSkipsEverythingElse() {
    QFETCH(QString, kind);
    QFETCH(QString, resultStart);
    Script s = coneScript();
    s.hub = {false, 10000, QStringLiteral("detail"), kind};
    const CheckResult r = run(s, target());
    QCOMPARE(s.calls, QStringList{"hub"});
    QCOMPARE(r.outcome, CheckOutcome::HubUnreachable);
    QCOMPARE(r.steps[0].state, CheckStepState::Failed);
    QVERIFY2(r.steps[0].result.startsWith(resultStart), qPrintable(r.steps[0].result));
    for (int i = 1; i < 5; ++i) {
      QCOMPARE(r.steps[i].state, CheckStepState::Skipped);
      QCOMPARE(r.steps[i].result, QString("Skipped"));
    }
    QCOMPARE(r.summary, QString("Home Hub cannot be reached"));
    QVERIFY(r.summaryDetail.contains("hub.example.com:8443"));
    QVERIFY(r.summaryDetail.contains("were skipped"));
  }

  void withoutAHubSessionTheIceServersAreUnknown() {
    Script s = coneScript();
    ConnectionTarget t = target();
    t.iceKnown = false;  // the Hub answers, but this Player has no hello_ack: "no STUN" would be a wrong claim
    const CheckResult r = run(s, t);
    QCOMPARE(s.calls, QStringList{"hub"});
    QCOMPARE(r.steps[0].state, CheckStepState::Ok);
    for (int i = 1; i < 5; ++i) QCOMPARE(r.steps[i].state, CheckStepState::Skipped);
    QCOMPARE(r.outcome, CheckOutcome::NotConnected);
    QVERIFY(r.summary.startsWith("Connect to Home Hub"));
  }

  void turnOffIsSkippedWithExplanation() {
    Script s = coneScript();
    const CheckResult r = run(s, target(/*turn=*/false));
    QVERIFY(!s.calls.contains("turn"));
    QCOMPARE(r.steps[2].state, CheckStepState::Skipped);
    QVERIFY(r.steps[2].explanation.contains("no relay"));
    QCOMPARE(r.outcome, CheckOutcome::Direct);
    QVERIFY(r.summaryDetail.contains("has no relay"));
  }

  void noStunNoTurnIsALocalNetworkOnlyHub() {
    Script s;  // the STUN probe answers with an empty list: nothing was asked
    s.test.ok = true;
    const CheckResult r = run(s, target(false, false));
    QCOMPARE(r.steps[1].state, CheckStepState::Skipped);
    QCOMPARE(r.steps[2].state, CheckStepState::Skipped);
    QCOMPARE(r.steps[3].state, CheckStepState::Skipped);
    QCOMPARE(r.outcome, CheckOutcome::Blocked);
    QVERIFY(r.summaryDetail.contains("same network"));
  }

  void stunBlockedButRelayWorksIsRelayed() {
    Script s = coneScript();
    s.stun.mappings = {map(QStringLiteral("stun.example.org:3478"), QString()), map(QStringLiteral("hub.example.com:3478"), QString())};
    s.test.srflxCandidates = 0;
    s.test.rttMs = 40;
    const CheckResult r = run(s, target());
    QCOMPARE(r.steps[1].state, CheckStepState::Failed);
    QCOMPARE(r.steps[3].state, CheckStepState::Skipped);
    QVERIFY(s.lastRelayOnly);
    QCOMPARE(r.outcome, CheckOutcome::Relayed);
  }

  void symmetricWithoutRelayIsBlocked() {
    Script s = symmetricScript();
    const CheckResult r = run(s, target(/*turn=*/false));
    QVERIFY(!s.lastRelayOnly);  // no relay to test: the test peers only report what they gathered
    QCOMPARE(r.outcome, CheckOutcome::Blocked);
    QVERIFY(r.summaryDetail.contains("blocks direct connections"));
  }

  void failedRelayWithSymmetricNatIsBlocked() {
    Script s = symmetricScript();
    s.turn = {false, 0, QStringLiteral("hub.example.com:3478"), QStringLiteral("relay answered error 401"), QStringLiteral("auth"), {}};
    s.test.ok = false;
    s.test.error = QStringLiteral("no connection within 15 s");
    const CheckResult r = run(s, target());
    QCOMPARE(r.steps[2].state, CheckStepState::Failed);
    QVERIFY(r.steps[2].result.contains("refused the Player's credentials"));
    QCOMPARE(r.steps[4].state, CheckStepState::Failed);
    QCOMPARE(r.outcome, CheckOutcome::Blocked);
  }

  void relayThatCarriesNoDataIsNotCalledRelayed() {
    Script s = symmetricScript();
    s.test.ok = false;  // allocation worked (step 3) but the relay test peers could not exchange data
    s.test.error = QStringLiteral("no connection within 15 s");
    const CheckResult r = run(s, target());
    QCOMPARE(r.steps[2].state, CheckStepState::Ok);
    QCOMPARE(r.steps[4].state, CheckStepState::Failed);
    QCOMPARE(r.outcome, CheckOutcome::Blocked);
  }

  void directNeedsAPublicAddressInTheTest() {
    Script s = coneScript();
    s.test.srflxCandidates = 0;  // STUN answered the raw probe, but the ICE agent found nothing
    const CheckResult r = run(s, target());
    QCOMPARE(r.steps[4].state, CheckStepState::Warning);
    QCOMPARE(r.outcome, CheckOutcome::Relayed);  // the relay allocation works, direct is not shown
  }

  void unknownNatWithOneServerStillAllowsDirect() {
    Script s = coneScript();
    s.stun.mappings.removeLast();
    const CheckResult r = run(s, target());
    QCOMPARE(r.nat, NatType::Unknown);
    QCOMPARE(r.steps[3].state, CheckStepState::Warning);
    QCOMPARE(r.steps[3].result, QString("Not determined"));
    QCOMPARE(r.outcome, CheckOutcome::Direct);
  }

  void forcedRelayIsRelayedEvenOnAnOpenNetwork() {
    Script s = coneScript();
    s.test.rttMs = 33;
    ConnectionTarget t = target();
    t.forceRelay = true;
    const CheckResult r = run(s, t);
    QVERIFY(s.lastRelayOnly);
    QCOMPARE(r.outcome, CheckOutcome::Relayed);
    QVERIFY(r.summaryDetail.contains("UZUME_FORCE_RELAY"));
  }

  void cancelStopsTheRunAndIgnoresLateAnswers() {
    Script s = coneScript();
    s.hold = true;
    ConnectionCheck c(std::make_unique<FakeProbes>(&s));
    QSignalSpy done(&c, &ConnectionCheck::finished);
    c.start(target());
    QVERIFY(c.running());
    QCOMPARE(c.result().steps[0].state, CheckStepState::Running);
    c.cancel();
    QCOMPARE(done.count(), 1);
    QCOMPARE(c.result().outcome, CheckOutcome::Canceled);
    QCOMPARE(c.result().steps[0].state, CheckStepState::Skipped);
    QCOMPARE(c.result().steps[1].state, CheckStepState::Skipped);
    QVERIFY(s.cancels >= 1);
    s.heldHub(s.hub);  // late callback
    QCOMPARE(s.calls, QStringList{"hub"});
    QCOMPARE(done.count(), 1);
    // A new run works afterwards.
    s.hold = false;
    c.start(target());
    QCOMPARE(c.result().outcome, CheckOutcome::Direct);
    QCOMPARE(done.count(), 2);
  }

  void overallTimeoutEndsTheRun() {
    Script s = coneScript();
    s.hold = true;
    ConnectionCheck c(std::make_unique<FakeProbes>(&s));
    c.setTimeoutMs(30);
    QSignalSpy done(&c, &ConnectionCheck::finished);
    c.start(target());
    QVERIFY(done.wait(2000));
    QCOMPARE(c.result().outcome, CheckOutcome::TimedOut);
    QCOMPARE(c.result().steps[0].state, CheckStepState::Failed);
    QCOMPARE(c.result().steps[0].result, QString("Timed out"));
    QCOMPARE(c.result().steps[4].state, CheckStepState::Skipped);
  }

  void restartWhileRunningDropsTheOldRun() {
    Script s = coneScript();
    s.hold = true;
    ConnectionCheck c(std::make_unique<FakeProbes>(&s));
    QSignalSpy done(&c, &ConnectionCheck::finished);
    c.start(target());
    auto old = s.heldHub;
    s.hold = false;
    c.start(target());
    QCOMPARE(done.count(), 1);
    old(s.hub);
    QCOMPARE(done.count(), 1);
    QCOMPARE(c.result().outcome, CheckOutcome::Direct);
  }

  void exportsHaveNoCredentialsAndTheDocumentedShape() {
    Script s = symmetricScript();
    const CheckResult r = run(s, target());
    const QString text = r.toPlainText();
    const QByteArray json = QJsonDocument(r.toJson()).toJson();
    for (const QByteArray& secret : {QByteArray("SECRET-CRED-123"), QByteArray("1700000000:max")}) {
      QVERIFY(!json.contains(secret));
      QVERIFY(!text.toUtf8().contains(secret));
    }
    const QJsonObject o = r.toJson();
    QCOMPARE(o["format"].toInt(), 1);
    QCOMPARE(o["kind"].toString(), QString("connection_check"));
    QCOMPARE(o["outcome"].toString(), QString("relayed"));
    QCOMPARE(o["nat_type"].toString(), QString("symmetric"));
    QCOMPARE(o["hub"].toObject()["address"].toString(), QString("https://hub.example.com:8443"));
    QCOMPARE(o["steps"].toArray().size(), 5);
    QCOMPARE(o["steps"].toArray()[3].toObject()["id"].toString(), QString("nat"));
    QCOMPARE(o["steps"].toArray()[3].toObject()["state"].toString(), QString("warning"));
    QVERIFY(!o["generated_at"].toString().isEmpty());
    QVERIFY(text.contains("1. Hub reachable: hub.example.com:8443 · 22 ms (ok)"));
    QVERIFY(text.contains("4. NAT type: Symmetric NAT (warning)"));
    QVERIFY(text.contains("Only relayed connections are possible here"));
    QVERIFY(text.contains("198.51.100.4:40000"));  // technical details are in the export
  }
};

QTEST_GUILESS_MAIN(ConnectionCheckTest)
#include "connectioncheck_test.moc"
