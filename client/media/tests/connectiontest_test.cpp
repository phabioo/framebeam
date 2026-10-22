// WebRTC test peers of the connection check (step 5), in-process on loopback; no servers, no network.
#include <QtTest>
#include <optional>

#include "connectiontest.h"

using namespace uzume;

class ConnectionTestTest : public QObject {
  Q_OBJECT
 private slots:
  void twoPeersConnectAndEchoOnHostCandidates() {
    ConnectionTestRunner runner;
    std::optional<TestProbeResult> r;
    runner.run(ConnectionTarget{}, TestPlan{}, [&r](const TestProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 20000);
    QVERIFY2(r->ok, qPrintable(r->error));
    QVERIFY(r->hostCandidates > 0);
    QCOMPARE(r->srflxCandidates, 0);  // no STUN server configured
    // A local peer-reflexive candidate is selected when a check arrives before the signalled host candidate;
    // both mean a direct path without a relay.
    QVERIFY2(r->pathType == QLatin1String("host") || r->pathType == QLatin1String("prflx"), qPrintable(r->pathType));
    QVERIFY(r->rttMs >= 1);
  }

  void relayOnlyWithoutARelayFailsHonestly() {
    ConnectionTestRunner runner;
    runner.setTimeoutMs(1500);
    std::optional<TestProbeResult> r;
    TestPlan plan;
    plan.relayOnly = true;
    runner.run(ConnectionTarget{}, plan, [&r](const TestProbeResult& x) { r = x; });
    QTRY_VERIFY_WITH_TIMEOUT(r.has_value(), 10000);
    QVERIFY(!r->ok);
    QVERIFY(!r->error.isEmpty());
    QCOMPARE(r->relayCandidates, 0);
  }

  void cancelDropsTheAnswerAndDestructionJoins() {
    bool called = false;
    {
      ConnectionTestRunner runner;
      runner.setTimeoutMs(30000);
      TestPlan plan;
      plan.relayOnly = true;
      runner.run(ConnectionTarget{}, plan, [&called](const TestProbeResult&) { called = true; });
      QTest::qWait(100);
      runner.cancel();
    }
    QTest::qWait(200);
    QVERIFY(!called);
  }

  void realProbesRunTheTestStep() {
    auto probes = makeConnectionProbes();
    std::optional<TestProbeResult> tr;
    probes->probeConnection(ConnectionTarget{}, TestPlan{}, [&tr](const TestProbeResult& x) { tr = x; });
    QTRY_VERIFY_WITH_TIMEOUT(tr.has_value(), 20000);
    QVERIFY2(tr->ok, qPrintable(tr->error));
  }
};

QTEST_MAIN(ConnectionTestTest)
#include "connectiontest_test.moc"
