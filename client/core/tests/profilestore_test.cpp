#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include "credentialstore.h"
#include "profilestore.h"

using namespace framebeam;

class ProfileStoreTest : public QObject {
  Q_OBJECT
 private slots:
  void deviceIdIsStable() {
    QTemporaryDir dir;
    QString id;
    {
      ProfileStore s(dir.path());
      id = s.deviceId();
      QVERIFY(!QUuid::fromString(id).isNull());
      QVERIFY(!s.deviceName().isEmpty());
    }
    ProfileStore again(dir.path());
    QCOMPARE(again.deviceId(), id);
  }

  void profileRoundTripWithoutSecrets() {
    QTemporaryDir dir;
    HubProfile p;
    p.hubId = QStringLiteral("hub-1");
    p.name = QStringLiteral("Wohnzimmer");
    p.address = QStringLiteral("https://192.0.2.10:8443");
    p.hubUserId = QStringLiteral("u_1");
    p.deviceId = QStringLiteral("dev");
    p.credentialRef = credentialTarget(p.hubId, p.deviceId);
    p.pinnedFingerprint = QStringLiteral("AA:BB");
    p.lastConnected = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5), Qt::UTC);
    {
      ProfileStore s(dir.path());
      QVERIFY(s.upsertProfile(p));
      QVERIFY(s.setAutoConnect(true));
      QVERIFY(s.setLastHubId(p.hubId));
    }
    ProfileStore s2(dir.path());
    QCOMPARE(s2.profiles().size(), 1);
    const auto q = s2.profile(p.hubId);
    QVERIFY(q.has_value());
    QCOMPARE(q->name, p.name);
    QCOMPARE(q->credentialRef, QStringLiteral("FrameBeam/hub-1/dev"));
    QCOMPARE(q->pinnedFingerprint, p.pinnedFingerprint);
    QCOMPARE(q->lastConnected, p.lastConnected);
    QVERIFY(s2.autoConnect());
    QCOMPARE(s2.lastHubId(), p.hubId);
    QVERIFY(s2.profileByAddress(QStringLiteral("HTTPS://192.0.2.10:8443")).has_value());
    QVERIFY(s2.removeProfile(p.hubId));
    QVERIFY(s2.profiles().isEmpty());
    QVERIFY(s2.lastHubId().isEmpty());
  }

  void hubDirsAreSeparatedAndSafe() {
    QTemporaryDir dir;
    ProfileStore s(dir.path());
    QVERIFY(s.hubDir(QStringLiteral("a")) != s.hubDir(QStringLiteral("b")));
    QVERIFY(s.hubDir(QStringLiteral("../evil")).isEmpty());
    QVERIFY(s.hubDir(QStringLiteral("a/b")).isEmpty());
    QVERIFY(!s.upsertProfile([] { HubProfile p; p.hubId = QStringLiteral("../x"); return p; }()));
    QVERIFY(s.romCacheDir().endsWith(QStringLiteral("cache/roms")));
  }

  void memoryCredentialStore() {
    MemoryCredentialStore c;
    QVERIFY(!c.read(QStringLiteral("t")).has_value());
    QVERIFY(c.write(QStringLiteral("t"), QStringLiteral("dummy")));
    QCOMPARE(c.read(QStringLiteral("t")).value(), QStringLiteral("dummy"));
    QVERIFY(c.remove(QStringLiteral("t")));
    QVERIFY(!c.read(QStringLiteral("t")).has_value());
  }
};

QTEST_GUILESS_MAIN(ProfileStoreTest)
#include "profilestore_test.moc"
