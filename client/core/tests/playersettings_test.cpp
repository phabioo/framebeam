#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "playersettings.h"

using namespace framebeam;
using A = PlayerSettings::Appearance;

class PlayerSettingsTest : public QObject {
  Q_OBJECT
 private slots:
  void defaultsToDark() {
    QTemporaryDir dir;
    PlayerSettings s(dir.path());
    QCOMPARE(s.appearance(), A::Dark);
    QVERIFY(!QFile::exists(s.filePath()));  // nothing is written until something changes
  }

  void diagnosticsStatesDefaultAndPersist() {  // 0.6 D5
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QVERIFY(!s.diagnosticsOpen());
      QVERIFY(s.diagnosticsEmulationOpen());
      QVERIFY(s.diagnosticsStreamingOpen());
      QVERIFY(s.setDiagnosticsOpen(true));
      QVERIFY(s.setDiagnosticsEmulationOpen(false));
    }
    {
      PlayerSettings s(dir.path());
      QVERIFY(s.diagnosticsOpen());
      QVERIFY(!s.diagnosticsEmulationOpen());
      QVERIFY(s.diagnosticsStreamingOpen());  // untouched stays at the default
      QVERIFY(s.setAppearance(A::Light));      // other keys survive and vice versa
      QVERIFY(s.setDiagnosticsStreamingOpen(false));
    }
    PlayerSettings s(dir.path());
    QCOMPARE(s.appearance(), A::Light);
    QVERIFY(s.diagnosticsOpen() && !s.diagnosticsEmulationOpen() && !s.diagnosticsStreamingOpen());
  }

  void persistsAppearance() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QVERIFY(s.setAppearance(A::Light));
    }
    QCOMPARE(PlayerSettings(dir.path()).appearance(), A::Light);
    {
      PlayerSettings s(dir.path());
      QVERIFY(s.setAppearance(A::System));
    }
    QCOMPARE(PlayerSettings(dir.path()).appearance(), A::System);
    QVERIFY(PlayerSettings(dir.path()).filePath().endsWith(QLatin1String("settings/player.json")));
  }

  void corruptedOrUnknownFallsBackToDark() {
    QTemporaryDir dir;
    PlayerSettings probe(dir.path());
    QVERIFY(probe.setAppearance(A::Light));
    QFile f(probe.filePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{not json");
    f.close();
    QCOMPARE(PlayerSettings(dir.path()).appearance(), A::Dark);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{\"appearance\":\"neon\"}");
    f.close();
    QCOMPARE(PlayerSettings(dir.path()).appearance(), A::Dark);
  }

  void preservesUnknownKeys() {
    QTemporaryDir dir;
    PlayerSettings probe(dir.path());
    QVERIFY(probe.setAppearance(A::Dark));
    QFile f(probe.filePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{\"appearance\":\"dark\",\"other\":{\"x\":1}}");
    f.close();
    PlayerSettings s(dir.path());
    QVERIFY(s.setAppearance(A::Light));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(o.value(QStringLiteral("appearance")).toString(), QStringLiteral("light"));
    QCOMPARE(o.value(QStringLiteral("other")).toObject().value(QStringLiteral("x")).toInt(), 1);
  }

  void names() {
    QCOMPARE(PlayerSettings::parseAppearance(QStringLiteral(" System ")), A::System);
    QCOMPARE(PlayerSettings::appearanceName(A::Light), QStringLiteral("light"));
    QCOMPARE(PlayerSettings::parseAppearance(QStringLiteral("x"), A::Light), A::Light);
  }

  void sessionVisibilityRoundTrip() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QVERIFY(s.sessionVisibility().isEmpty());
      QVERIFY(!s.setSessionVisibility(QStringLiteral("public")));
      QVERIFY(!s.setSessionVisibility(QString()));
      QVERIFY(s.sessionVisibility().isEmpty());
      QVERIFY(s.setSessionVisibility(QStringLiteral("invite_only")));
    }
    QCOMPARE(PlayerSettings(dir.path()).sessionVisibility(), QStringLiteral("invite_only"));
  }

  void saveSlotPerHubAndGame() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QCOMPARE(s.saveSlot(QStringLiteral("hub-a"), QStringLiteral("g1")), QStringLiteral("default"));
      QVERIFY(!s.setSaveSlot(QStringLiteral("hub-a"), QStringLiteral("g1"), QStringLiteral("Bad Name")));
      QVERIFY(s.setSaveSlot(QStringLiteral("hub-a"), QStringLiteral("g1"), QStringLiteral("boss")));
      QVERIFY(s.setSaveSlot(QStringLiteral("hub-b"), QStringLiteral("g1"), QStringLiteral("other")));
      QVERIFY(s.setAppearance(A::Light));
    }
    PlayerSettings s(dir.path());  // persisted, unrelated keys kept
    QCOMPARE(s.saveSlot(QStringLiteral("hub-a"), QStringLiteral("g1")), QStringLiteral("boss"));
    QCOMPARE(s.saveSlot(QStringLiteral("hub-b"), QStringLiteral("g1")), QStringLiteral("other"));
    QCOMPARE(s.saveSlot(QStringLiteral("hub-a"), QStringLiteral("g2")), QStringLiteral("default"));
    QCOMPARE(s.appearance(), A::Light);
    QVERIFY(s.setSaveSlot(QStringLiteral("hub-a"), QStringLiteral("g1"), QStringLiteral("default")));
    QCOMPARE(PlayerSettings(dir.path()).saveSlot(QStringLiteral("hub-a"), QStringLiteral("g1")), QStringLiteral("default"));
  }

  void librarySortAndReadyFirstPersistPerDevice() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QCOMPARE(s.librarySort(), QStringLiteral("name_asc"));
      QVERIFY(!s.libraryReadyFirst());
      QVERIFY(!s.setLibrarySort(QStringLiteral("random")));
      QCOMPARE(s.librarySort(), QStringLiteral("name_asc"));
      QVERIFY(s.setLibrarySort(QStringLiteral("size_asc")));
      QVERIFY(s.setLibraryReadyFirst(true));
      QVERIFY(s.setAppearance(A::Light));
    }
    PlayerSettings s(dir.path());
    QCOMPARE(s.librarySort(), QStringLiteral("size_asc"));
    QVERIFY(s.libraryReadyFirst());
    QCOMPARE(s.appearance(), A::Light);  // unrelated keys kept
  }

  void romCacheLimitDefaultsTo20GbAndPersists() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QCOMPARE(s.romCacheLimitBytes(), qint64(20) * 1024 * 1024 * 1024);
      QVERIFY(!s.setRomCacheLimitBytes(-1));
      QCOMPARE(s.romCacheLimitBytes(), PlayerSettings::kDefaultRomCacheLimitBytes);
      QVERIFY(s.setRomCacheLimitBytes(5ll * 1024 * 1024 * 1024));
    }
    {
      PlayerSettings s(dir.path());
      QCOMPARE(s.romCacheLimitBytes(), qint64(5) * 1024 * 1024 * 1024);
      QVERIFY(s.setRomCacheLimitBytes(0));  // unlimited
    }
    PlayerSettings s(dir.path());
    QCOMPARE(s.romCacheLimitBytes(), qint64(0));
  }

  void lastPlayedIsSeparatedByHub() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QCOMPARE(s.lastPlayed(QStringLiteral("hub-a"), QStringLiteral("g1")), qint64(0));
      QVERIFY(!s.setLastPlayed(QString(), QStringLiteral("g1"), 5));
      QVERIFY(!s.setLastPlayed(QStringLiteral("hub-a"), QString(), 5));
      QVERIFY(!s.setLastPlayed(QStringLiteral("hub-a"), QStringLiteral("g1"), 0));
      QVERIFY(s.setLastPlayed(QStringLiteral("hub-a"), QStringLiteral("g1"), 1760000000000LL));
      QVERIFY(s.setLastPlayed(QStringLiteral("hub-a"), QStringLiteral("g2"), 1760000001000LL));
      QVERIFY(s.setLastPlayed(QStringLiteral("hub-b"), QStringLiteral("g1"), 1770000000000LL));
    }
    PlayerSettings s(dir.path());
    QCOMPARE(s.lastPlayed(QStringLiteral("hub-a"), QStringLiteral("g1")), 1760000000000LL);
    QCOMPARE(s.lastPlayed(QStringLiteral("hub-b"), QStringLiteral("g1")), 1770000000000LL);
    QCOMPARE(s.lastPlayed(QStringLiteral("hub-b"), QStringLiteral("g2")), qint64(0));  // same game id on another Hub stays separate
    const QHash<QString, qint64> a = s.lastPlayedAll(QStringLiteral("hub-a"));
    QCOMPARE(a.size(), 2);
    QCOMPARE(a.value(QStringLiteral("g2")), 1760000001000LL);
    QVERIFY(s.lastPlayedAll(QStringLiteral("hub-c")).isEmpty());
  }
};

QTEST_GUILESS_MAIN(PlayerSettingsTest)
#include "playersettings_test.moc"
