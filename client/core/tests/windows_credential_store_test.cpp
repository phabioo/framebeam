// Nur unter Windows gebaut/registriert (siehe core/CMakeLists.txt). Nutzt einen Dummy-Wert.
#include <QtTest>

#include "credentialstore.h"

using namespace framebeam;

class WindowsCredentialStoreTest : public QObject {
  Q_OBJECT
 private slots:
  void roundTrip() {
#ifdef Q_OS_WIN
    WindowsCredentialStore store;
    const QString target = credentialTarget(QStringLiteral("test-hub"), QStringLiteral("test-device-") + QString::number(QCoreApplication::applicationPid()));
    QVERIFY(store.write(target, QStringLiteral("fbd_DUMMY_TEST_VALUE")));
    QCOMPARE(store.read(target).value_or(QString()), QStringLiteral("fbd_DUMMY_TEST_VALUE"));
    QVERIFY(store.write(target, QStringLiteral("fbd_DUMMY_TEST_VALUE_2")));
    QCOMPARE(store.read(target).value_or(QString()), QStringLiteral("fbd_DUMMY_TEST_VALUE_2"));
    QVERIFY(store.remove(target));
    QVERIFY(!store.read(target).has_value());
    QVERIFY(store.remove(target));  // nicht vorhanden ist kein Fehler
#else
    QSKIP("Nur Windows");
#endif
  }
};

QTEST_GUILESS_MAIN(WindowsCredentialStoreTest)
#include "windows_credential_store_test.moc"
