#include <QDir>
#include <QFile>
#include <QFileInfo>
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

  void chooseBaseDirPortableOrFallback() {
    QTemporaryDir app;
    const auto c = ProfileStore::chooseBaseDir(app.path(), QStringLiteral("/appdata"));
    QVERIFY(c.portable);
    QCOMPARE(c.path, QDir(app.path()).filePath(QStringLiteral("data")));
    QVERIFY(QDir(c.path).entryList(QDir::Files | QDir::Hidden).isEmpty());  // Schreibtest raeumt auf
    // nicht beschreibbar: "data" ist eine Datei
    QTemporaryDir bad;
    QFile f(QDir(bad.path()).filePath(QStringLiteral("data")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    const auto b = ProfileStore::chooseBaseDir(bad.path(), QStringLiteral("/appdata"));
    QVERIFY(!b.portable);
    QCOMPARE(b.path, QStringLiteral("/appdata"));
    QVERIFY(!ProfileStore::chooseBaseDir(QString(), QStringLiteral("/appdata")).portable);
  }

  void migrationCopiesWithoutOverwriteOrDelete() {
    QTemporaryDir oldDir, newDir;
    auto write = [](const QString& base, const QString& rel, const QByteArray& data) {
      const QString p = QDir(base).filePath(rel);
      QDir().mkpath(QFileInfo(p).absolutePath());
      QFile f(p);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(data);
    };
    HubProfile p;
    p.hubId = QStringLiteral("hub-1");
    p.address = QStringLiteral("https://192.0.2.10:8443");
    QString devId;
    {
      ProfileStore old(oldDir.path());
      devId = old.deviceId();
      QVERIFY(old.upsertProfile(p));
    }
    write(oldDir.path(), QStringLiteral("hubs/hub-1/saves/a.sav"), "old-save");
    write(oldDir.path(), QStringLiteral("cache/roms/x.rom"), "rom");
    write(newDir.path(), QStringLiteral("hubs/hub-1/saves/a.sav"), "keep-me");

    QVERIFY(ProfileStore::migrateLegacyData(oldDir.path(), newDir.path()) > 0);
    ProfileStore ns(newDir.path());
    QCOMPARE(ns.deviceId(), devId);
    QVERIFY(ns.profile(QStringLiteral("hub-1")).has_value());
    QFile keep(QDir(newDir.path()).filePath(QStringLiteral("hubs/hub-1/saves/a.sav")));
    QVERIFY(keep.open(QIODevice::ReadOnly));
    QCOMPARE(keep.readAll(), QByteArray("keep-me"));
    QVERIFY(!QFileInfo::exists(QDir(newDir.path()).filePath(QStringLiteral("cache/roms/x.rom"))));
    QVERIFY(QFileInfo::exists(QDir(oldDir.path()).filePath(QStringLiteral("hubs/hub-1/saves/a.sav"))));
    QVERIFY(QFileInfo::exists(QDir(oldDir.path()).filePath(QStringLiteral("profiles.json"))));
    // zweiter Lauf: profiles.json existiert -> nichts mehr
    QCOMPARE(ProfileStore::migrateLegacyData(oldDir.path(), newDir.path()), 0);
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
