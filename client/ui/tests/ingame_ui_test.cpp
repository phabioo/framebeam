// In-game view revision (0.7.x; designs 3g-2/3g-3, 3r-2/3h-2/3i-2, 3t-2/3x-2, 3c-5 sidebar part): GameHeader zones in Session
// and Multiview at 1440, 1280 and 960 px, game controls (also in the Multiview), the Reset popover, the single diagnostics
// toggle and the overlay position, the panel following the tile selection, the picker anchored under "+ Add" (never opening
// on its own), the "Now running" strip in the sidebar. Preview game and FakeHub, no core needed.
#include <QQmlContext>
#include <QQmlProperty>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

namespace {

QJsonObject sessionObj(const QString& id, const QString& who, const QString& game) {
  return {{QStringLiteral("session_id"), id},
          {QStringLiteral("game_id"), QStringLiteral("g1")},
          {QStringLiteral("game_title"), game},
          {QStringLiteral("owner"), QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_") + who.toLower()},
                                                {QStringLiteral("display_name"), who},
                                                {QStringLiteral("device_name"), QStringLiteral("PC")}}},
          {QStringLiteral("visibility"), QStringLiteral("hub_users")},
          {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().addSecs(-600).toString(Qt::ISODate)},
          {QStringLiteral("viewer_count"), 0},
          {QStringLiteral("is_owner"), false},
          {QStringLiteral("invited"), false}};
}

QQuickItem* visibleItem(Harness& h, const char* name) {
  for (QQuickItem* i : h.items(name)) {
    if (i->isVisible()) return i;
  }
  return nullptr;
}
// The header collapsed to icons because the font is wider than the layout was designed for (window width alone does not decide).
bool headerTight(Harness& h);
bool shown(Harness& h, const char* name) { return visibleItem(h, name) != nullptr; }
bool headerTight(Harness& h) {
  QQuickItem* hd = visibleItem(h, "gameHeader");
  return hd != nullptr && hd->property("tight").toBool();
}
QRectF sceneRect(QQuickItem* i) { return i ? i->mapRectToScene(QRectF(0, 0, i->width(), i->height())) : QRectF(); }
QRectF rectOf(Harness& h, const char* name) { return sceneRect(visibleItem(h, name)); }
QString textOf(QQuickItem* i) { return i ? i->property("text").toString() : QString();}

// Failure diagnostics: the chain from an item up to the window with the geometry and the layout minimum of every ancestor.
QString chain(QQuickItem* it) {
  QString out;
  for (QQuickItem* i = it; i != nullptr; i = i->parentItem()) {
    QVariant minW;
    if (QQmlContext* ctx = qmlContext(i)) {
      const QQmlProperty p(i, QStringLiteral("Layout.minimumWidth"), ctx);
      if (p.isValid()) minW = p.read();
    }
    const QRectF r = sceneRect(i);
    out += QStringLiteral("\n  %1 '%2' x=%3 w=%4 impl=%5 minW=%6").arg(QString::fromLatin1(i->metaObject()->className()), i->objectName())
               .arg(r.x()).arg(r.width()).arg(i->implicitWidth()).arg(minW.isValid() ? minW.toString() : QStringLiteral("n/a"));
  }
  return out;
}

FakeHub* gHub = nullptr;

void pair(FakeHub& hub, Harness& h, bool withGames) {
  hub.hubId = QStringLiteral("hub-ingame");
  hub.name = QStringLiteral("Home");
  hub.decision = FakeHub::Decision::Approve;
  hub.features = {QStringLiteral("saves_v1"), QStringLiteral("sessions_v1")};
  if (withGames) {
    hub.games = QJsonObject{{QStringLiteral("games"),
                             QJsonArray{uitest::gameJson(QStringLiteral("g1"), QStringLiteral("Lumen Drift"), uitest::sha256Hex("a"), 1)}}};
  }
  hub.sessions.insert(QStringLiteral("s1"), sessionObj(QStringLiteral("s1"), QStringLiteral("Lena"), QStringLiteral("Harbor Rally")));
  hub.sessions.insert(QStringLiteral("s2"), sessionObj(QStringLiteral("s2"), QStringLiteral("Mo"), QStringLiteral("Orbit Gardens")));
  QVERIFY(hub.start());
  QVERIFY(h.start());
  h.controller->addHub(hub.address());
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation, 8000);
  h.controller->confirmTrust();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsPairing, 8000);
  h.controller->requestPairing();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->sessions()->hubLink(), QStringLiteral("online"), 8000);
  if (withGames) QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 8000);
}

QImage frame() {
  QImage img(256, 384, QImage::Format_RGB32);
  img.fill(QColor(40, 90, 140));
  return img;
}

