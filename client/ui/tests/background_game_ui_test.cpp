// Game in the background (0.7.x): "← Library" pauses and keeps the game loaded; Library strip, Resume, Quit, the
// confirmation before another game starts, and no input to the core while in the background (preview game, no core).
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

class BackgroundGameUiTest : public QObject {
  Q_OBJECT

  static void pair(FakeHub& hub, Harness& h) {
    hub.hubId = QStringLiteral("hub-bg");
    hub.name = QStringLiteral("Home");
    hub.decision = FakeHub::Decision::Approve;
    hub.games = QJsonObject{{QStringLiteral("games"),
                             QJsonArray{uitest::gameJson(QStringLiteral("g1"), QStringLiteral("Lumen Drift"), uitest::sha256Hex("a"), 1),
                                        uitest::gameJson(QStringLiteral("g2"), QStringLiteral("Night Run"), uitest::sha256Hex("b"), 1)}}};
    QVERIFY(hub.start());
    QVERIFY(h.start());
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsPairing, 8000);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 8000);
  }

  static void startPreview(Harness& h, const QString& gameId) {
    h.controller->selectGame(gameId);
    h.controller->adoptPreviewGame(gameId);
    GameSession* gs = h.controller->gameSession();
    QImage frame(256, 384, QImage::Format_RGB32);
    frame.fill(QColor(40, 90, 140));
    gs->setPreview(QStringLiteral("Preview"), frame, emu::DisplayProfile(), EmulationDiagnostics());
    emit gs->started();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 8000);
  }

 private slots:
  void initTestCase() { uitest::installWarningCounter(); }

  void backgroundResumeQuit() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h);
    PlayerController* c = h.controller.get();
    GameSession* gs = c->gameSession();
    startPreview(h, QStringLiteral("g1"));
    gs->setKeyboardMap({{Qt::Key_A, 1u << 8}});
    QVERIFY(gs->keyEvent(Qt::Key_A, true));
    gs->keyEvent(Qt::Key_A, false);
    QVERIFY(c->backgroundGame().isEmpty());

    // "← Library": paused, still loaded, Library page shown.
    c->leaveGameView();
    QCOMPARE(c->screen(), QStringLiteral("library"));
    QVERIFY(gs->isActive());
    QTRY_VERIFY(gs->isPaused());
    QCOMPARE(c->backgroundGame().value(QStringLiteral("id")).toString(), QStringLiteral("g1"));
    QCOMPARE(c->backgroundGame().value(QStringLiteral("title")).toString(), QStringLiteral("Lumen Drift"));
    QQuickTest::qWaitForPolish(h.window);
    QQuickItem* strip = h.item("runningStrip");
    QVERIFY(strip != nullptr && strip->isVisible());
    QVERIFY(strip->parentItem() != nullptr && strip->width() <= strip->parentItem()->width());
    QVERIFY(h.item("stripResumeButton") != nullptr && h.item("stripQuitButton") != nullptr);
    const QVariantMap sel = c->selectedGame();
    QCOMPARE(sel.value(QStringLiteral("running")).toBool(), true);
    QCOMPARE(sel.value(QStringLiteral("playLabel")).toString(), QStringLiteral("Resume"));
    QQuickItem* second = h.item("playShareButton");
    QVERIFY(second != nullptr && second->isVisible());

    // Input does not reach the core; hotkeys and Esc do not act on the background game.
    QVERIFY(!gs->keyEvent(Qt::Key_A, true));
    gs->setGamepadMask(1u << 3);
    QCOMPARE(gs->joypadMask(), 0u);
    QTest::keyClick(h.window, Qt::Key_Escape);
    QTest::keyClick(h.window, Qt::Key_Space);
    QTest::qWait(50);
    QVERIFY(gs->isPaused());

    // Other pages stay usable, the game stays in the background.
    c->showSettings();
    QCOMPARE(c->screen(), QStringLiteral("settings"));
    c->showEmulation();
    QCOMPARE(c->screen(), QStringLiteral("emulation"));
    c->showControllers();
    QCOMPARE(c->screen(), QStringLiteral("controllers"));
    c->showLibrary();
    QCOMPARE(c->screen(), QStringLiteral("library"));
    QVERIFY(gs->isActive());

    // Resume: back in the game view, running again, input works again.
    c->resumeGame();
    QCOMPARE(c->screen(), QStringLiteral("game"));
    QTRY_VERIFY(!gs->isPaused());
    QVERIFY(c->backgroundGame().isEmpty());
    QCOMPARE(gs->joypadMask(), 1u << 3);
    QVERIFY(gs->keyEvent(Qt::Key_A, true));
    gs->keyEvent(Qt::Key_A, false);

    // Back to the background and quit explicitly.
    c->leaveGameView();
    QCOMPARE(c->screen(), QStringLiteral("library"));
    QVERIFY(!c->backgroundGame().isEmpty());
    c->quitGame();
    QCOMPARE(c->screen(), QStringLiteral("library"));
    QVERIFY(!gs->isActive());
    QVERIFY(c->backgroundGame().isEmpty());
    QVERIFY(c->selectedGame().value(QStringLiteral("running")).toBool() == false);
    QQuickTest::qWaitForPolish(h.window);
    QQuickItem* gone = h.item("runningStrip");
    QVERIFY(gone == nullptr || !gone->isVisible());
    QCOMPARE(uitest::warningCount().load(), 0);
  }

  void startOtherGameAsksFirst() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h);
    PlayerController* c = h.controller.get();
    GameSession* gs = c->gameSession();
    startPreview(h, QStringLiteral("g1"));
    c->leaveGameView();
    QVERIFY(gs->isActive());

    // The running game itself: Play resumes it, no question.
    c->playSelected();
    QCOMPARE(c->screen(), QStringLiteral("game"));
    c->leaveGameView();

    // Another game: confirmation, Cancel keeps everything as it is.
    c->selectGame(QStringLiteral("g2"));
    QCOMPARE(c->selectedGame().value(QStringLiteral("running")).toBool(), false);
    c->playSelected();
    QVERIFY(c->startConfirm().value(QStringLiteral("active")).toBool());
    QCOMPARE(c->startConfirm().value(QStringLiteral("runningTitle")).toString(), QStringLiteral("Lumen Drift"));
    QCOMPARE(c->startConfirm().value(QStringLiteral("newTitle")).toString(), QStringLiteral("Night Run"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("startConfirmDialog") != nullptr && h.item("startConfirmDialog")->isVisible());
    QVERIFY(h.click("startConfirmCancel"));
    QTRY_VERIFY(!c->startConfirm().value(QStringLiteral("active")).toBool());
    QVERIFY(gs->isActive());
    QVERIFY(!c->backgroundGame().isEmpty());

    // Confirm: the running game quits first.
    c->playSelected();
    QVERIFY(c->startConfirm().value(QStringLiteral("active")).toBool());
    c->confirmQuitAndStart();
    QVERIFY(c->backgroundGame().isEmpty());
    QVERIFY(!gs->isActive() || gs->state() == GameSession::Starting);
    QVERIFY(!c->startConfirm().value(QStringLiteral("active")).toBool());
    QCOMPARE(uitest::warningCount().load(), 0);
  }
};

QTEST_MAIN(BackgroundGameUiTest)
#include "background_game_ui_test.moc"
