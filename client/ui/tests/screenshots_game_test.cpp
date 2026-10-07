// Screenshots of the game view (0.6 UI pass, designs 3g, 3h, 3i, 3r, 3t, 3u, 3w, 3y) with fake data at 1440x900.
// Active only with FRAMEBEAM_SCREENSHOT_DIR=<dir> (otherwise QSKIP); needs neither a core nor a ROM: the game is a
// preview (GameSession::setPreview) and the Hub is the FakeHub. The PNGs are for the review, never checked in.
#include <QDir>
#include <QPainter>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

namespace {

QJsonObject sessionObj(const QString& id, const QString& who, const QString& game, const QString& vis, bool owner = false) {
  return {{QStringLiteral("session_id"), id},
          {QStringLiteral("game_id"), QStringLiteral("t1")},
          {QStringLiteral("game_title"), game},
          {QStringLiteral("owner"), QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_") + who.toLower()},
                                                {QStringLiteral("display_name"), who},
                                                {QStringLiteral("device_name"), QStringLiteral("PC")}}},
          {QStringLiteral("visibility"), vis},
          {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().addSecs(-600).toString(Qt::ISODate)},
          {QStringLiteral("viewer_count"), 0},
          {QStringLiteral("is_owner"), owner},
          {QStringLiteral("invited"), false}};
}

void pair(Harness& h, FakeHub& hub) {
  h.controller->addHub(hub.address());
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation, 8000);
  h.controller->confirmTrust();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsPairing, 8000);
  h.controller->requestPairing();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->sessions()->hubLink(), QStringLiteral("online"), 8000);
}

// Two stacked screens (top: sky, bottom: a panel with a few buttons), `scale` times the native 256x384.
QImage fakeFrame(int scale, const QColor& tint) {
  QImage img(256 * scale, 384 * scale, QImage::Format_RGB32);
  QPainter p(&img);
  p.scale(scale, scale);
  QLinearGradient sky(0, 0, 0, 192);
  sky.setColorAt(0, tint.lighter(150));
  sky.setColorAt(1, tint.darker(160));
  p.fillRect(QRect(0, 0, 256, 192), sky);
  p.setPen(Qt::NoPen);
  p.setBrush(tint.darker(220));
  p.drawPolygon(QPolygon({QPoint(0, 192), QPoint(60, 110), QPoint(110, 150), QPoint(170, 90), QPoint(256, 192)}));
  p.setBrush(QColor(255, 255, 255, 200));
  p.drawEllipse(QPoint(200, 44), 16, 16);
  p.fillRect(QRect(0, 192, 256, 192), QColor(28, 34, 44));
  p.setBrush(tint);
  for (int i = 0; i < 3; ++i) {
    p.drawRoundedRect(QRect(24, 212 + i * 52, 208, 40), 8, 8);
  }
  return img;
}

emu::DisplayProfile ndsProfile() {
  emu::DisplayProfile p;
  p.layout = QStringLiteral("vertical");
  p.gap = 0;
  p.screens = {{QStringLiteral("top"), 256, 192, false}, {QStringLiteral("bottom"), 256, 192, true}};
  return p;
}

QVector<float> history(float base, float jitter, int n = 300) {
  QVector<float> v;
  v.reserve(n);
  for (int i = 0; i < n; ++i) {
    v.append(base + jitter * std::sin(i * 0.37f) + (i % 61 == 0 ? jitter * 3 : 0.0f));
  }
  return v;
}

EmulationDiagnostics softwareDiag() {
  EmulationDiagnostics d;
  d.valid = true;
  d.core = QStringLiteral("melonDS DS 1.4.0");
  d.frameSize = QSize(256, 384);
  d.baseSize = QSize(256, 384);
  d.screens = 2;
  d.cpuThreads = 12;
  d.fps = 59.8;
  d.targetFps = 59.83;
  d.frameMs = 7.4;
  d.emuMs = 7.4;
  d.totalHistory = history(7.4f, 0.8f);
  d.emuHistory = d.totalHistory;
  d.audioActive = true;
  d.audioBufferMs = 84;
  return d;
}