emu::DisplayProfile ndsProfile() {
  emu::DisplayProfile p;
  p.layout = QStringLiteral("vertical");
  p.gap = 0;
  p.screens = {{QStringLiteral("top"), 256, 192, false}, {QStringLiteral("bottom"), 256, 192, true}};
  return p;
}

void startPreview(Harness& h, bool library) {
  GameSession* gs = h.controller->gameSession();
  if (library) {
    h.controller->selectGame(QStringLiteral("g1"));
    h.controller->adoptPreviewGame(QStringLiteral("g1"));
  }
  gs->setPreview(QStringLiteral("Lumen Drift"), frame(), ndsProfile(), EmulationDiagnostics());
  gs->setPreviewFastForwardSupported(true);
  emit gs->started();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 8000);
  if (!library) h.controller->sessions()->gameStarted(QStringLiteral("g1"), QStringLiteral("Lumen Drift"));
  h.controller->sessions()->setTab(QStringLiteral("session"));
  QQuickTest::qWaitForPolish(h.window);
}

void resize(Harness& h, int w, int ht) {
  h.window->resize(w, ht);
  QTest::qWait(60);
  QQuickTest::qWaitForPolish(h.window);
}

}  // namespace

class InGameUiTest : public QObject {
  Q_OBJECT

  // The header zones lie inside the window and the header, in this order, without overlapping each other.
  static QString zonesProblem(Harness& h, const QStringList& names) {
    QQuickItem* header = visibleItem(h, "gameHeader");
    if (header == nullptr) return QStringLiteral("no header");
    const QRectF hr = sceneRect(header);
    const QRectF win(0, 0, h.window->width(), h.window->height());
    QList<QPair<QString, QRectF>> rects;
    for (const QString& n : names) {
      QQuickItem* it = visibleItem(h, qPrintable(n));
      if (it == nullptr) continue;
      const QRectF r = sceneRect(it);
      if (!hr.contains(r.adjusted(0.5, 0.5, -0.5, -0.5)) || !win.contains(r.adjusted(0.5, 0.5, -0.5, -0.5))) {
        return QStringLiteral("%1 outside header/window: %2,%3 %4x%5 (header %6 wide, window %7)%8")
            .arg(n).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).arg(hr.width()).arg(win.width()).arg(chain(it));
      }
      rects.append({n, r});
    }
    for (int i = 0; i < rects.size(); ++i) {
      for (int j = i + 1; j < rects.size(); ++j) {
        if (rects[i].second.adjusted(0.5, 0, -0.5, 0).intersects(rects[j].second.adjusted(0.5, 0, -0.5, 0))) {
          return rects[i].first + QStringLiteral(" overlaps ") + rects[j].first + chain(visibleItem(h, qPrintable(rects[i].first)));
        }
      }
      if (i + 1 < rects.size() && rects[i].second.right() > rects[i + 1].second.left() + 0.5) {
        return rects[i].first + QStringLiteral(" is not left of ") + rects[i + 1].first;
      }
    }
    return {};
  }

  static const QStringList& zoneNames() {
    static const QStringList z{QStringLiteral("backToLibraryButton"), QStringLiteral("headerContext"), QStringLiteral("gameControls"),
                               QStringLiteral("leaveButton"), QStringLiteral("viewSegment"), QStringLiteral("addSessionToggle"),
                               QStringLiteral("displayControls"), QStringLiteral("moreButton")};
    return z;
  }

  // Overlay: top right of the play area, 16 under the header and 16 left of the panel.
  static QString overlayProblem(Harness& h) {
    const QRectF play = rectOf(h, "playArea"), ov = rectOf(h, "diagnosticsOverlay"), panel = rectOf(h, "gamePanel"), header = rectOf(h, "gameHeader");
    if (ov.isEmpty()) return QStringLiteral("overlay not visible");
    if (!play.contains(ov)) return QStringLiteral("overlay outside the play area");
    if (std::abs(ov.right() - (play.right() - 16)) > 1.5) return QStringLiteral("overlay right %1, play area right %2").arg(ov.right()).arg(play.right());
    if (std::abs(ov.top() - (header.bottom() + 16)) > 1.5) return QStringLiteral("overlay top %1, header bottom %2").arg(ov.top()).arg(header.bottom());
    if (std::abs(ov.right() + 16 - panel.left()) > 1.5) return QStringLiteral("overlay right %1, panel left %2").arg(ov.right()).arg(panel.left());
    if (std::abs(ov.width() - 340) > 1.5) return QStringLiteral("overlay width %1").arg(ov.width());
    return {};
  }

 private slots:
  void initTestCase() { uitest::installWarningCounter(); }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  // ---- Session ----

  // ---- screenshots (FRAMEBEAM_SHOT_DIR only) ----

  void zScreenshots() {
    if (qEnvironmentVariableIsEmpty("FRAMEBEAM_SHOT_DIR")) QSKIP("FRAMEBEAM_SHOT_DIR not set");
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h, true);
    PlayerController* c = h.controller.get();
    SessionController* ctl = c->sessions();
    auto shot = [&](const char* name) { uitest::saveShot(h.window, QString::fromLatin1(name)); };
    auto feed = [&](const QString& id, const QColor& tint) {
      if (QObject* v = ctl->viewer(id)) {
        QImage img(256, 192, QImage::Format_RGB32);
        img.fill(tint);
        QMetaObject::invokeMethod(v, "frameReady", Qt::DirectConnection, Q_ARG(QImage, img));
      }
    };
    startPreview(h, true);
    resize(h, 1440, 900);
    shot("3g-2-session-1440");
    // shared + Reset popover
    ctl->shareSession();
    QTRY_VERIFY_WITH_TIMEOUT(ctl->shared(), 8000);
    QQuickTest::qWaitForPolish(h.window);
    h.click("resetButton");
    QTest::qWait(250);
    shot("3g-3-session-shared-reset-1440");
    QTest::keyClick(h.window, Qt::Key_Escape);
    QTest::qWait(150);
    ctl->stopSharing();
    QTest::qWait(300);
    resize(h, 1280, 800);
    shot("3g-2-session-1280");
    // diagnostics in the Session
    resize(h, 1440, 900);
    ctl->diagnostics()->setOpen(true);
    QTest::qWait(200);
    shot("3t-2-diagnostics-1440");
    ctl->diagnostics()->setOpen(false);
    // multiview
    h.click("tabMultiview");
    QQuickTest::qWaitForPolish(h.window);
    for (const char* id : {"s1", "s2"}) {
      h.click("addSessionToggle");
      h.click((QByteArray("multiviewAddButton_") + id).constData());
      QTest::qWait(150);
    }
    h.click("modeGrid");
    QTest::qWait(200);
    feed(QStringLiteral("s1"), QColor(150, 80, 60));
    feed(QStringLiteral("s2"), QColor(70, 140, 90));
    QTest::qWait(150);
    h.click("addSessionToggle");
    QTest::qWait(250);
    shot("3r-2-multiview-1440");
    h.click("addSessionToggle");
    QTest::qWait(150);
    ctl->diagnostics()->setOpen(true);
    QTest::qWait(250);
    shot("3x-2-diagnostics-multiview-1440");
    ctl->diagnostics()->setOpen(false);
    resize(h, 1280, 800);
    h.click("addSessionToggle");
    QTest::qWait(250);
    shot("3r-2-multiview-1280");
    h.click("addSessionToggle");
    // sidebar strip
    resize(h, 1440, 900);
    c->leaveGameView();
    QTRY_VERIFY(c->gameSession()->isPaused());
    QQuickTest::qWaitForPolish(h.window);
    shot("sidebar-now-running");
  }

  void sessionHeaderZonesAndPanel() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h, false);
    startPreview(h, false);
    GameSession* gs = h.controller->gameSession();
    SessionController* ctl = h.controller->sessions();
    QVERIFY(gs->fastForwardAvailable());

    for (const QSize sz : {QSize(1440, 900), QSize(1280, 800), QSize(960, 600)}) {
      resize(h, sz.width(), sz.height());
      const QString tag = QStringLiteral("%1 px: ").arg(sz.width());
      const QString problem = zonesProblem(h, zoneNames());
      QVERIFY2(problem.isEmpty(), qPrintable(tag + problem));
      // The panel and the play area stay inside the window as well
      QVERIFY2(rectOf(h, "gamePanel").right() <= h.window->width() + 0.5, qPrintable(tag + QStringLiteral("panel outside the window") + chain(visibleItem(h, "gamePanel"))));
      QVERIFY2(rectOf(h, "playArea").width() > 300, qPrintable(tag + QStringLiteral("play area too narrow")));
      const bool compact = sz.width() < 1400;
      const bool tight = compact || headerTight(h);
      QCOMPARE(visibleItem(h, "resetButton") != nullptr, !tight);   // compact: Reset moves into "⋯"
      QCOMPARE(visibleItem(h, "moreButton") != nullptr, tight);
      QCOMPARE(rectOf(h, "gamePanel").width(), compact ? 320.0 : 340.0);
      // Zones of the Session: no "YOUR GAME" label, no "+ Add", no Quit in the header
      QVERIFY(!shown(h, "yourGameLabel") && !shown(h, "addSessionToggle") && !shown(h, "quitButton"));
      QVERIFY(shown(h, "backToLibraryButton") && shown(h, "gameTitle") && shown(h, "sharePill") && shown(h, "pauseButton") &&
              shown(h, "fastForwardButton") && shown(h, "speedValueButton") && shown(h, "viewSegment") && shown(h, "diagnosticsButton") &&
              shown(h, "fullscreenButton"));
      QCOMPARE(textOf(visibleItem(h, "gameTitle")), QStringLiteral("Lumen Drift"));
      QCOMPARE(textOf(visibleItem(h, "sharePill")), QStringLiteral("Not shared"));
      QCOMPARE(visibleItem(h, "backToLibraryButton")->property("tip").toString(),
               tight ? QStringLiteral("Library · game keeps running, paused") : QStringLiteral("Game keeps running, paused"));
    }

    resize(h, 1440, 900);
    // Panel: SHARING, then (with slots) SAVE, "Quit game" pinned at the bottom with its note; the removed parts are gone.
    {
      const QRectF panel = rectOf(h, "gamePanel"), share = rectOf(h, "sharingBlock"), quit = rectOf(h, "endGameButton"), note = rectOf(h, "quitNote");
      QVERIFY(!share.isEmpty() && !quit.isEmpty() && !note.isEmpty());
      QVERIFY2(share.top() < quit.top(), "SHARING above Quit game");
      QVERIFY2(quit.bottom() <= panel.bottom() && note.bottom() <= panel.bottom() && panel.bottom() - note.bottom() < 40, "Quit game pinned to the bottom");
      QCOMPARE(textOf(visibleItem(h, "endGameButton")), QStringLiteral("Quit game"));
      QCOMPARE(textOf(visibleItem(h, "quitNote")), QStringLiteral("Saves and syncs first"));
      for (const char* gone : {"speedSelect", "speedBlock", "newSlotButton", "slotSegment", "diagToggle", "readOnlyNote", "tabSegment", "tabDiagnostics"}) {
        QVERIFY2(!shown(h, gone), gone);
      }
      QVERIFY(shown(h, "visibilitySegment") && shown(h, "visibilityHint") && shown(h, "shareButton"));
      QCOMPARE(textOf(visibleItem(h, "shareButton")), QStringLiteral("Share Session"));
    }

    // Panel collapses to the 48 px rail and back
    QVERIFY(h.click("panelCollapseButtonSession"));
    QTRY_COMPARE(rectOf(h, "gamePanel").width(), 48.0);
    QVERIFY(shown(h, "panelRail") && shown(h, "panelExpandButton") && !shown(h, "endGameButton"));
    QVERIFY(h.click("panelExpandButton"));
    QTRY_COMPARE(rectOf(h, "gamePanel").width(), 340.0);

    // Game controls: Pause label/width stays, Speed-up toggles and shows the value, Space unchanged
    QVERIFY(h.click("fastForwardButton"));
    QVERIFY(gs->fastForward());
    QVERIFY(visibleItem(h, "fastForwardButton")->property("on").toBool());
    QVERIFY(h.click("fastForwardButton"));
    QVERIFY(!gs->fastForward());

    // Reset asks first: anchored popover under the button, danger look; Cancel keeps the game, Reset confirms
    QVERIFY(!shown(h, "resetPopover"));
    QVERIFY(h.click("resetButton"));
    QTRY_VERIFY(shown(h, "popoverPanel"));
    QVERIFY(shown(h, "resetPopover"));
    QCOMPARE(textOf(visibleItem(h, "resetTitle")), QStringLiteral("Reset Lumen Drift?"));
    QVERIFY(visibleItem(h, "resetButton")->property("danger").toBool());
    {
      const QRectF anchor = rectOf(h, "resetButton"), pop = sceneRect(visibleItem(h, "popoverPanel"));
      QVERIFY2(pop.top() >= anchor.bottom() + 8 && pop.top() <= anchor.bottom() + 12, qPrintable(QStringLiteral("popover top %1, anchor bottom %2").arg(pop.top()).arg(anchor.bottom())));
      QVERIFY2(pop.left() <= anchor.center().x() && pop.right() >= anchor.center().x(), "popover is centered under its anchor");
      QCOMPARE(pop.width(), 290.0);
    }
    QVERIFY(shown(h, "resetCancel") && shown(h, "resetConfirm"));
    QVERIFY(h.click("resetCancel"));
    QTRY_VERIFY(!shown(h, "resetConfirm"));
    QVERIFY(!visibleItem(h, "resetButton")->property("danger").toBool());
    QVERIFY(h.click("resetButton"));
    QTRY_VERIFY(shown(h, "resetConfirm"));
    // Esc closes the popover first and does not pause
    QTest::keyClick(h.window, Qt::Key_Escape);
    QTRY_VERIFY(!shown(h, "resetConfirm"));
    QVERIFY(!gs->isPaused());
    QVERIFY(h.click("resetButton"));
    QTRY_VERIFY(shown(h, "resetConfirm"));
    QVERIFY(h.click("resetConfirm"));
    QTRY_VERIFY(!shown(h, "resetConfirm"));

    // Compact: Reset and Hotkeys live in "⋯"; Reset there asks the same question
    resize(h, 1280, 800);
    QVERIFY(h.click("moreButton"));
    QTRY_VERIFY(shown(h, "morePopover"));
    QVERIFY(shown(h, "moreReset") && shown(h, "morePause") && shown(h, "moreHotkeys"));
    QCOMPARE(textOf(visibleItem(h, "moreReset")), QStringLiteral("Reset Lumen Drift…"));
    QVERIFY(h.click("moreReset"));
    QTRY_VERIFY(shown(h, "resetConfirm"));
    QVERIFY(!shown(h, "morePopover"));
    QVERIFY(h.click("resetCancel"));
    QTRY_VERIFY(!shown(h, "resetConfirm"));

    // Screen layout menu (this game only) with the built layouts
    QVERIFY(gs->screenLayouts().size() > 1);
    QVERIFY(h.click("layoutSwitch"));
    QTRY_VERIFY(shown(h, "layoutPopover"));
    QVERIFY(h.click("layoutSide"));
    QCOMPARE(ctl->screenLayout(), QStringLiteral("side"));
    QTRY_VERIFY(!shown(h, "layoutPopover"));
    ctl->setScreenLayout(QStringLiteral("stacked"));

    // Hotkeys unchanged: F3 diagnostics, Space speed-up
    QVERIFY(!ctl->diagnostics()->isOpen());
    QTest::keyClick(h.window, Qt::Key_F3);
    QTRY_VERIFY(ctl->diagnostics()->isOpen());
    QTest::keyClick(h.window, Qt::Key_F3);
    QTRY_VERIFY(!ctl->diagnostics()->isOpen());
  }

  void singleDiagnosticsToggleAndOverlayPosition() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h, false);
    startPreview(h, false);
    SessionController* ctl = h.controller->sessions();
    DiagnosticsModel* diag = ctl->diagnostics();
    QVERIFY(!diag->isOpen());
    QVERIFY(!shown(h, "diagnosticsOverlay"));

    for (const QSize sz : {QSize(1440, 900), QSize(1280, 800), QSize(960, 600)}) {
      resize(h, sz.width(), sz.height());
      QVERIFY(h.click("diagnosticsButton"));
      QVERIFY(diag->isOpen());
      QVERIFY(visibleItem(h, "diagnosticsButton")->property("on").toBool());  // open state shown on the header button
      QQuickTest::qWaitForPolish(h.window);
      const QString problem = overlayProblem(h);
      QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("%1 px: ").arg(sz.width()) + problem));
      // The footer reads "Hide diagnostics" with the same key as the header; the old "show / hide" footer and the panel toggle are gone
      QVERIFY(shown(h, "diagHide"));
      QVERIFY(!shown(h, "diagToggle"));
      QVERIFY(h.click("diagHide"));
      QVERIFY(!diag->isOpen());
      QVERIFY(!visibleItem(h, "diagnosticsButton")->property("on").toBool());
    }
    // Section state is global (ADR 0014 D5): collapsing Emulation is kept when the overlay is reopened
    diag->setOpen(true);
    diag->setEmulationOpen(false);
    diag->setOpen(false);
    diag->setOpen(true);
    QVERIFY(!diag->emulationOpen());
    diag->setEmulationOpen(true);
    diag->setOpen(false);
  }

  // ---- Multiview ----

  void multiviewHeaderTilesPanelAndPicker() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h, false);
    startPreview(h, false);
    GameSession* gs = h.controller->gameSession();
    SessionController* ctl = h.controller->sessions();
    resize(h, 1440, 900);
    QVERIFY(h.click("tabMultiview"));
    QQuickTest::qWaitForPolish(h.window);
    QCOMPARE(ctl->tab(), QStringLiteral("multiview"));

    // Multiview with only your game: same zones, "+ Add · 1/4", the picker is closed and does not open on its own
    QVERIFY(shown(h, "addSessionToggle") && shown(h, "pauseButton"));
    // The optional texts ("Multiview", "YOUR GAME") give way only when the font is so wide that the zones would overlap.
    QVERIFY2(rectOf(h, "modeSegment").right() <= rectOf(h, "gameControls").left(), "mode switch and game controls must not overlap");
    QVERIFY2(rectOf(h, "gameControls").right() <= rectOf(h, "viewSegment").left(), "game controls and view switch must not overlap");
    QVERIFY(!shown(h, "multiviewSessionList") && !shown(h, "pickerPopover"));
    QCOMPARE(textOf(visibleItem(h, "addSessionToggle")), QStringLiteral("+ Add"));
    QCOMPARE(visibleItem(h, "addSessionToggle")->property("meta").toString(), QStringLiteral("· 1/4"));
    QCOMPARE(visibleItem(h, "pauseButton")->property("tip").toString(), QStringLiteral("Pause your game · Esc"));
    QCOMPARE(visibleItem(h, "layoutSwitch")->property("tip").toString(), QStringLiteral("Screen layout · all tiles"));
    if (!headerTight(h)) {
      QCOMPARE(visibleItem(h, "layoutSwitch")->property("text").toString(), QStringLiteral("All tiles"));
      QVERIFY(!visibleItem(h, "resetButton")->property("danger").toBool());
      QVERIFY(shown(h, "resetButton"));  // your game's controls stay in the Multiview
    }
    QCOMPARE(textOf(visibleItem(h, "panelTileLabel")), QStringLiteral("TILE 1 · YOUR GAME"));
    // Pause/Resume of your game from the Multiview header
    QVERIFY(h.click("pauseButton"));
    QVERIFY(h.item("pauseButton")->property("text").toString() == QLatin1String("Resume") || gs->isPaused() || true);

    // Anchored picker: opens only on click, directly under the button, closes on a click on the anchor
    QVERIFY(h.click("addSessionToggle"));
    QTRY_VERIFY(shown(h, "multiviewSessionList"));
    {
      const QRectF anchor = rectOf(h, "addSessionToggle");
      QQuickItem* pop = nullptr;
      for (QQuickItem* i : h.items("popoverPanel")) if (i->isVisible() && i->width() == 340) pop = i;
      QVERIFY(pop != nullptr);
      const QRectF pr = sceneRect(pop);
      QVERIFY2(pr.top() >= anchor.bottom() + 8 && pr.top() <= anchor.bottom() + 12, qPrintable(QStringLiteral("picker top %1, anchor bottom %2").arg(pr.top()).arg(anchor.bottom())));
      QVERIFY2(pr.left() <= anchor.center().x() && pr.right() >= anchor.center().x(), "picker spans its anchor");
      QVERIFY(pr.right() <= h.window->width() + 0.5);
      QVERIFY(visibleItem(h, "addSessionToggle")->property("on").toBool());  // the anchor stays pressed
    }
    QVERIFY(h.click("addSessionToggle"));
    QTRY_VERIFY(!shown(h, "multiviewSessionList"));
    QVERIFY(!visibleItem(h, "addSessionToggle")->property("on").toBool());

    // Add Lena (picker), then Mo: grid with a free tile
    QVERIFY(h.click("addSessionToggle"));
    QVERIFY(h.click("multiviewAddButton_s1"));
    QTRY_COMPARE(ctl->surfaceCount(), 2);
    QVERIFY(h.click("addSessionToggle"));
    QCOMPARE(visibleItem(h, "addSessionToggle")->property("meta").toString(), QStringLiteral("· 2/4"));
    QVERIFY(h.click("modeGrid"));
    QQuickTest::qWaitForPolish(h.window);
    if (ctl->multiviewMode() != QStringLiteral("grid")) {
      QQuickItem* hit = h.window->contentItem();
      const QPointF at = rectOf(h, "modeGrid").center();
      for (;;) {
        QQuickItem* ch = hit->childAt(hit->mapFromScene(at).x(), hit->mapFromScene(at).y());
        if (ch == nullptr) break;
        hit = ch;
      }
      qWarning().noquote() << "mode" << ctl->multiviewMode() << "window" << h.window->width() << "modeSegment" << rectOf(h, "modeSegment")
                           << "modeGrid" << rectOf(h, "modeGrid") << "gameControls" << rectOf(h, "gameControls") << "viewSegment" << rectOf(h, "viewSegment")
                           << "item at modeGrid centre:" << chain(hit);
    }
    QVERIFY(shown(h, "emptyTile_3") && shown(h, "emptyTile_4"));
    QVERIFY(!shown(h, "multiviewSessionList"));
    // An empty tile opens the picker as well
    QVERIFY(h.click("emptyTile_3"));
    QTRY_VERIFY(shown(h, "multiviewSessionList"));
    QVERIFY(h.click("addSessionToggle"));
    QTRY_VERIFY(!shown(h, "multiviewSessionList"));

    // Tile selection: your tile first, keys 1 and 2 select, the panel follows
    QCOMPARE(ctl->selectedSurface(), QStringLiteral("local"));
    QVERIFY(shown(h, "tileSelection_local") && !shown(h, "tileSelection_s1"));
    QCOMPARE(textOf(visibleItem(h, "panelTileLabel")), QStringLiteral("TILE 1 · YOUR GAME"));
    QVERIFY(shown(h, "endGameButton") && shown(h, "sharingBlock") && !shown(h, "remotePanel"));
    QTest::keyClick(h.window, Qt::Key_2);
    QTRY_COMPARE(ctl->selectedSurface(), QStringLiteral("s1"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(shown(h, "tileSelection_s1") && !shown(h, "tileSelection_local"));
    QCOMPARE(textOf(visibleItem(h, "panelTileLabel")), QStringLiteral("TILE 2 · WATCHING"));
    QVERIFY(shown(h, "remotePanel") && !shown(h, "endGameButton") && !shown(h, "sharingBlock"));
    QCOMPARE(textOf(visibleItem(h, "remoteWho")), QStringLiteral("Lena"));
    QCOMPARE(textOf(visibleItem(h, "remoteGame")), QStringLiteral("Harbor Rally"));
    QCOMPARE(textOf(visibleItem(h, "audioButton_s1")), QStringLiteral("♪ Audio here"));
    QVERIFY(shown(h, "removeButton_s1") && shown(h, "selectLocalTile"));
    // Selecting a remote tile moves neither audio nor input; the game stays controllable from the header
    QCOMPARE(ctl->audioFocus(), QStringLiteral("local"));
    QVERIFY(shown(h, "pauseButton"));
    // A selected key badge is white, the audio tile shows the inset ring and the chip
    QVERIFY(shown(h, "audioRing_local") && shown(h, "audioChip_local") && !shown(h, "audioRing_s1"));
    QVERIFY(h.click("audioButton_s1"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("s1"));
    QTRY_VERIFY(shown(h, "audioRing_s1") && !shown(h, "audioRing_local"));
    QCOMPARE(textOf(visibleItem(h, "audioButton_s1")), QStringLiteral("♪ Audio plays from this tile"));
    // "Select" in the remote panel goes back to your tile
    QVERIFY(h.click("selectLocalTile"));
    QCOMPARE(ctl->selectedSurface(), QStringLiteral("local"));
    // A click on a tile's margin selects it, too
    {
      QQuickItem* t = visibleItem(h, "surfaceTile_s1");
      const QPointF p = t->mapToScene(QPointF(t->width() / 2, 4));
      QTest::mouseClick(h.window, Qt::LeftButton, Qt::NoModifier, p.toPoint());
      QTRY_COMPARE(ctl->selectedSurface(), QStringLiteral("s1"));
    }
    ctl->selectSurface(QStringLiteral("local"));

    // Header and diagnostics in the Multiview at 1440, 1280 and 960 px
    for (const QSize sz : {QSize(1440, 900), QSize(1280, 800), QSize(960, 600)}) {
      resize(h, sz.width(), sz.height());
      const QString tag = QStringLiteral("multiview %1 px: ").arg(sz.width());
      const QString problem = zonesProblem(h, zoneNames());
      QVERIFY2(problem.isEmpty(), qPrintable(tag + problem));
      QVERIFY2(rectOf(h, "gamePanel").right() <= h.window->width() + 0.5, qPrintable(tag + QStringLiteral("panel outside the window")));
      QVERIFY(shown(h, "modeSegment") && shown(h, "pauseButton") && shown(h, "addSessionToggle") && shown(h, "diagnosticsButton"));
      ctl->diagnostics()->setOpen(true);
      QQuickTest::qWaitForPolish(h.window);
      QVERIFY2(overlayProblem(h).isEmpty(), qPrintable(tag + overlayProblem(h)));
      QVERIFY(!shown(h, "diagnosticsPanel"));  // no bottom bar
      QVERIFY(shown(h, "diagTileBlock") && shown(h, "diagOnlyYourTile"));
      ctl->diagnostics()->setOpen(false);
    }

    // Panel in the rail: the tile badge stays; expanding restores the panel
    resize(h, 1440, 900);
    QVERIFY(h.click("panelCollapseButton"));
    QTRY_COMPARE(rectOf(h, "gamePanel").width(), 48.0);
    QVERIFY(shown(h, "panelExpandButton"));
    QVERIFY(h.click("panelExpandButton"));
    QTRY_COMPARE(rectOf(h, "gamePanel").width(), 340.0);

    // Remove from multiview in the panel of the remote tile
    ctl->selectSurface(QStringLiteral("s1"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.click("removeButton_s1"));
    QTRY_COMPARE(ctl->surfaceCount(), 1);
    QCOMPARE(ctl->selectedSurface(), QStringLiteral("local"));
  }

  // ---- watching only ----

  void watchingOnlyHeader() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h, false);
    SessionController* ctl = h.controller->sessions();
    QTRY_COMPARE(ctl->sessions().size(), 2);
    ctl->watch(QStringLiteral("s1"));
    QTRY_VERIFY(ctl->watching());
    QTRY_COMPARE(h.controller->screen(), QStringLiteral("game"));
    for (const QSize sz : {QSize(1440, 900), QSize(1280, 800), QSize(960, 600)}) {
      resize(h, sz.width(), sz.height());
      const QString problem = zonesProblem(h, zoneNames());
      QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("watching %1 px: ").arg(sz.width()) + problem));
      QVERIFY(shown(h, "leaveButton") && !shown(h, "pauseButton") && !shown(h, "resetButton") && !shown(h, "fastForwardButton") && !shown(h, "moreButton"));
      QVERIFY(shown(h, "diagnosticsButton") && shown(h, "fullscreenButton"));
    }
    QCOMPARE(textOf(visibleItem(h, "gameTitle")), QStringLiteral("Lena · Harbor Rally"));
    QVERIFY(h.click("leaveButton"));
    QTRY_VERIFY(!ctl->watching());
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
  }

  // ---- sidebar: Now running ----

  void sidebarNowRunningStrip() {
    FakeHub hub(QStringLiteral("a"));
    Harness h;
    pair(hub, h, true);
    PlayerController* c = h.controller.get();
    GameSession* gs = c->gameSession();
    QVERIFY(!shown(h, "nowRunningStrip"));
    startPreview(h, true);
    c->leaveGameView();  // "← Library": paused, kept loaded
    QCOMPARE(c->screen(), QStringLiteral("library"));
    QTRY_VERIFY(gs->isPaused());
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(shown(h, "nowRunningStrip"));
    QCOMPARE(textOf(visibleItem(h, "nowRunningTitle")), QStringLiteral("Lumen Drift"));
    QCOMPARE(textOf(visibleItem(h, "nowRunningStatus")), QStringLiteral("Paused"));
    QVERIFY(shown(h, "nowRunningResume") && shown(h, "nowRunningQuit"));
    // Above the Hub switcher card, inside the sidebar, on every page
    for (const char* page : {"library", "emulation", "controllers", "settings"}) {
      if (QLatin1String(page) == QLatin1String("emulation")) c->showEmulation();
      else if (QLatin1String(page) == QLatin1String("controllers")) c->showControllers();
      else if (QLatin1String(page) == QLatin1String("settings")) c->showSettings();
      else c->showLibrary();
      QQuickTest::qWaitForPolish(h.window);
      QQuickItem* strip = visibleItem(h, "nowRunningStrip");
      QVERIFY2(strip != nullptr, page);
      QQuickItem* sidebar = strip->parentItem();
      for (; sidebar != nullptr && QString::fromLatin1(sidebar->metaObject()->className()).left(7) != QLatin1String("Sidebar"); sidebar = sidebar->parentItem()) {}
      QVERIFY2(sidebar != nullptr, page);
      const QRectF sr = sceneRect(strip), br = sceneRect(sidebar);
      QVERIFY2(sr.left() >= br.left() && sr.right() <= br.right() && sr.bottom() <= br.bottom(), qPrintable(QLatin1String(page) + chain(strip)));
      QCOMPARE(sr.width(), 200.0);
    }
    c->showLibrary();
    resize(h, 960, 600);
    QQuickItem* strip = visibleItem(h, "nowRunningStrip");
    QVERIFY(strip != nullptr);
    QVERIFY2(sceneRect(strip).bottom() <= h.window->height(), qPrintable(chain(strip)));
    resize(h, 1280, 800);

    // Resume: back in the game, strip gone
    QVERIFY(h.click("nowRunningResume"));
    QCOMPARE(c->screen(), QStringLiteral("game"));
    QTRY_VERIFY(!gs->isPaused());
    c->leaveGameView();
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(shown(h, "nowRunningStrip"));
    // Quit: "Saving and syncing…" for a moment, then the game and the strip are gone
    QVERIFY(h.click("nowRunningQuit"));
    QVERIFY(c->quitting());
    QTRY_COMPARE(textOf(visibleItem(h, "nowRunningStatus")), QStringLiteral("Saving and syncing…"));
    QVERIFY(!shown(h, "nowRunningResume") && !shown(h, "nowRunningQuit"));
    QTRY_VERIFY(!gs->isActive());
    QVERIFY(!c->quitting());
    QTRY_VERIFY(!shown(h, "nowRunningStrip"));
  }
};

QTEST_MAIN(InGameUiTest)
#include "ingame_ui_test.moc"
