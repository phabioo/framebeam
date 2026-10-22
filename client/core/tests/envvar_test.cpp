#include <QtTest>

#include "envvar.h"

using namespace uzume;

class EnvVarTest : public QObject {
  Q_OBJECT
 private slots:
  void cleanup() {
    for (const char* n : {"UZUME_APP_UPDATE_INDEX_URL", "FRAMEBEAM_PLAYER_UPDATE_INDEX_URL", "UZUME_LOCAL_SERVER_PORT",
                          "FRAMEBEAM_LOCAL_HUB_PORT", "UZUME_FORCE_RELAY", "FRAMEBEAM_FORCE_RELAY"}) {
      qunsetenv(n);
    }
  }
  void legacyNamesFollowTheRenameTable() {
    QCOMPARE(env::legacyName("UZUME_APP_UPDATE_INDEX_URL"), QByteArray("FRAMEBEAM_PLAYER_UPDATE_INDEX_URL"));
    QCOMPARE(env::legacyName("UZUME_APP_TRUST_KEYS"), QByteArray("FRAMEBEAM_PLAYER_TRUST_KEYS"));
    QCOMPARE(env::legacyName("UZUME_LOCAL_SERVER_PORT"), QByteArray("FRAMEBEAM_LOCAL_HUB_PORT"));
    QCOMPARE(env::legacyName("UZUME_SERVER_CONTAINER"), QByteArray("FRAMEBEAM_HUB_CONTAINER"));
    QCOMPARE(env::legacyName("UZUME_FORCE_RELAY"), QByteArray("FRAMEBEAM_FORCE_RELAY"));
    QCOMPARE(env::legacyName("UZUME_APPLE_X"), QByteArray("FRAMEBEAM_APPLE_X"));  // whole tokens only
    QVERIFY(env::legacyName("SOMETHING_ELSE").isEmpty());
  }
  void newNameWinsAndOldIsTheFallback() {
    QVERIFY(env::get("UZUME_FORCE_RELAY").isEmpty());
    qputenv("FRAMEBEAM_FORCE_RELAY", "1");
    QTest::ignoreMessage(QtWarningMsg, "FRAMEBEAM_FORCE_RELAY is deprecated, use UZUME_FORCE_RELAY");
    QCOMPARE(env::get("UZUME_FORCE_RELAY"), QStringLiteral("1"));
    QVERIFY(env::isSet("UZUME_FORCE_RELAY"));
    qputenv("UZUME_FORCE_RELAY", "0");
    QCOMPARE(env::get("UZUME_FORCE_RELAY"), QStringLiteral("0"));  // the new name takes precedence
  }
  void theWarningIsLoggedOncePerOldName() {
    qputenv("FRAMEBEAM_PLAYER_UPDATE_INDEX_URL", "https://example.invalid/index.json");
    QTest::ignoreMessage(QtWarningMsg, "FRAMEBEAM_PLAYER_UPDATE_INDEX_URL is deprecated, use UZUME_APP_UPDATE_INDEX_URL");
    QCOMPARE(env::get("UZUME_APP_UPDATE_INDEX_URL"), QStringLiteral("https://example.invalid/index.json"));
    // A second read must not warn: QtTest fails the test on an unexpected warning only when it is not ignored, so
    // count through a message handler instead.
    static int warnings = 0;
    const auto previous = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext&, const QString&) {
      if (t == QtWarningMsg) ++warnings;
    });
    (void)env::get("UZUME_APP_UPDATE_INDEX_URL");
    (void)env::bytes("UZUME_APP_UPDATE_INDEX_URL");
    qInstallMessageHandler(previous);
    QCOMPARE(warnings, 0);
  }
  void intValueAndBytesAcceptTheOldName() {
    qputenv("FRAMEBEAM_LOCAL_HUB_PORT", "8555");
    QTest::ignoreMessage(QtWarningMsg, "FRAMEBEAM_LOCAL_HUB_PORT is deprecated, use UZUME_LOCAL_SERVER_PORT");
    bool ok = false;
    QCOMPARE(env::intValue("UZUME_LOCAL_SERVER_PORT", &ok), 8555);
    QVERIFY(ok);
    QCOMPARE(env::bytes("UZUME_LOCAL_SERVER_PORT"), QByteArray("8555"));
  }
};

QTEST_GUILESS_MAIN(EnvVarTest)
#include "envvar_test.moc"
