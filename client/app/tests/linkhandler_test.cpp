#include <QLocalSocket>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

#include "linkhandler.h"

using namespace uzume::app;

class LinkHandlerTest : public QObject {
  Q_OBJECT
 private slots:
  void findsTheLinkArgument() {
    QCOMPARE(findLinkArgument({"uzume", "--dev-allow-http"}), QString());
    QCOMPARE(findLinkArgument({"uzume", "uzume://join/pi.local/UZ-8XK2-M4QD#AA"}), QString("uzume://join/pi.local/UZ-8XK2-M4QD#AA"));
    QCOMPARE(findLinkArgument({"uzume", "UZUME://join/x"}), QString("UZUME://join/x"));
    QCOMPARE(findLinkArgument({"uzume", "https://example.org"}), QString());
  }

  void serverNameIsPerUserAndDataDir() {
    const QString a = instanceServerName("max", "");
    QCOMPARE(a, instanceServerName("max", ""));
    QVERIFY(a != instanceServerName("anna", ""));
    QVERIFY(a != instanceServerName("max", "/tmp/data"));
    QVERIFY(a.startsWith("uzume-instance-"));
  }

  void openCommandQuotesTheExecutableAndTheLink() {
    const QString c = schemeOpenCommand("/opt/uzume/Uzume.exe");
    QVERIFY(c.startsWith('"'));
    QVERIFY(c.endsWith(" \"%1\""));
    QVERIFY(c.contains("Uzume.exe\" "));
  }

  void runningInstanceReceivesTheLink() {
    const QString name = instanceServerName("test", QUuid::createUuid().toString());
    SingleInstance first(name);
    QVERIFY(first.acquire(QString()));
    QSignalSpy spy(&first, &SingleInstance::linkReceived);
    QSignalSpy act(&first, &SingleInstance::activateRequested);
    // The second start would run in another process; here a plain socket plays it so the event loop of `first` keeps running.
    QLocalSocket probe;
    probe.connectToServer(name);
    QVERIFY(probe.waitForConnected(1000));
    probe.write("uzume://join/pi.local/UZ-8XK2-M4QD\n");
    probe.flush();
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("uzume://join/pi.local/UZ-8XK2-M4QD"));
    QCOMPARE(act.count(), 1);
  }
};

QTEST_MAIN(LinkHandlerTest)
#include "linkhandler_test.moc"
