// UI tests: screens 3a/3b/3c of the Player UI against a fake hub, offscreen.
#include <QSignalSpy>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

class ScreensTest : public QObject {
  Q_OBJECT

  static QString screenOf(Harness& h) { return h.controller->screen(); }

  static QVariantMap pairingOf(Harness& h) { return h.controller->pairing(); }

  static HubProfile profile(const QString& id, const QString& name, const QString& address, const QString& pin = QString()) {
    HubProfile p;
    p.hubId = id;
    p.name = name;
    p.address = address;
    p.pinnedFingerprint = pin;
    p.lastConnected = QDateTime(QDate(2026, 10, 4), QTime(18, 40), QTimeZone::utc());
    return p;
  }

 private slots:
  void initTestCase() {
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");  // core deliberately not set: detail pane shows "missing"
    uitest::installWarningCounter();
  }

  void init() { uitest::warningCount() = 0; }
  void cleanup() {
    QCOMPARE(uitest::warningCount().load(), 0);  // no QML/Qt warnings during the test
  }

  // 3a empty: input field, footer, no hub saved.
  void connectionScreenEmpty() {
    Harness h;
    QVERIFY(h.start());
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QVERIFY(h.item("addressField") != nullptr);
    QVERIFY(h.controller->deviceFooter().startsWith(QStringLiteral("This device: ")));
    QVERIFY(h.controller->deviceFooter().contains(QStringLiteral("Player ")));
    QCOMPARE(h.controller->hubs().size(), 0);
    // Empty address: notice instead of a connection attempt.
    h.controller->addHub(QStringLiteral("  "));
    QVERIFY(!h.controller->connectionNotice().isEmpty());
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    uitest::saveShot(h.window, QStringLiteral("3a-connection-empty"));
  }

