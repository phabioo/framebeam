#include <QCryptographicHash>
#include <QtTest>

#include "securitycode.h"

using namespace uzume;

class SecurityCodeTest : public QObject {
  Q_OBJECT
 private slots:
  // The vectors of docs/reference/protocol.md ("Security code and setup code"); the input is the DER encoding.
  void vectorsOfTheProtocolReference_data() {
    QTest::addColumn<QByteArray>("der");
    QTest::addColumn<QString>("code");
    QByteArray seq;
    for (int i = 0; i < 32; ++i) {
      seq.append(static_cast<char>(i));
    }
    QTest::newRow("ascii") << QByteArray("uzume test certificate") << QStringLiteral("QX7A-7H6Y-J729");
    QTest::newRow("empty") << QByteArray() << QStringLiteral("WERC-8GMR-ZGE1");
    QTest::newRow("bytes 00..1F") << seq << QStringLiteral("CC6W-TAB6-RGSP");
  }
  void vectorsOfTheProtocolReference() {
    QFETCH(QByteArray, der);
    QFETCH(QString, code);
    const QByteArray digest = QCryptographicHash::hash(der, QCryptographicHash::Sha256);
    QCOMPARE(securityCodeFromDigest(digest), code);
    // The pinned fingerprint is that digest as uppercase hex with colons.
    QString fp;
    const QByteArray hex = digest.toHex().toUpper();
    for (int i = 0; i < hex.size(); i += 2) {
      if (i > 0) fp += QLatin1Char(':');
      fp += QString::fromLatin1(hex.mid(i, 2));
    }
    QCOMPARE(securityCodeFromFingerprint(fp), code);
    QCOMPARE(securityCodeFromFingerprint(fp.toLower()), code);
    QCOMPARE(securityCodeFromFingerprint(QString::fromLatin1(hex)), code);  // without colons
  }

  void badInputGivesNoCode() {
    QVERIFY(securityCodeFromDigest(QByteArray()).isEmpty());
    QVERIFY(securityCodeFromDigest(QByteArray(7, 'a')).isEmpty());
    QVERIFY(securityCodeFromFingerprint(QString()).isEmpty());
    QVERIFY(securityCodeFromFingerprint(QStringLiteral("AA:BB")).isEmpty());
    QVERIFY(securityCodeFromFingerprint(QString(64, QLatin1Char('Z'))).isEmpty());
  }

  void normalizeAndFormat() {
    QCOMPARE(normalizeCode(QStringLiteral(" k7q4-9wmp 2hxd ")), QStringLiteral("K7Q49WMP2HXD"));
    QCOMPARE(normalizeCode(QStringLiteral("k7q4\t9wmp\n2hxd")), QStringLiteral("K7Q49WMP2HXD"));
    QCOMPARE(formatSecurityCode(QStringLiteral("k7q49wmp2hxd")), QStringLiteral("K7Q4-9WMP-2HXD"));
    QCOMPARE(formatSecurityCode(QStringLiteral("K7Q4 9WMP 2HXD")), QStringLiteral("K7Q4-9WMP-2HXD"));
    QCOMPARE(formatSecurityCode(QStringLiteral("abc")), QStringLiteral("ABC"));  // not a security code: only normalized
    QCOMPARE(groupCode(QStringLiteral("7h4k2q"), 4), QStringLiteral("7H4K-2Q"));
    QVERIFY(isCrockford(QStringLiteral("0123456789ABCDEFGHJKMNPQRSTVWXYZ")));
    QVERIFY(!isCrockford(QStringLiteral("K7Q4I")));  // I, L, O, U are not in the alphabet
    QVERIFY(!isCrockford(QString()));
  }
};

QTEST_GUILESS_MAIN(SecurityCodeTest)
#include "securitycode_test.moc"
