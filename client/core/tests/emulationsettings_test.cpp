#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "emulationsettings.h"

using namespace framebeam;
using L = EmulationSettings::Level;
using S = EmulationSettings::Source;

class EmulationSettingsTest : public QObject {
  Q_OBJECT
 private slots:
  void emptyByDefaultAndNothingWritten() {
    QTemporaryDir dir;
    EmulationSettings s(dir.path());
    QVERIFY(s.mergedOverrides(QStringLiteral("nds"), QStringLiteral("g1")).isEmpty());
    QVERIFY(!QFile::exists(s.filePath()));
    QVERIFY(s.filePath().endsWith(QStringLiteral("settings/emulation.json")));
  }

  // game > system/core > global > manifest default > core default
  void hierarchyResolution() {
    QTemporaryDir dir;
    EmulationSettings s(dir.path());
    const QString k = QStringLiteral("melonds_x");
    const QString nds = QStringLiteral("nds");
    const QString g = QStringLiteral("g1");
    auto r = s.resolve(k, nds, g, QString(), QStringLiteral("core"));
    QCOMPARE(r.value, QStringLiteral("core"));
    QCOMPARE(r.source, S::Core);
    r = s.resolve(k, nds, g, QStringLiteral("manifest"), QStringLiteral("core"));
    QCOMPARE(r.value, QStringLiteral("manifest"));
    QCOMPARE(r.source, S::Manifest);
    QVERIFY(s.setValue(L::Global, QString(), k, QStringLiteral("global")));
    r = s.resolve(k, nds, g, QStringLiteral("manifest"), QStringLiteral("core"));
    QCOMPARE(r.value, QStringLiteral("global"));
    QCOMPARE(r.source, S::Global);
    QVERIFY(s.setValue(L::System, nds, k, QStringLiteral("system")));
    r = s.resolve(k, nds, g, QStringLiteral("manifest"), QStringLiteral("core"));
    QCOMPARE(r.value, QStringLiteral("system"));
    QCOMPARE(r.source, S::System);
    QVERIFY(s.setValue(L::Game, g, k, QStringLiteral("game")));
    r = s.resolve(k, nds, g, QStringLiteral("manifest"), QStringLiteral("core"));
    QCOMPARE(r.value, QStringLiteral("game"));
    QCOMPARE(r.source, S::Game);
    // Another game / system does not see it.
    QCOMPARE(s.resolve(k, nds, QStringLiteral("g2"), QString(), QString()).source, S::System);
    QCOMPARE(s.resolve(k, QStringLiteral("gba"), QString(), QString(), QStringLiteral("c")).source, S::Global);
    QCOMPARE(s.mergedOverrides(nds, g).value(k), QStringLiteral("game"));
    QCOMPARE(s.mergedOverrides(nds, QString()).value(k), QStringLiteral("system"));
  }

  // Reset removes the key (only explicit overrides are stored) and inheritance is restored.
  void resetRestoresInheritance() {
    QTemporaryDir dir;
    EmulationSettings s(dir.path());
    const QString k = QStringLiteral("k");
    const QString nds = QStringLiteral("nds");
    QVERIFY(s.setValue(L::Global, QString(), k, QStringLiteral("g")));
    QVERIFY(s.setValue(L::System, nds, k, QStringLiteral("s")));
    QVERIFY(s.removeValue(L::System, nds, k));
    QVERIFY(!s.hasValue(L::System, nds, k));
    QCOMPARE(s.resolve(k, nds, QString(), QString(), QString()).source, S::Global);
    QVERIFY(s.removeValue(L::Global, QString(), k));
    QCOMPARE(s.resolve(k, nds, QString(), QString(), QStringLiteral("c")).source, S::Core);
    // Nothing is left behind in the file.
    QFile f(s.filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(QJsonDocument::fromJson(f.readAll()).object().isEmpty());
    QVERIFY(s.removeValue(L::Game, QStringLiteral("zz"), k));  // unknown: no error
  }

  void persistsAndPreservesUnknownKeys() {
    QTemporaryDir dir;
    QDir().mkpath(dir.path() + QStringLiteral("/settings"));
    {
      QFile f(dir.path() + QStringLiteral("/settings/emulation.json"));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(R"({"future":{"a":1},"systems":{"nds":{"core":"melonds_ds"}}})");
    }
    {
      EmulationSettings s(dir.path());
      QVERIFY(s.setValue(L::System, QStringLiteral("nds"), QStringLiteral("k"), QStringLiteral("v")));
      QVERIFY(s.setValue(L::Game, QStringLiteral("g1"), QStringLiteral("k"), QStringLiteral("w")));
    }
    EmulationSettings s(dir.path());
    QCOMPARE(s.value(L::System, QStringLiteral("nds"), QStringLiteral("k")), QStringLiteral("v"));
    QCOMPARE(s.value(L::Game, QStringLiteral("g1"), QStringLiteral("k")), QStringLiteral("w"));
    QVERIFY(s.removeValue(L::System, QStringLiteral("nds"), QStringLiteral("k")));
    QFile f(s.filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QVERIFY(o.contains(QStringLiteral("future")));
    QCOMPARE(o.value(QStringLiteral("systems")).toObject().value(QStringLiteral("nds")).toObject().value(QStringLiteral("core")).toString(),
             QStringLiteral("melonds_ds"));
  }

  void corruptedFileYieldsDefaults() {
    QTemporaryDir dir;
    QDir().mkpath(dir.path() + QStringLiteral("/settings"));
    QFile f(dir.path() + QStringLiteral("/settings/emulation.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{not json");
    f.close();
    EmulationSettings s(dir.path());
    QVERIFY(s.values(L::Global, QString()).isEmpty());
  }
};

QTEST_APPLESS_MAIN(EmulationSettingsTest)
#include "emulationsettings_test.moc"
