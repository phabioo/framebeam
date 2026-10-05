// Spielansicht mit echtem melonDS-DS-Core und der Homebrew-Test-ROM (NEEDS_CORE: ohne Core Rueckgabecode 77).
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include "fakehub.h"
#include "gameview.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

class GameTest : public QObject {
  Q_OBJECT
 private slots:
  void playTestRom() {
    if (qEnvironmentVariableIsEmpty("FRAMEBEAM_MELONDS_DS_CORE")) {
      QSKIP("FRAMEBEAM_MELONDS_DS_CORE nicht gesetzt");
    }
    uitest::installWarningCounter();
    QFile rom(QStringLiteral(FB_TEST_ROM_PATH));
    QVERIFY2(rom.open(QIODevice::ReadOnly), "Test-ROM fehlt (Ziel framebeam_test_rom)");
    const QByteArray romData = rom.readAll();
    const QString sha = uitest::sha256Hex(romData);

    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-game");
    hub.name = QStringLiteral("Zuhause");
    hub.decision = FakeHub::Decision::Approve;
    QJsonArray games;
    games.append(uitest::gameJson(QStringLiteral("t1"), QStringLiteral("Framebeam Test"), sha, romData.size(), QStringLiteral("framebeam_test.nds")));
    games.append(uitest::gameJson(QStringLiteral("t2"), QStringLiteral("Paper Wizards"), uitest::sha256Hex("pw"), 128LL * 1024 * 1024));
    hub.games = QJsonObject{{QStringLiteral("games"), games}};
    hub.roms.insert(sha, romData);
    QVERIFY(hub.start());

    Harness h;
    QVERIFY(h.start(/*probeCores=*/true));
    // Core-Version fuer den Handshake kam aus der Core-Info (oder: nur core_id, leere Version).
    QCOMPARE(h.controller->handshakeCores().size(), 1);
    QCOMPARE(h.controller->handshakeCores().at(0).id, QStringLiteral("melonds_ds"));
    QVERIFY2(!h.controller->handshakeCores().at(0).version.isEmpty(), "Core-Version nicht ermittelt");

    h.controller->addHub(hub.address());
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation);
    h.controller->confirmTrust();
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsPairing);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
    QTRY_COMPARE(h.controller->libraryState(), QStringLiteral("ready"));
    QCOMPARE(h.controller->selectedGameId(), QStringLiteral("t1"));

    QVariantMap g = h.controller->selectedGame();
    QVERIFY(g.value(QStringLiteral("coreText")).toString().contains(QStringLiteral("bereit")));
    QCOMPARE(g.value(QStringLiteral("firmwareText")).toString(), QStringLiteral("nicht benötigt"));
    QVERIFY(g.value(QStringLiteral("canPlay")).toBool());
    QCOMPARE(g.value(QStringLiteral("playLabel")).toString(), QStringLiteral("Herunterladen und spielen"));
    uitest::saveShot(h.window, QStringLiteral("3c-library-core-ready"));

    // Spielen: laedt die ROM vom Hub (SHA-256 geprueft), startet melonDS DS, wechselt in die Spielansicht.
    h.controller->playSelected();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 20000);
    GameSession* s = h.controller->gameSession();
    QCOMPARE(s->title(), QStringLiteral("Framebeam Test"));
    QTRY_VERIFY_WITH_TIMEOUT(s->frameNumber() >= 5, 10000);
    QVERIFY(s->hasFrame());
    QVERIFY(!uitest::isAllBlack(s->frame()));
    QCOMPARE(s->frame().size(), QSize(256, 384));

    // Die Spielansicht zeigt ein Bild (nicht komplett schwarz) in der gerenderten Szene.
    auto* view = h.window->findChild<GameView*>(QStringLiteral("gameView"));
    QVERIFY(view != nullptr);
    QTRY_VERIFY(!view->frameRect().isEmpty());
    QTest::qWait(300);
    const QImage shot = h.window->grabWindow();
    const QPointF topLeft = view->mapToScene(view->frameRect().topLeft());
    const QRect region = QRectF(topLeft, view->frameRect().size()).toAlignedRect().intersected(shot.rect());
    QVERIFY(!region.isEmpty());
    QVERIFY2(!uitest::isAllBlack(shot.copy(region)), "Spielansicht ist schwarz");
    // Seitenverhaeltnis 2:3 bleibt erhalten und der Frame fuellt die Hoehe der Ansicht.
    QVERIFY(std::abs(view->frameRect().width() / view->frameRect().height() - 256.0 / 384.0) < 0.01);
    QVERIFY(view->frameRect().height() >= view->height() * 0.9);
    uitest::saveShot(h.window, QStringLiteral("game-view"));

    // Saves je Hub getrennt: <data>/hubs/<hub_id>/saves
    QVERIFY(QDir(QDir(h.controller->profileStore()->hubDir(QStringLiteral("hub-game"))).filePath(QStringLiteral("saves"))).exists());

    // Pause/Fortsetzen, Reset
    QVERIFY(h.click("pauseButton"));
    QTRY_COMPARE(s->state(), GameSession::Paused);
    const quint64 pausedAt = s->frameNumber();
    QTest::qWait(250);
    QVERIFY(s->frameNumber() <= pausedAt + 2);
    uitest::saveShot(h.window, QStringLiteral("game-view-paused"));
    QVERIFY(h.click("pauseButton"));
    QTRY_COMPARE(s->state(), GameSession::Running);
    QTRY_VERIFY(s->frameNumber() > pausedAt + 5);
    QVERIFY(h.click("resetButton"));

    // Eingabe: Tastatur (A/B/Pfeile) und Maus-Touch gelangen ohne Absturz in den Core.
    view->forceActiveFocus();
    QTest::keyClick(h.window, Qt::Key_X);
    QTest::keyPress(h.window, Qt::Key_Up);
    QTest::keyRelease(h.window, Qt::Key_Up);
    const QPointF touch = view->mapToScene(QPointF(view->frameRect().center().x(), view->frameRect().top() + view->frameRect().height() * 0.75));
    QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, touch.toPoint());
    // Esc = Pause, nochmal Esc = Fortsetzen
    QTest::keyClick(h.window, Qt::Key_Escape);
    QTRY_COMPARE(s->state(), GameSession::Paused);
    QTest::keyClick(h.window, Qt::Key_Escape);
    QTRY_COMPARE(s->state(), GameSession::Running);

    // Beenden: zurueck zur Library.
    QVERIFY(h.click("quitButton"));
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
    QCOMPARE(s->state(), GameSession::Idle);
    QCOMPARE(uitest::warningCount().load(), 0);
  }
};

UITEST_MAIN(GameTest)
#include "game_test.moc"
