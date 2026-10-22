#include <QtTest>

#include "envvar.h"

using namespace uzume;

class EnvVarTest : public QObject {
  Q_OBJECT
 private slots:
  void cleanup() {
    qunsetenv("UZUME_FORCE_RELAY");
    qunsetenv("UZUME_LOCAL_SERVER_PORT");
  }
  void readsTheUzumeName() {
    QVERIFY(!env::isSet("UZUME_FORCE_RELAY"));
    QVERIFY(env::get("UZUME_FORCE_RELAY").isEmpty());
    qputenv("UZUME_FORCE_RELAY", "1");
    QVERIFY(env::isSet("UZUME_FORCE_RELAY"));
    QCOMPARE(env::get("UZUME_FORCE_RELAY"), QStringLiteral("1"));
  }
  void intValueAndBytes() {
    qputenv("UZUME_LOCAL_SERVER_PORT", "8555");
    bool ok = false;
    QCOMPARE(env::intValue("UZUME_LOCAL_SERVER_PORT", &ok), 8555);
    QVERIFY(ok);
    QCOMPARE(env::bytes("UZUME_LOCAL_SERVER_PORT"), QByteArray("8555"));
  }
};

QTEST_GUILESS_MAIN(EnvVarTest)
#include "envvar_test.moc"
