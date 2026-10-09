// Effective core of a game: game > system > Hub default, restricted to the cores the Hub serves (ADR 0020 D6).
#include <QTemporaryDir>
#include <QtTest>

#include "corechoice.h"

using namespace framebeam;
using L = EmulationSettings::Level;

namespace {
SystemInfo hubSystem(const QStringList& served, const QString& def) {
  SystemInfo s;
  s.id = QStringLiteral("nds");
  s.defaultCoreId = def;
  for (const QString& id : served) {
    SystemCore c;
    c.coreId = id;
    c.version = QStringLiteral("2026.10.09");
    s.cores.append(c);
  }
  return s;
}
const QString kKey = QString::fromLatin1(EmulationSettings::kCoreKey);
}  // namespace

class CoreChoiceTest : public QObject {
  Q_OBJECT
 private slots:
  void hubDefaultWithoutSettings() {
    QTemporaryDir dir;
    EmulationSettings st(dir.path());
    const CoreChoice c = chooseCore(hubSystem({QStringLiteral("melondsds"), QStringLiteral("desmume")}, QStringLiteral("desmume")), st, QStringLiteral("g1"));
    QCOMPARE(c.core.coreId, QStringLiteral("desmume"));
    QCOMPARE(c.core.version, QStringLiteral("2026.10.09"));
    QCOMPARE(c.source, QStringLiteral("hub"));
    QVERIFY(c.staleChoice.isEmpty());
  }

  void gameBeatsSystemBeatsHub() {
    QTemporaryDir dir;
    EmulationSettings st(dir.path());
    const SystemInfo sys = hubSystem({QStringLiteral("melondsds"), QStringLiteral("desmume")}, QStringLiteral("melondsds"));
    QVERIFY(st.setValue(L::System, QStringLiteral("nds"), kKey, QStringLiteral("desmume")));
    CoreChoice c = chooseCore(sys, st, QStringLiteral("g1"));
    QCOMPARE(c.core.coreId, QStringLiteral("desmume"));
    QCOMPARE(c.source, QStringLiteral("system"));
    QVERIFY(st.setValue(L::Game, QStringLiteral("g1"), kKey, QStringLiteral("melondsds")));
    c = chooseCore(sys, st, QStringLiteral("g1"));
    QCOMPARE(c.core.coreId, QStringLiteral("melondsds"));
    QCOMPARE(c.source, QStringLiteral("game"));
    // Another game only sees the system choice.
    QCOMPARE(chooseCore(sys, st, QStringLiteral("g2")).core.coreId, QStringLiteral("desmume"));
    // Removing the game override restores inheritance.
    QVERIFY(st.removeValue(L::Game, QStringLiteral("g1"), kKey));
    QCOMPARE(chooseCore(sys, st, QStringLiteral("g1")).source, QStringLiteral("system"));
  }

  void unservedChoiceFallsBackWithNotice() {
    QTemporaryDir dir;
    EmulationSettings st(dir.path());
    const SystemInfo sys = hubSystem({QStringLiteral("melondsds")}, QStringLiteral("melondsds"));
    QVERIFY(st.setValue(L::System, QStringLiteral("nds"), kKey, QStringLiteral("desmume")));
    CoreChoice c = chooseCore(sys, st, QStringLiteral("g1"));
    QCOMPARE(c.core.coreId, QStringLiteral("melondsds"));
    QCOMPARE(c.source, QStringLiteral("hub"));
    QCOMPARE(c.staleChoice, QStringLiteral("desmume"));
    QCOMPARE(c.staleLevel, QStringLiteral("system"));
    // A stale game choice falls through to a served system choice.
    QVERIFY(st.setValue(L::System, QStringLiteral("nds"), kKey, QStringLiteral("melondsds")));
    QVERIFY(st.setValue(L::Game, QStringLiteral("g1"), kKey, QStringLiteral("noods")));
    c = chooseCore(sys, st, QStringLiteral("g1"));
    QCOMPARE(c.source, QStringLiteral("system"));
    QCOMPARE(c.staleChoice, QStringLiteral("noods"));
    QCOMPARE(c.staleLevel, QStringLiteral("game"));
  }

  void legacyHubWithoutCoresV2() {
    QTemporaryDir dir;
    EmulationSettings st(dir.path());
    SystemInfo sys;
    sys.id = QStringLiteral("nds");
    sys.preferredCoreId = QStringLiteral("melonds_ds");
    sys.corePackageVersion = QStringLiteral("1.4.0");
    CoreChoice c = chooseCore(sys, st, QStringLiteral("g1"));
    QCOMPARE(c.core.coreId, QStringLiteral("melonds_ds"));
    QCOMPARE(c.core.version, QStringLiteral("1.4.0"));
    // A stored choice of another core is not served by this Hub.
    QVERIFY(st.setValue(L::System, QStringLiteral("nds"), kKey, QStringLiteral("desmume")));
    c = chooseCore(sys, st, QStringLiteral("g1"));
    QCOMPARE(c.core.coreId, QStringLiteral("melonds_ds"));
    QCOMPARE(c.staleChoice, QStringLiteral("desmume"));
  }

  void aliasMatchesServedCore() {
    QTemporaryDir dir;
    EmulationSettings st(dir.path());
    const SystemInfo sys = hubSystem({QStringLiteral("melondsds"), QStringLiteral("desmume")}, QStringLiteral("desmume"));
    QVERIFY(st.setValue(L::System, QStringLiteral("nds"), kKey, QStringLiteral("melonds_ds")));
    const auto canon = [](const QString& id) { return id == QLatin1String("melonds_ds") ? QStringLiteral("melondsds") : id; };
    const CoreChoice c = chooseCore(sys, st, QString(), canon);
    QCOMPARE(c.core.coreId, QStringLiteral("melondsds"));
    QCOMPARE(c.source, QStringLiteral("system"));
    QVERIFY(c.staleChoice.isEmpty());
  }

  void noCoreOnTheHub() {
    QTemporaryDir dir;
    EmulationSettings st(dir.path());
    const CoreChoice c = chooseCore(hubSystem({}, QString()), st, QStringLiteral("g1"));
    QVERIFY(!c.valid());
    QCOMPARE(c.source, QStringLiteral("none"));
  }
};

QTEST_GUILESS_MAIN(CoreChoiceTest)
#include "corechoice_test.moc"
