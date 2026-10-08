// Speed-up button and speed select in the game header (preview game, no core): hidden/visible, Space toggles.
#include <QPainter>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

class FastForwardUiTest : public QObject {
  Q_OBJECT
 private slots:
  void buttonAndSpace() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-ff");
    hub.name = QStringLiteral("Home");
    hub.decision = FakeHub::Decision::Approve;
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsPairing, 8000);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);

    GameSession* gs = h.controller->gameSession();
    QImage frame(256, 384, QImage::Format_RGB32);
    frame.fill(QColor(40, 90, 140));
    emu::DisplayProfile profile;
    gs->setPreview(QStringLiteral("Framebeam Test"), frame, profile, EmulationDiagnostics());
    emit gs->started();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 8000);
    h.controller->sessions()->gameStarted(QStringLiteral("t1"), QStringLiteral("Framebeam Test"));
    h.controller->sessions()->setTab(QStringLiteral("session"));
    QQuickTest::qWaitForPolish(h.window);

    // Unsupported core: no button.
    QVERIFY(!gs->fastForwardAvailable());
    QQuickItem* btn = h.item("fastForwardButton");
    QVERIFY(btn == nullptr || !btn->isVisible());
    gs->setFastForward(true);  // refused
    QVERIFY(!gs->fastForward());

    // Supported: button visible, enabled, off.
    gs->setPreviewFastForwardSupported(true);
    QQuickTest::qWaitForPolish(h.window);
    btn = h.item("fastForwardButton");
    QVERIFY(btn != nullptr && btn->isVisible() && btn->isEnabled());
    QVERIFY(h.click("fastForwardButton"));
    QVERIFY(gs->fastForward());
    QTest::keyClick(h.window, Qt::Key_Space);  // Space toggles (off)
    QTRY_VERIFY(!gs->fastForward());
    QTest::keyClick(h.window, Qt::Key_Space);
    QTRY_VERIFY(gs->fastForward());

    // Speed select: changes the ratio of the running game; the indicator follows.
    QQuickItem* sel = h.item("speedSelect");
    QVERIFY(sel != nullptr && sel->isVisible());
    QCOMPARE(gs->fastForwardRatio(), 2.0);
    QCOMPARE(sel->property("current").toString(), QStringLiteral("2"));
    QMetaObject::invokeMethod(sel, "picked", Q_ARG(QString, QStringLiteral("6")));
    QCOMPARE(gs->fastForwardRatio(), 6.0);
    QCOMPARE(sel->property("current").toString(), QStringLiteral("6"));
    QVERIFY(h.item("fastForwardIndicator")->isVisible());

    // Still usable while the own Session is shared (no block): Space and the button keep working.
    QVERIFY(gs->fastForward());
    QTest::keyClick(h.window, Qt::Key_Space);
    QTRY_VERIFY(!gs->fastForward());
    QVERIFY(h.click("fastForwardButton"));
    QVERIFY(gs->fastForward());

    // Core without support (new game): everything off and hidden again.
    gs->setPreviewFastForwardSupported(false);
    QVERIFY(!gs->fastForward());
    QQuickTest::qWaitForPolish(h.window);
    QQuickItem* b2 = h.item("fastForwardButton");
    QVERIFY(b2 == nullptr || !b2->isVisible());
  }
};

QTEST_MAIN(FastForwardUiTest)
#include "fast_forward_ui_test.moc"
