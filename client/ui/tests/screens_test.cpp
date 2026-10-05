// UI-Tests: Screens 3a/3b/3c der Player-Oberflaeche gegen einen Fake-Hub, offscreen.
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
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");  // Core bewusst nicht gesetzt: Detailspalte zeigt "fehlt"
    uitest::installWarningCounter();
  }

  void init() { uitest::warningCount() = 0; }
  void cleanup() {
    QCOMPARE(uitest::warningCount().load(), 0);  // keine QML-/Qt-Warnungen waehrend des Tests
  }

  // 3a leer: Eingabefeld, Footer, kein Hub gespeichert.
  void connectionScreenEmpty() {
    Harness h;
    QVERIFY(h.start());
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QVERIFY(h.item("addressField") != nullptr);
    QVERIFY(h.controller->deviceFooter().startsWith(QStringLiteral("Dieses Gerät: ")));
    QVERIFY(h.controller->deviceFooter().contains(QStringLiteral("Player ")));
    QCOMPARE(h.controller->hubs().size(), 0);
    // Leere Adresse: Hinweis statt Verbindungsversuch.
    h.controller->addHub(QStringLiteral("  "));
    QVERIFY(!h.controller->connectionNotice().isEmpty());
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    uitest::saveShot(h.window, QStringLiteral("3a-connection-empty"));
  }

  // 3a mit Karten: gespeichert/bereit, Zertifikat geaendert (blockiert), Hub zu alt.
  void connectionScreenCards() {
    FakeHub lena(QStringLiteral("a"));
    lena.hubId = QStringLiteral("hub-lena");
    lena.name = QStringLiteral("Studio Lena");
    QVERIFY(lena.start());
    FakeHub other(QStringLiteral("b"));
    FakeHub old(QStringLiteral("a"));
    old.hubId = QStringLiteral("hub-buero");
    old.name = QStringLiteral("Büro");
    old.protocolVersion = 2;  // Hub verlangt Protokoll v2: Player (v1) ist zu alt
    old.minProtocolVersion = 2;
    QVERIFY(old.start());

    Harness h;
    QVERIFY(h.start());
    ProfileStore* ps = h.controller->profileStore();
    HubProfile zuhause = profile(QStringLiteral("hub-zuhause"), QStringLiteral("Zuhause"), QStringLiteral("https://hub.local:8443"));
    QVERIFY(ps->upsertProfile(zuhause));
    QVERIFY(ps->upsertProfile(profile(QStringLiteral("hub-lena"), QStringLiteral("Studio Lena"), lena.address(), other.fingerprint())));
    QVERIFY(ps->upsertProfile(profile(QStringLiteral("hub-buero"), QStringLiteral("Büro"), old.address(), old.fingerprint())));
    ps->setLastHubId(QStringLiteral("hub-zuhause"));
    h.controller->setAutoConnect(true);
    QVERIFY(ps->autoConnect());
    QCOMPARE(h.controller->hubs().size(), 3);
    const QVariantMap idle = h.controller->hubs().at(0).toMap();
    QCOMPARE(idle.value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
    QVERIFY(idle.value(QStringLiteral("isLast")).toBool());

    // Zertifikat geaendert: blockiert, keine Verbinden-Aktion, beide Fingerprints vorhanden.
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
    QVERIFY(h.controller->connection()->state() != HubConnection::State::NeedsPairing);  // nie still uebernommen
    uitest::saveShot(h.window, QStringLiteral("3a-connection-cert-changed"));

    // Player zu alt (Hub verlangt ein neueres Protokoll)
    h.controller->connectProfile(QStringLiteral("hub-buero"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Incompatible);
    for (const QVariant& v : h.controller->hubs()) {
      if (v.toMap().value(QStringLiteral("hubId")).toString() == QLatin1String("hub-buero")) card = v.toMap();
    }
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("incompatible"));
    QCOMPARE(card.value(QStringLiteral("statusText")).toString(), QStringLiteral("Player zu alt"));
    uitest::saveShot(h.window, QStringLiteral("3a-connection-incompatible"));

    // Entfernen funktioniert bei jeder Karte (auch bei der blockierten).
    h.controller->removeHub(QStringLiteral("hub-lena"));
    QCOMPARE(h.controller->hubs().size(), 2);
    QVERIFY(!ps->profile(QStringLiteral("hub-lena")).has_value());
  }

  // 3a: nicht erreichbar -> Karte fuer die versuchte Adresse mit Wiederholen.
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
    h.controller->removeHub(QString());  // Versuch verwerfen
    QCOMPARE(h.controller->hubs().size(), 0);
  }

  // Regression: jeder Versuch (addHub, Wiederholen) zeigt die Pairing-/Statusphase, nicht nur der erste.
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

  // Retry nach connectProfile (gespeichertes Profil) zeigt nie den Pairing-Statusbildschirm.
  void retryForSavedProfileStaysOnConnection() {
    Harness h;
    QVERIFY(h.start());
    QVERIFY(h.controller->profileStore()->upsertProfile(
        profile(QStringLiteral("hub-weg"), QStringLiteral("Weg"), QStringLiteral("http://127.0.0.1:1"))));
    h.controller->connectProfile(QStringLiteral("hub-weg"));
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    h.controller->retryConnection();
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Unreachable);
    QCOMPARE(screenOf(h), QStringLiteral("connection"));
  }

  // 3b + 3c: TOFU-Bestaetigung, Freigabe (warten, abgelehnt, genehmigt), Library.
  void pairingAndLibrary() {
    const QByteArray readyRom = "homebrew-dummy-rom-ready";
    const QByteArray badRom = "this-is-not-the-expected-content";
    const QString readySha = uitest::sha256Hex(readyRom);
    const QString badSha = uitest::sha256Hex(QByteArray("expected-but-never-served"));

    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-zuhause");
    hub.name = QStringLiteral("Zuhause");
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
    // Lumen Drift liegt bereits (validiert) im Cache.
    {
      QFile f(QDir(h.controller->profileStore()->romCacheDir()).filePath(readySha + QStringLiteral(".nds")));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(readyRom);
    }

    h.controller->addHub(hub.address());
    QTRY_COMPARE(screenOf(h), QStringLiteral("pairing"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation);

    // Schritt 2: Fingerprint wird angezeigt und muss explizit bestaetigt werden.
    QVariantMap p = pairingOf(h);
    QCOMPARE(p.value(QStringLiteral("phase")).toString(), QStringLiteral("trust"));
    QCOMPARE(p.value(QStringLiteral("hubName")).toString(), QStringLiteral("Zuhause"));
    QCOMPARE(p.value(QStringLiteral("fingerprint")).toString().remove(QLatin1Char('\n')).remove(QLatin1Char(':')),
             PlayerController::formatFingerprint(hub.fingerprint()).remove(QLatin1Char('\n')).remove(QLatin1Char(':')));
    QCOMPARE(p.value(QStringLiteral("fingerprint")).toString().count(QLatin1Char('\n')), 1);
    QVERIFY(h.item("trustButton") != nullptr);
    QVERIFY(h.item("fingerprintText") != nullptr);
    QTest::qWait(100);
    QCOMPARE(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation);  // kein stilles Vertrauen
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-trust"));

    QVERIFY(h.click("trustButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsPairing);
    QCOMPARE(pairingOf(h).value(QStringLiteral("phase")).toString(), QStringLiteral("needsPairing"));
    QVERIFY(h.controller->profileStore()->profile(QStringLiteral("hub-zuhause")).has_value());
    QCOMPARE(h.controller->profileStore()->profile(QStringLiteral("hub-zuhause"))->pinnedFingerprint, hub.fingerprint());

    // Schritt 3: Freigabe anfragen -> Warten.
    QVERIFY(h.click("requestButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::AwaitingApproval);
    p = pairingOf(h);
    QCOMPARE(p.value(QStringLiteral("phase")).toString(), QStringLiteral("awaiting"));
    QVERIFY(!p.value(QStringLiteral("deviceName")).toString().isEmpty());
    QVERIFY(!p.value(QStringLiteral("platform")).toString().isEmpty());
    QVERIFY(!p.value(QStringLiteral("playerVersion")).toString().isEmpty());
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-waiting"));

    // Anfrage abbrechen -> zurueck zu "Freigabe anfragen"
    QVERIFY(h.click("cancelRequestButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsPairing);

    // Abgelehnt
    hub.decision = FakeHub::Decision::Deny;
    QVERIFY(h.click("requestButton"));
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Denied);
    QCOMPARE(pairingOf(h).value(QStringLiteral("phase")).toString(), QStringLiteral("denied"));
    QVERIFY(h.item("pairingProblem") != nullptr && h.item("pairingProblem")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-denied"));

    // Abgelaufen
    hub.decision = FakeHub::Decision::Expire;
    QVERIFY(h.click("requestButton"));  // "Neu anfragen"
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::Expired);
    QCOMPARE(pairingOf(h).value(QStringLiteral("phase")).toString(), QStringLiteral("expired"));
    uitest::saveShot(h.window, QStringLiteral("3b-pairing-expired"));

    // Genehmigt -> Library
    hub.decision = FakeHub::Decision::Approve;
    QVERIFY(h.click("requestButton"));
    QTRY_COMPARE_WITH_TIMEOUT(screenOf(h), QStringLiteral("library"), 8000);
    QTRY_COMPARE(h.controller->libraryState(), QStringLiteral("ready"));
    LibraryModel* lib = h.controller->library();
    QCOMPARE(lib->totalCount(), 6);
    QCOMPARE(h.controller->hubName(), QStringLiteral("Zuhause"));
    QTRY_COMPARE(lib->readyCount(), 1);  // Lumen Drift nach asynchroner Pruefung
    QCOMPARE(h.controller->selectedGameId(), QStringLiteral("g1"));  // erstes Spiel vorausgewaehlt

    // Hash mismatch sichtbar machen (Hub liefert falschen Inhalt).
    h.controller->downloader()->ensureRom(h.controller->hubLibrary()->games().at(4));
    QTRY_COMPARE(lib->data(lib->index(lib->rowOfGame(QStringLiteral("g5"))), LibraryModel::StateKindRole).toString(),
                 QStringLiteral("mismatch"));

    // Detailspalte: Core fehlt (Pfadhinweis), Firmware nicht benoetigt, Spielen deaktiviert.
    QVariantMap g = h.controller->selectedGame();
    QCOMPARE(g.value(QStringLiteral("title")).toString(), QStringLiteral("Lumen Drift"));
    QCOMPARE(g.value(QStringLiteral("systemName")).toString(), QStringLiteral("Nintendo DS"));
    QCOMPARE(g.value(QStringLiteral("romText")).toString(), QStringLiteral("Lokal gecacht · geprüft"));
    QVERIFY(g.value(QStringLiteral("coreText")).toString().contains(QStringLiteral("fehlt")));
    QVERIFY(g.value(QStringLiteral("coreHint")).toString().contains(QStringLiteral("FRAMEBEAM_MELONDS_DS_CORE")));
    QCOMPARE(g.value(QStringLiteral("firmwareText")).toString(), QStringLiteral("nicht benötigt"));
    QVERIFY(!g.value(QStringLiteral("canPlay")).toBool());
    QCOMPARE(g.value(QStringLiteral("checklist")).toList().size(), 4);
    QCOMPARE(g.value(QStringLiteral("sha")).toString(), readySha);
    QVERIFY(g.value(QStringLiteral("cachePath")).toString().endsWith(readySha + QStringLiteral(".nds")));

    // Auswahl, Suche, Chips
    h.controller->selectGame(QStringLiteral("g2"));
    g = h.controller->selectedGame();
    QCOMPARE(g.value(QStringLiteral("title")).toString(), QStringLiteral("Paper Wizards"));
    QCOMPARE(g.value(QStringLiteral("romText")).toString(), QStringLiteral("Download nötig · 128 MB"));
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

    // Hub wechseln = trennen und zurueck zu 3a.
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