EmulationDiagnostics openglDiag() {
  EmulationDiagnostics d = softwareDiag();
  d.hwRequested = d.hwActive = true;
  d.api = QStringLiteral("OpenGL 4.6 Core");
  d.gpu = QStringLiteral("Example GPU · Driver 560.35");
  d.frameSize = QSize(768, 1152);
  d.requestedScale = 3;
  d.frameMs = 5.1;
  d.emuMs = 3.7;
  d.readbackMs = 1.4;
  d.totalHistory = history(5.1f, 0.5f);
  d.emuHistory = history(3.7f, 0.4f);
  return d;
}

EmulationDiagnostics fallbackDiag() {
  EmulationDiagnostics d = softwareDiag();
  d.hwRequested = true;
  d.hwActive = false;
  d.fallbackReason = QStringLiteral("no OpenGL context");
  d.requestedScale = 3;
  d.frameMs = 14.9;
  d.emuMs = 14.9;
  d.fps = 52.4;
  d.totalHistory = history(14.9f, 3.0f);
  d.emuHistory = d.totalHistory;
  d.underruns = 3;
  d.audioBufferMs = 31;
  return d;
}

}  // namespace

class ScreenshotsGameTest : public QObject {
  Q_OBJECT

  QString dir_;
  FakeHub hub_{QStringLiteral("a")};
  Harness h_;

  void shot(const QString& name) {
    QQuickTest::qWaitForPolish(h_.window);
    QTest::qWait(250);  // settle animations and canvases
    const QImage img = h_.window->grabWindow();
    QVERIFY2(!img.isNull() && img.save(QDir(dir_).filePath(name + QStringLiteral(".png"))), qPrintable(name));
  }
  QQuickItem* gameScreen() {
    for (QQuickItem* i = h_.item("gameHeader"); i != nullptr; i = i->parentItem()) {
      if (QString::fromLatin1(i->metaObject()->className()).startsWith(QLatin1String("GameScreen"))) return i;
    }
    return nullptr;
  }
  void preview(const EmulationDiagnostics& d, int scale, const QColor& tint) {
    h_.controller->gameSession()->setPreview(QStringLiteral("Framebeam Test"), fakeFrame(scale, tint), ndsProfile(), d);
    h_.controller->sessions()->refreshDiagnostics();
  }
  void feedRemote(const QString& id, const QColor& tint) {
    SessionController* ctl = h_.controller->sessions();
    QMetaObject::invokeMethod(ctl->viewer(id), "frameReady", Qt::DirectConnection, Q_ARG(QImage, fakeFrame(1, tint)));
  }