  // 3a with cards: saved/ready, certificate changed (blocked), hub too old.
  void connectionScreenCards() {
    FakeHub lena(QStringLiteral("a"));
    lena.hubId = QStringLiteral("hub-lena");
    lena.name = QStringLiteral("Studio Lena");
    QVERIFY(lena.start());
    FakeHub other(QStringLiteral("b"));
    FakeHub old(QStringLiteral("a"));
    old.hubId = QStringLiteral("hub-office");
    old.name = QStringLiteral("Office");
    old.protocolVersion = 2;  // hub requires protocol v2: Player (v1) is too old
    old.minProtocolVersion = 2;
    QVERIFY(old.start());

    Harness h;
    QVERIFY(h.start());
    ProfileStore* ps = h.controller->profileStore();
    HubProfile home = profile(QStringLiteral("hub-home"), QStringLiteral("Home"), QStringLiteral("https://hub.local:8443"));
    QVERIFY(ps->upsertProfile(home));
    QVERIFY(ps->upsertProfile(profile(QStringLiteral("hub-lena"), QStringLiteral("Studio Lena"), lena.address(), other.fingerprint())));
    QVERIFY(ps->upsertProfile(profile(QStringLiteral("hub-office"), QStringLiteral("Office"), old.address(), old.fingerprint())));
    ps->setLastHubId(QStringLiteral("hub-home"));
    h.controller->setAutoConnect(true);
    QVERIFY(ps->autoConnect());
    QCOMPARE(h.controller->hubs().size(), 3);
    const QVariantMap idle = h.controller->hubs().at(0).toMap();
    QCOMPARE(idle.value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
    QVERIFY(idle.value(QStringLiteral("isLast")).toBool());

    // Certificate changed: blocked, no Connect action, both fingerprints present.
    h.controller->connectProfile(QStringLiteral("hub-lena"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::CertificateChanged);
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QVariantMap card;
    for (const QVariant& v : h.controller->hubs()) {
      if (v.toMap().value(QStringLiteral("hubId")).toString() == QLatin1String("hub-lena")) card = v.toMap();
    }
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("certChanged"));
    QCOMPARE(card.value(QStringLiteral("tone")).toString(), QStringLiteral("error"));
    QVERIFY(!card.value(QStringLiteral("expectedFingerprint")).toString().isEmpty());
    QVERIFY(!card.value(QStringLiteral("observedFingerprint")).toString().isEmpty());
    QVERIFY(card.value(QStringLiteral("expectedFingerprint")) != card.value(QStringLiteral("observedFingerprint")));
    QVERIFY(h.controller->connection()->state() != HubConnection::State::NeedsPairing);  // never silently accepted
    uitest::saveShot(h.window, QStringLiteral("3a-connection-cert-changed"));

    // Player too old (hub requires a newer protocol)
    h.controller->connectProfile(QStringLiteral("hub-office"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Incompatible);
    for (const QVariant& v : h.controller->hubs()) {
      if (v.toMap().value(QStringLiteral("hubId")).toString() == QLatin1String("hub-office")) card = v.toMap();
    }
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("incompatible"));
    QCOMPARE(card.value(QStringLiteral("statusText")).toString(), QStringLiteral("Player too old"));
    uitest::saveShot(h.window, QStringLiteral("3a-connection-incompatible"));

    // Remove works on every card (including the blocked one).
    h.controller->removeHub(QStringLiteral("hub-lena"));
    QCOMPARE(h.controller->hubs().size(), 2);
    QVERIFY(!ps->profile(QStringLiteral("hub-lena")).has_value());
  }

  // 3a: not reachable -> card for the attempted address with Retry.
  void unreachableAttempt() {
    Harness h;
    QVERIFY(h.start());
    h.controller->addHub(QStringLiteral("http://127.0.0.1:1"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    const QVariantList hubs = h.controller->hubs();
    QCOMPARE(hubs.size(), 1);
    const QVariantMap c = hubs.at(0).toMap();
    QCOMPARE(c.value(QStringLiteral("status")).toString(), QStringLiteral("unreachable"));
    QVERIFY(!c.value(QStringLiteral("saved")).toBool());
    QVERIFY(!c.value(QStringLiteral("message")).toString().isEmpty());
    uitest::saveShot(h.window, QStringLiteral("3a-connection-unreachable"));
    h.controller->removeHub(QString());  // discard attempt
    QCOMPARE(h.controller->hubs().size(), 0);
  }

  // Regression: every attempt (addHub, Retry) shows the pairing/status phase, not just the first.
  void unreachableTwiceShowsStatusScreen() {
    Harness h;
    QVERIFY(h.start());
    for (int i = 0; i < 2; ++i) {
      h.controller->addHub(QStringLiteral("http://127.0.0.1:1"));
      QCOMPARE(screenOf(h), QStringLiteral("pairing"));
      QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
      QCOMPARE(screenOf(h), QStringLiteral("connection"));
    }
    h.controller->retryConnection();
    QCOMPARE(screenOf(h), QStringLiteral("pairing"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
  }

  // Retry after connectProfile (saved profile) never shows the pairing status screen.
  void retryForSavedProfileStaysOnConnection() {
    Harness h;
    QVERIFY(h.start());
    QVERIFY(h.controller->profileStore()->upsertProfile(
        profile(QStringLiteral("hub-gone"), QStringLiteral("Gone"), QStringLiteral("http://127.0.0.1:1"))));
    h.controller->connectProfile(QStringLiteral("hub-gone"));
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    h.controller->retryConnection();
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
  }

  // addHub on an already paired address (profile with credential): Retry stays on "connection".
  void retryAfterAddHubOfPairedProfileStaysOnConnection() {
    Harness h;
    QVERIFY(h.start());
    HubProfile p = profile(QStringLiteral("hub-paired"), QStringLiteral("Paired"), QStringLiteral("http://127.0.0.1:1"));
    p.credentialRef = QStringLiteral("framebeam/test-credential");
    QVERIFY(h.controller->profileStore()->upsertProfile(p));
    h.controller->addHub(QStringLiteral("http://127.0.0.1:1"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    h.controller->retryConnection();
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
  }

  // 3b + 3c: TOFU confirmation, approval (waiting, denied, approved), Library.
  void pairingAndLibrary() {
    const QByteArray readyRom = "homebrew-dummy-rom-ready";
    const QByteArray badRom = "this-is-not-the-expected-content";
    const QString readySha = uitest::sha256Hex(readyRom);
    const QString badSha = uitest::sha256Hex(QByteArray("expected-but-never-served"));

    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-home");
    hub.name = QStringLiteral("Home");
    hub.decision = FakeHub::Decision::Pending;
    QJsonArray games;
    games.append(uitest::gameJson(QStringLiteral("g1"), QStringLiteral("Lumen Drift"), readySha, readyRom.size()));
    games.append(uitest::gameJson(QStringLiteral("g2"), QStringLiteral("Paper Wizards"), uitest::sha256Hex("pw"), 128LL * 1024 * 1024));
    games.append(uitest::gameJson(QStringLiteral("g3"), QStringLiteral("Orbit Gardens"), uitest::sha256Hex("og"), 16LL * 1024 * 1024));
    games.append(uitest::gameJson(QStringLiteral("g4"), QStringLiteral("Clocktower Kids"), uitest::sha256Hex("ck"), 24LL * 1024 * 1024));
    games.append(uitest::gameJson(QStringLiteral("g5"), QStringLiteral("Copper Courier"), badSha, badRom.size()));
    games.append(uitest::gameJson(QStringLiteral("g6"), QStringLiteral("Stylus Knights"), uitest::sha256Hex("sk"), 32LL * 1024 * 1024));
    hub.games = QJsonObject{{QStringLiteral("games"), games}};
    hub.roms.insert(badSha, badRom);
    QVERIFY(hub.start());

    Harness h;
    QVERIFY(h.start());
    // Lumen Drift is already in the cache (validated).
    {
      QFile f(QDir(h.controller->profileStore()->romCacheDir()).filePath(readySha + QStringLiteral(".nds")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(readyRom);
    }

    h.controller->addHub(hub.address());
    QTRY_COMPARE(screenOf(h), QStringLiteral("pairing"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation);

    // Step 2: fingerprint is shown and must be confirmed explicitly.
    QVariantMap p = pairingOf(h);
    QCOMPARE(p.value(QStringLiteral("phase")).toString(), QStringLiteral("trust"));
    QCOMPARE(p.value(QStringLiteral("hubName")).toString(), QStringLiteral("Home"));
    QCOMPARE(p.value(QStringLiteral("fingerprint")).toString().remove(QLatin1Char('\n')).remove(QLatin1Char(':')),
             PlayerController::formatFingerprint(hub.fingerprint()).remove(QLatin1Char('\n')).remove(QLatin1Char(':')));
    QCOMPARE(p.value(QStringLiteral("fingerprint")).toString().count(QLatin1Char('\n')), 1);
    QVERIFY(h.item("trustButton") != nullptr);
    QVERIFY(h.item("fingerprintText") != nullptr);
    QTest::qWait(100);
    QCOMPARE(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation);  // no silent trust
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-trust"));

    QVERIFY(h.click("trustButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsPairing);
    QCOMPARE(pairingOf(h).value(QStringLiteral("phase")).toString(), QStringLiteral("needsPairing"));
    QVERIFY(h.controller->profileStore()->profile(QStringLiteral("hub-home")).has_value());
    QCOMPARE(h.controller->profileStore()->profile(QStringLiteral("hub-home"))->pinnedFingerprint, hub.fingerprint());

    // Step 3: request approval -> waiting.
    QVERIFY(h.click("requestButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::AwaitingApproval);
    p = pairingOf(h);
    QCOMPARE(p.value(QStringLiteral("phase")).toString(), QStringLiteral("awaiting"));
    QVERIFY(!p.value(QStringLiteral("deviceName")).toString().isEmpty());
    QVERIFY(!p.value(QStringLiteral("platform")).toString().isEmpty());
    QVERIFY(!p.value(QStringLiteral("playerVersion")).toString().isEmpty());
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-waiting"));

    // Cancel request -> back to "Request approval"
    QVERIFY(h.click("cancelRequestButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsPairing);

    // Denied
    hub.decision = FakeHub::Decision::Deny;
    QVERIFY(h.click("requestButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Denied);
    QCOMPARE(pairingOf(h).value(QStringLiteral("phase")).toString(), QStringLiteral("denied"));
    QVERIFY(h.item("pairingProblem") != nullptr && h.item("pairingProblem")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-denied"));

    // Expired
    hub.decision = FakeHub::Decision::Expire;
    QVERIFY(h.click("requestButton"));  // "Request again"
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Expired);
    QCOMPARE(pairingOf(h).value(QStringLiteral("phase")).toString(), QStringLiteral("expired"));
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-expired"));

    // Approved -> Library
    hub.decision = FakeHub::Decision::Approve;
    QVERIFY(h.click("requestButton"));
    QTRY_COMPARE_WITH_TIMEOUT(screenOf(h), QStringLiteral("library"), 8000);
    QTRY_COMPARE(h.controller->libraryState(), QStringLiteral("ready"));
    LibraryModel* lib = h.controller->library();
    QCOMPARE(lib->totalCount(), 6);
    QCOMPARE(h.controller->hubName(), QStringLiteral("Home"));
    QTRY_COMPARE(lib->readyCount(), 1);  // Lumen Drift after asynchronous verification
    QCOMPARE(h.controller->selectedGameId(), QStringLiteral("g1"));  // first game preselected

    // Make hash mismatch visible (hub delivers wrong content).
    h.controller->downloader()->ensureRom(h.controller->hubLibrary()->games().at(4));
    QTRY_COMPARE(lib->data(lib->index(lib->rowOfGame(QStringLiteral("g5"))), LibraryModel::StateKindRole).toString(),
                 QStringLiteral("mismatch"));

    // Detail pane: core missing (path hint), firmware not required, Play disabled.
    QVariantMap g = h.controller->selectedGame();
    QCOMPARE(g.value(QStringLiteral("title")).toString(), QStringLiteral("Lumen Drift"));
    QCOMPARE(g.value(QStringLiteral("systemName")).toString(), QStringLiteral("Nintendo DS"));
    QCOMPARE(g.value(QStringLiteral("romText")).toString(), QStringLiteral("Cached locally · verified"));
    QVERIFY(g.value(QStringLiteral("coreText")).toString().contains(QStringLiteral("missing")));
    QVERIFY(g.value(QStringLiteral("coreHint")).toString().contains(QStringLiteral("FRAMEBEAM_MELONDS_DS_CORE")));
    QCOMPARE(g.value(QStringLiteral("firmwareText")).toString(), QStringLiteral("Not required"));
    QVERIFY(!g.value(QStringLiteral("canPlay")).toBool());
    QCOMPARE(g.value(QStringLiteral("checklist")).toList().size(), 5);
    QCOMPARE(g.value(QStringLiteral("sha")).toString(), readySha);
    QVERIFY(g.value(QStringLiteral("cachePath")).toString().endsWith(readySha + QStringLiteral(".nds")));

    // Selection, search, chips
    h.controller->selectGame(QStringLiteral("g2"));
    g = h.controller->selectedGame();
    QCOMPARE(g.value(QStringLiteral("title")).toString(), QStringLiteral("Paper Wizards"));
    QCOMPARE(g.value(QStringLiteral("romText")).toString(), QStringLiteral("Download needed · 128 MB"));
    h.controller->selectGame(QStringLiteral("g1"));
    lib->setFilterText(QStringLiteral("ts"));  // Stylus Knights
    QCOMPARE(lib->rowCount(), 1);
    lib->setFilterText(QString());
    lib->setReadyOnly(true);
    QCOMPARE(lib->rowCount(), 1);
    lib->setReadyOnly(false);
    QCOMPARE(lib->rowCount(), 6);
    QVERIFY(h.item("gameGrid") != nullptr);
    QVERIFY(h.item("searchField") != nullptr);
    uitest::saveShot(h.window, QStringLiteral("3c-library"));

    // Switch hub = disconnect and back to 3a.
    QVERIFY(h.click("switchHubButton"));
    QTRY_COMPARE(screenOf(h), QStringLiteral("connection"));
    QCOMPARE(lib->totalCount(), 0);
    QCOMPARE(h.controller->hubs().size(), 1);
    QVERIFY(h.controller->hubs().at(0).toMap().value(QStringLiteral("saved")).toBool());
  }

  void autoConnectIsProfileSetting() {
    Harness h;
    QVERIFY(h.start());
    QVERIFY(!h.controller->autoConnect());
    h.controller->setAutoConnect(true);
    QVERIFY(h.controller->autoConnect());
    ProfileStore reread(h.dir.path());
    QVERIFY(reread.autoConnect());
  }

  void fingerprintFormat() {
    const QString fp = QStringLiteral("AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89");
    const QString f = PlayerController::formatFingerprint(fp);
    QCOMPARE(f.count(QLatin1Char('\n')), 1);
    QCOMPARE(f.section(QLatin1Char('\n'), 0, 0).count(QLatin1Char(':')), 15);
    QCOMPARE(f.section(QLatin1Char('\n'), 1, 1).count(QLatin1Char(':')), 15);
    QCOMPARE(PlayerController::platformLabel(QStringLiteral("windows"), QStringLiteral("x86_64")), QStringLiteral("Windows x86-64"));
  }
};

UITEST_MAIN(ScreensTest)
#include "screens_test.moc"
