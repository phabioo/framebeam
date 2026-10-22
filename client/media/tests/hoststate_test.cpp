// Host state signal over fb-diag (ADR 0026 D3): message codec, validation and compatibility with the older rx reports.
#include <QtTest>

#include "bitratecontroller.h"
#include "hoststate.h"
#include "processguard.h"

using namespace uzume;

class HostStateTest : public QObject {
  Q_OBJECT
 private slots:
  void roundTrip() {
    const auto p = parseHostState(makeHostState(true, QStringLiteral("Max")));
    QVERIFY(p.has_value());
    QVERIFY(p->paused);
    QCOMPARE(p->by, QStringLiteral("Max"));
    const auto r = parseHostState(makeHostState(false, QStringLiteral("Max")));
    QVERIFY(r.has_value());
    QVERIFY(!r->paused);
    QVERIFY(r->by.isEmpty());  // "running" carries no name
  }

  void wireFormatIsStable() {
    QCOMPARE(makeHostState(true, QStringLiteral("Max")), QByteArray("{\"by\":\"Max\",\"paused\":true,\"t\":\"state\"}"));
    QCOMPARE(makeHostState(false, QString()), QByteArray("{\"paused\":false,\"t\":\"state\"}"));
  }

  void nameIsOptionalAndSanitized() {
    const auto noName = parseHostState("{\"t\":\"state\",\"paused\":true}");
    QVERIFY(noName.has_value() && noName->paused && noName->by.isEmpty());
    const auto wrongType = parseHostState("{\"t\":\"state\",\"paused\":true,\"by\":12}");
    QVERIFY(wrongType.has_value() && wrongType->by.isEmpty());  // invalid optional field: dropped, message stays valid
    const auto ctrl = parseHostState(QByteArray("{\"t\":\"state\",\"paused\":true,\"by\":\"Ma\\u0007x\"}"));
    QVERIFY(ctrl.has_value());
    QCOMPARE(ctrl->by, QStringLiteral("Max"));
    const auto longName = parseHostState(makeHostState(true, QString(200, QLatin1Char('x'))));
    QVERIFY(longName.has_value());
    QCOMPARE(longName->by.size(), 64);
  }

  void unknownFieldsAreIgnored() {
    const auto p = parseHostState("{\"t\":\"state\",\"paused\":false,\"future\":[1,2]}");
    QVERIFY(p.has_value() && !p->paused);
  }

  void malformedIsRejected() {
    for (const char* bad : {"", "nope", "[]", "{\"t\":\"state\"}", "{\"t\":\"state\",\"paused\":\"yes\"}", "{\"paused\":true}",
                            "{\"t\":\"rx\",\"paused\":true}"}) {
      QVERIFY2(!parseHostState(bad).has_value(), bad);
    }
    QVERIFY(!parseHostState(QByteArray(600, 'x')).has_value());
  }

  // Old and new messages share the channel without confusing each other: a host (or an older Player) that reads a state
  // message as an rx report ignores it, and a viewer that reads an rx report as a state ignores that too.
  void messageTypesDoNotCollide() {
    QVERIFY(!parseRxReport(makeHostState(true, QStringLiteral("Max"))).has_value());
    QVERIFY(!parseHostState(makeRxReport(0.01, 900.0, 59.9, QStringLiteral("h264"))).has_value());
  }
};

FB_TEST_MAIN(HostStateTest)
#include "hoststate_test.moc"