 private slots:
  void renderGameScreens() {
    dir_ = qEnvironmentVariable("FRAMEBEAM_SCREENSHOT_DIR");
    if (dir_.isEmpty()) QSKIP("FRAMEBEAM_SCREENSHOT_DIR not set");
    QVERIFY(QDir().mkpath(dir_));

    hub_.hubId = QStringLiteral("hub-shots");
    hub_.name = QStringLiteral("Home");
    hub_.decision = FakeHub::Decision::Approve;
    hub_.features = {QStringLiteral("saves_v1"), QStringLiteral("sessions_v1")};
    hub_.sessions.insert(QStringLiteral("s1"), sessionObj(QStringLiteral("s1"), QStringLiteral("Lena"), QStringLiteral("Harbor Rally"), QStringLiteral("hub_users")));
    hub_.sessions.insert(QStringLiteral("s2"), sessionObj(QStringLiteral("s2"), QStringLiteral("Mo"), QStringLiteral("Lumen Drift"), QStringLiteral("hub_users")));
    hub_.sessions.insert(QStringLiteral("s3"), sessionObj(QStringLiteral("s3"), QStringLiteral("Jonas"), QStringLiteral("Pixel Quest"), QStringLiteral("hub_users")));
    hub_.fakeUsers = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("u_test_1")}, {QStringLiteral("display_name"), QStringLiteral("Tester")}, {QStringLiteral("online"), true}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("u_sam")}, {QStringLiteral("display_name"), QStringLiteral("Sam")}, {QStringLiteral("online"), true}}};
    QVERIFY(hub_.start());
    QVERIFY(h_.start());
    pair(h_, hub_);
    h_.window->resize(1440, 900);
    QTest::qWait(200);
    SessionController* ctl = h_.controller->sessions();
    GameSession* gs = h_.controller->gameSession();
    QTRY_COMPARE_WITH_TIMEOUT(ctl->sessions().size(), 3, 8000);

    // A preview game instead of a running core: enters the game view like a started game.
    gs->setPreview(QStringLiteral("Framebeam Test"), fakeFrame(1, QColor(60, 140, 200)), ndsProfile(), softwareDiag());
    emit gs->started();
    QTRY_COMPARE_WITH_TIMEOUT(h_.controller->screen(), QStringLiteral("game"), 8000);
    ctl->gameStarted(QStringLiteral("t1"), QStringLiteral("Framebeam Test"));  // the preview has no library entry: give the Session its game id
    QQuickItem* screen = gameScreen();
    QVERIFY(screen != nullptr);
    screen->setProperty("applyToWindow", false);
    ctl->setTab(QStringLiteral("session"));
    QQuickTest::qWaitForPolish(h_.window);

    // 3t: software renderer, no Session
    ctl->diagnostics()->setOpen(true);
    ctl->diagnostics()->setEmulationOpen(true);
    ctl->diagnostics()->setStreamingOpen(true);
    preview(softwareDiag(), 1, QColor(60, 140, 200));
    shot(QStringLiteral("3t-software"));

    // 3u: OpenGL
    preview(openglDiag(), 3, QColor(60, 140, 200));
    shot(QStringLiteral("3u-opengl"));

    // 3w: OpenGL requested, software runs
    preview(fallbackDiag(), 1, QColor(60, 140, 200));
    shot(QStringLiteral("3w-fallback"));

    // 3g: shared Session with viewers; one direct, one over TURN
    preview(openglDiag(), 3, QColor(60, 140, 200));
    ctl->shareSession();
    QTRY_VERIFY_WITH_TIMEOUT(ctl->shared(), 8000);
    QString own;
    for (const QJsonObject& s : std::as_const(hub_.sessions)) {
      if (s.value(QStringLiteral("is_owner")).toBool()) own = s.value(QStringLiteral("session_id")).toString();
    }
    QVERIFY(!own.isEmpty());
    QJsonObject o = hub_.sessions.value(own);
    o.insert(QStringLiteral("viewer_count"), 2);
    o.insert(QStringLiteral("viewers"), QJsonArray{
        QJsonObject{{QStringLiteral("viewer_id"), QStringLiteral("v1")}, {QStringLiteral("display_name"), QStringLiteral("Lena")}, {QStringLiteral("device_name"), QStringLiteral("PC")}},
        QJsonObject{{QStringLiteral("viewer_id"), QStringLiteral("v2")}, {QStringLiteral("display_name"), QStringLiteral("Mo")}, {QStringLiteral("device_name"), QStringLiteral("Laptop")}}});
    o.insert(QStringLiteral("invites"), QJsonArray{
        QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_lena")}, {QStringLiteral("display_name"), QStringLiteral("Lena")}, {QStringLiteral("state"), QStringLiteral("joined")}, {QStringLiteral("online"), true}},
        QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_mo")}, {QStringLiteral("display_name"), QStringLiteral("Mo")}, {QStringLiteral("state"), QStringLiteral("joined")}, {QStringLiteral("online"), true}},
        QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_jonas")}, {QStringLiteral("display_name"), QStringLiteral("Jonas")}, {QStringLiteral("state"), QStringLiteral("invited")}, {QStringLiteral("online"), false}}});
    o.insert(QStringLiteral("visibility"), QStringLiteral("invite_only"));
    hub_.sendWs(QStringLiteral("session_update"), {{QStringLiteral("session"), o}});
    QTRY_VERIFY_WITH_TIMEOUT(ctl->participants().size() >= 3, 8000);
    SessionStats local;
    local.active = true;
    local.encoderName = QStringLiteral("h264_nvenc");
    local.fps = 60.0;
    local.videoBitrateKbps = 6000;
    local.targetBitrateKbps = 6500;
    local.audioBitrateKbps = 128;
    ctl->setStatsOverride(&local, {});
    ViewerLinkStats l1, l2;
    l1.viewerId = QStringLiteral("v1");
    l1.state = QStringLiteral("connected");
    l1.rttMs = 18;
    l1.connectionType = QStringLiteral("direct (srflx)");
    l1.hasReport = true;
    l1.reportKbps = 5800;
    l1.reportLoss = 0.004;
    l1.reportFps = 59.7;
    l1.reportDecoder = QStringLiteral("h264");
    l2.viewerId = QStringLiteral("v2");
    l2.state = QStringLiteral("connected");
    l2.rttMs = 74;
    l2.connectionType = QStringLiteral("relay (udp)");
    l2.hasReport = true;
    l2.reportKbps = 3900;
    l2.reportLoss = 0.012;
    l2.reportFps = 58.9;
    l2.reportDecoder = QStringLiteral("h264");
    ctl->setLinksOverride(true, {l1, l2});
    ctl->refreshDiagnostics();
    shot(QStringLiteral("3g-session"));

    // Remote Sessions: PiP (3i), side by side with the bottom panel (3h), grid with the picker (3r)
    ctl->diagnostics()->setOpen(false);
    ctl->watch(QStringLiteral("s1"));
    QTRY_COMPARE_WITH_TIMEOUT(ctl->surfaceCount(), 2, 8000);
    feedRemote(QStringLiteral("s1"), QColor(200, 110, 60));
    ctl->setStatsOverride(&local, {{QStringLiteral("s1"), SessionStats()}});
    ctl->setTab(QStringLiteral("multiview"));
    ctl->setMultiviewMode(QStringLiteral("pip"));
    QQuickTest::qWaitForPolish(h_.window);
    shot(QStringLiteral("3i-pip"));

    SessionStats remote;
    remote.active = true;
    remote.fps = 59.9;
    remote.connectionType = QStringLiteral("direct (srflx)");
    remote.rttMs = 18;
    remote.videoBitrateKbps = 5800;
    remote.packetLossPercent = 0.4;
    remote.decoderName = QStringLiteral("h264");
    ctl->setStatsOverride(&local, {{QStringLiteral("s1"), remote}});
    ctl->setMultiviewMode(QStringLiteral("side"));
    ctl->diagnostics()->setOpen(true);
    ctl->refreshDiagnostics();
    shot(QStringLiteral("3h-side-diagnostics"));

    ctl->diagnostics()->setOpen(false);
    ctl->watch(QStringLiteral("s2"));
    QTRY_COMPARE_WITH_TIMEOUT(ctl->surfaceCount(), 3, 8000);
    feedRemote(QStringLiteral("s2"), QColor(120, 90, 190));
    ctl->setMultiviewMode(QStringLiteral("grid"));
    QQuickTest::qWaitForPolish(h_.window);
    QVERIFY(h_.click("addSessionToggle"));
    QTRY_VERIFY_WITH_TIMEOUT(h_.item("multiviewSessionList") != nullptr && h_.item("multiviewSessionList")->isVisible(), 4000);
    shot(QStringLiteral("3r-grid-picker"));
    if (h_.item("addSessionToggle")->isVisible()) QVERIFY(h_.click("addSessionToggle"));

    // 3y: fullscreen with the overlay (Emulation only) in the Session tab
    ctl->setTab(QStringLiteral("session"));
    ctl->diagnostics()->setOpen(true);
    QMetaObject::invokeMethod(screen, "setFullscreen", Q_ARG(QVariant, true));
    screen->setProperty("toolbarPinned", true);
    preview(openglDiag(), 3, QColor(60, 140, 200));
    shot(QStringLiteral("3y-fullscreen"));
    screen->setProperty("toolbarPinned", false);
    QMetaObject::invokeMethod(screen, "setFullscreen", Q_ARG(QVariant, false));

    ctl->setStatsOverride(nullptr, {});
    ctl->setLinksOverride(false, {});
    QVERIFY(QFile::exists(QDir(dir_).filePath(QStringLiteral("3y-fullscreen.png"))));
  }
};

UITEST_MAIN(ScreenshotsGameTest)
#include "screenshots_game_test.moc"
