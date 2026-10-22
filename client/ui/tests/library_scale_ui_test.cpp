// Library at scale (500 / 1000 / 2000 games) against the fake Hub: the library load and a reload with a local sync state
// for every game must stay fast (regression guard for the quadratic rebuild chain).
// The bounds are deliberately generous so slow CI machines do not flake; homebrew-style dummy bytes only.
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QtTest>

#include "fakehub.h"
#include "savestore.h"
#include "savesync.h"
#include "testsupport.h"

using namespace uzume;
using namespace uzume::ui;
using uitest::Harness;
using S = HubConnection::State;

namespace {
const QString kHubId = QStringLiteral("hub-scale");
const QStringList kFeatures{QStringLiteral("saves_v1"), QStringLiteral("saves_v2"), QStringLiteral("saves_v3"),
                            QStringLiteral("saves_v4"), QStringLiteral("saves_v5"), QStringLiteral("sessions_v1")};
}  // namespace

class LibraryScaleUiTest : public QObject {
  Q_OBJECT

  static void fill(FakeHub& hub, int n) {
    hub.hubId = kHubId;
    hub.features = kFeatures;
    QJsonArray games;
    for (int i = 0; i < n; ++i) {
      const QByteArray rom = "homebrew-scale-rom-" + QByteArray::number(i);
      const QString sha = uitest::sha256Hex(rom);
      hub.roms.insert(sha, rom);
      games.append(uitest::gameJson(QStringLiteral("g%1").arg(i), QStringLiteral("Game %1").arg(i, 5, 10, QLatin1Char('0')), sha, rom.size()));
    }
    hub.games = QJsonObject{{QStringLiteral("games"), games}};
  }

 private slots:
  void initTestCase() { qunsetenv("UZUME_MELONDS_DS_CORE"); }

  void loadAndReloadStayFast_data() {
    QTest::addColumn<int>("games");
    QTest::newRow("500") << 500;
    QTest::newRow("1000") << 1000;
    QTest::newRow("2000") << 2000;
  }

  void loadAndReloadStayFast() {
    QFETCH(int, games);
    FakeHub hub(QStringLiteral("a"));
    fill(hub, games);
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    hub.decision = FakeHub::Decision::Approve;
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsPairing, 8000);
    QElapsedTimer t;
    t.start();
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 600000);
    const qint64 firstMs = t.elapsed();
    QCOMPARE(h.controller->library()->rowCount(), games);

    // A local sync state for every game: pending local changes, so refreshKinds reports a change for each of them.
    for (int i = 0; i < games; ++i) {
      const QString id = QStringLiteral("g%1").arg(i);
      const QString dir = h.controller->saveSync()->slotDirFor(id, QStringLiteral("default"));
      QVERIFY(!dir.isEmpty());
      QDir().mkpath(dir);
      SyncState st;
      st.pending = true;
      st.lastSyncedSha256 = uitest::sha256Hex("scale-base");
      QVERIFY(SaveStore::saveState(dir, st));
    }
    t.restart();
    h.controller->reloadLibrary();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 600000);
    const qint64 reloadMs = t.elapsed();
    QCOMPARE(h.controller->library()->rowCount(), games);

    fprintf(stderr, "SCALE games=%d first_load_ms=%lld reload_with_sync_states_ms=%lld\n", games, static_cast<long long>(firstMs),
            static_cast<long long>(reloadMs));
    QVERIFY2(firstMs < 15000, qPrintable(QStringLiteral("first load took %1 ms").arg(firstMs)));
    QVERIFY2(reloadMs < 15000, qPrintable(QStringLiteral("reload took %1 ms").arg(reloadMs)));
  }
};

QTEST_MAIN(LibraryScaleUiTest)
#include "library_scale_ui_test.moc"
