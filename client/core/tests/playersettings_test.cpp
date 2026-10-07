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
};

QTEST_GUILESS_MAIN(PlayerSettingsTest)
#include "playersettings_test.moc"
