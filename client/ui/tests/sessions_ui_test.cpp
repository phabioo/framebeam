// Phase 4 UI: sessions in the library, watching, Session panel, multiview, diagnostics (offscreen, FakeHub, no real network).
// Tests with a running game need the DS core and the homebrew test ROM (QSKIP without FRAMEBEAM_MELONDS_DS_CORE).
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

namespace {
const QString kViewerId = QStringLiteral("11111111-1111-4111-8111-111111111111");  // fixed by FakeHub join

QJsonObject sessionObj(const QString& id, const QString& who, const QString& game, const QString& vis, bool invited = false,
                       int minutesAgo = 5, bool owner = false) {
  return {{QStringLiteral("session_id"), id},
          {QStringLiteral("game_id"), QStringLiteral("t1")},
          {QStringLiteral("game_title"), game},
          {QStringLiteral("owner"), QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_x")},
                                                {QStringLiteral("display_name"), who},
                                                {QStringLiteral("device_name"), QStringLiteral("PC")}}},
          {QStringLiteral("visibility"), vis},
          {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().addSecs(-60 * minutesAgo).toString(Qt::ISODate)},
          {QStringLiteral("viewer_count"), 0},
          {QStringLiteral("is_owner"), owner},
          {QStringLiteral("invited"), invited}};
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

void prepareHub(FakeHub& hub) {
  hub.hubId = QStringLiteral("hub-sessions");
  hub.name = QStringLiteral("Home");
  hub.decision = FakeHub::Decision::Approve;
  hub.features = {QStringLiteral("saves_v1"), QStringLiteral("sessions_v1")};
}

bool anyVisible(Harness& h, const char* name) {
  for (QQuickItem* i : h.items(name)) {
    if (i->isVisible()) return true;
  }
  return false;
}
QQuickItem* visibleItem(Harness& h, const char* name) {
  for (QQuickItem* i : h.items(name)) {
    if (i->isVisible()) return i;
  }
  return nullptr;
}
QString textOf(QQuickItem* i) { return i ? i->property("text").toString() : QString(); }
int countContaining(const FakeHub& hub, const QString& part, const QByteArray& method = {}) {
  int n = 0;
  for (const FakeRequest& r : hub.requests) {
    if (r.path.contains(part) && (method.isEmpty() || r.method == method)) ++n;
  }
  return n;
}
QJsonObject lastBody(const FakeHub& hub, const QByteArray& method, const QString& pathPrefix) {
  for (qsizetype i = hub.requests.size() - 1; i >= 0; --i) {
    if (hub.requests.at(i).method == method && hub.requests.at(i).path.startsWith(pathPrefix)) {
      return QJsonDocument::fromJson(hub.requests.at(i).body).object();
    }
  }
  return {};
}
}  // namespace

class SessionsUiTest : public QObject {
  Q_OBJECT
  QString ownSessionId(const FakeHub& hub) const {
    for (const QJsonObject& s : hub.sessions) {
      if (s.value(QStringLiteral("is_owner")).toBool()) return s.value(QStringLiteral("session_id")).toString();
    }
    return {};
  }

  public:
  struct GameRig {
    FakeHub hub{QStringLiteral("a")};
    Harness h;
    QByteArray rom;
  };

  static bool startGame(GameRig& r, bool share) {
    if (qEnvironmentVariableIsEmpty("FRAMEBEAM_MELONDS_DS_CORE")) {
      return false;
    }
    QFile f(QStringLiteral(FB_TEST_ROM_PATH));
    if (!f.open(QIODevice::ReadOnly)) {
      return false;
    }
    r.rom = f.readAll();
    const QString sha = uitest::sha256Hex(r.rom);
    prepareHub(r.hub);
    QJsonArray games;
    games.append(uitest::gameJson(QStringLiteral("t1"), QStringLiteral("Framebeam Test"), sha, r.rom.size(), QStringLiteral("framebeam_test.nds")));
    r.hub.games = QJsonObject{{QStringLiteral("games"), games}};
    r.hub.roms.insert(sha, r.rom);
    r.hub.fakeUsers = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("u_test_1")}, {QStringLiteral("display_name"), QStringLiteral("Tester")}, {QStringLiteral("online"), true}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("u_sam")}, {QStringLiteral("display_name"), QStringLiteral("Sam")}, {QStringLiteral("online"), true}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("u_sarah")}, {QStringLiteral("display_name"), QStringLiteral("Sarah")}, {QStringLiteral("online"), false}}};
    r.hub.sessions.insert(QStringLiteral("s1"), sessionObj(QStringLiteral("s1"), QStringLiteral("Lena"), QStringLiteral("Harbor Rally"), QStringLiteral("hub_users")));
    if (!r.hub.start() || !r.h.start(true)) {
      return false;
    }
    pair(r.h, r.hub);
    if (QTest::currentTestFailed()) return false;
    if (!QTest::qWaitFor([&]() { return r.h.controller->libraryState() == QLatin1String("ready") && r.h.controller->selectedGameId() == QLatin1String("t1"); }, 8000)) {
      return false;
    }
    if (share) {
      r.h.controller->playAndShareSelected();
    } else {
      r.h.controller->playSelected();
    }
    return true;
  }

 private slots:
  void initTestCase() { uitest::installWarningCounter(); }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  void handshakeReportsMediaCapabilitiesTruthfully() {
    FakeHub hub(QStringLiteral("a"));
    prepareHub(hub);
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QJsonObject video;
    for (const FakeRequest& r : hub.requests) {
      if (r.path.endsWith(QLatin1String("/handshake"))) {
        video = QJsonDocument::fromJson(r.body).object().value(QStringLiteral("video")).toObject();
      }
    }
    QVERIFY(video.value(QStringLiteral("h264_decode")).toBool());
    QVERIFY(video.value(QStringLiteral("h264_encode")).toBool());
    QVERIFY(!video.value(QStringLiteral("encoders")).toArray().isEmpty());
    // WSS: connected, hello with the device id, presence "online" announced.
    QTRY_VERIFY(hub.wsConnections >= 1);
    QTRY_VERIFY(!hub.wsReceived.isEmpty() && hub.wsReceived.first().value(QStringLiteral("type")).toString() == QLatin1String("hello"));
  }

  void sessionsSectionRenderingAndActions() {
    FakeHub hub(QStringLiteral("a"));
    prepareHub(hub);
    hub.sessions.insert(QStringLiteral("s1"), sessionObj(QStringLiteral("s1"), QStringLiteral("Lena"), QStringLiteral("Harbor Rally"), QStringLiteral("hub_users"), false, 12));
    hub.sessions.insert(QStringLiteral("s2"), sessionObj(QStringLiteral("s2"), QStringLiteral("Jonas"), QStringLiteral("Lumen Drift"), QStringLiteral("invite_only"), true, 1));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    SessionController* ctl = h.controller->sessions();
    QTRY_COMPARE(ctl->sessions().size(), 2);
    const QVariantList list = ctl->sessions();
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("sessionId")).toString(), QStringLiteral("s2"));  // newest first
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Jonas · Lumen Drift"));
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("meta")).toString(), QStringLiteral("inviting you"));
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Lena · Harbor Rally"));
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("meta")).toString(), QStringLiteral("Hub users · for 12 min"));

    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("sessionsSection")->isVisible());
    QVERIFY(h.item("watch_s1") && h.item("watch_s1")->isVisible());
    QCOMPARE(textOf(h.item("watch_s1")), QStringLiteral("Watch Session"));
    QVERIFY(h.item("join_s2") && h.item("join_s2")->isVisible());
    QCOMPARE(textOf(h.item("join_s2")), QStringLiteral("Join"));
    QVERIFY(h.item("decline_s2") && h.item("decline_s2")->isVisible());
    QVERIFY(h.item("watch_s2") == nullptr);  // an invite shows Join/Decline, not Watch
    QVERIFY(h.item("decline_s1") == nullptr || !h.item("decline_s1")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("p4-3c-sessions"));

    // Live updates: new Session, invite, ended, no_longer_visible
    hub.sendWs(QStringLiteral("session_update"), {{QStringLiteral("session"), sessionObj(QStringLiteral("s3"), QStringLiteral("Mia"), QStringLiteral("Paper Wizards"), QStringLiteral("hub_users"))}});
    QTRY_COMPARE(ctl->sessions().size(), 3);
    hub.sendWs(QStringLiteral("session_ended"), {{QStringLiteral("session_id"), QStringLiteral("s3")}, {QStringLiteral("reason"), QStringLiteral("ended")}});
    QTRY_COMPARE(ctl->sessions().size(), 2);
    hub.sendWs(QStringLiteral("session_ended"), {{QStringLiteral("session_id"), QStringLiteral("s1")}, {QStringLiteral("reason"), QStringLiteral("no_longer_visible")}});
    QTRY_COMPARE(ctl->sessions().size(), 1);
    hub.sendWs(QStringLiteral("session_invite"), {{QStringLiteral("session"), sessionObj(QStringLiteral("s4"), QStringLiteral("Sam"), QStringLiteral("Stylus Knights"), QStringLiteral("invite_only"), true)}});
    QTRY_COMPARE(ctl->sessions().size(), 2);

    // Decline
    QTRY_VERIFY(h.item("decline_s2") && h.item("decline_s2")->isVisible());
    QVERIFY(h.click("decline_s2"));
    QTRY_COMPARE(ctl->sessions().size(), 1);
    QCOMPARE(hub.count(QStringLiteral("/api/v1/sessions/s2/decline")), 1);

    // Empty: section hidden
    hub.sendWs(QStringLiteral("session_ended"), {{QStringLiteral("session_id"), QStringLiteral("s4")}, {QStringLiteral("reason"), QStringLiteral("ended")}});
    QTRY_COMPARE(ctl->sessions().size(), 0);
    QTRY_VERIFY(!h.item("sessionsSection")->isVisible());
  }

  void hubLinkLossShowsBannerAndRecovers() {
    FakeHub hub(QStringLiteral("a"));
    prepareHub(hub);
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    SessionController* ctl = h.controller->sessions();
    ctl->socket()->setBackoffMs(100, 300);
    QVERIFY(!h.item("hubLinkBanner")->isVisible());
    hub.refuseWs = true;
    hub.closeWsClients();
    QTRY_VERIFY(ctl->hubLink() != QLatin1String("online"));
    QTRY_VERIFY(h.item("hubLinkBanner")->isVisible());
    hub.refuseWs = false;
    QTRY_COMPARE_WITH_TIMEOUT(ctl->hubLink(), QStringLiteral("online"), 10000);
    QTRY_VERIFY(!h.item("hubLinkBanner")->isVisible());
  }

  void watchWithoutLocalGameShowsRemoteAlone() {
    FakeHub hub(QStringLiteral("a"));
    prepareHub(hub);
    hub.sessions.insert(QStringLiteral("s1"), sessionObj(QStringLiteral("s1"), QStringLiteral("Lena"), QStringLiteral("Harbor Rally"), QStringLiteral("hub_users")));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    SessionController* ctl = h.controller->sessions();
    QTRY_COMPARE(ctl->sessions().size(), 1);
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.click("watch_s1"));
    QTRY_VERIFY(ctl->watching());
    QCOMPARE(hub.count(QStringLiteral("/api/v1/sessions/s1/join")), 1);
    QCOMPARE(h.controller->screen(), QStringLiteral("game"));
    QCOMPARE(ctl->tab(), QStringLiteral("multiview"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("remote"));  // only audible surface
    QCOMPARE(ctl->watchedWho(), QStringLiteral("Lena"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("tabSession") == nullptr);  // no local game: no Session tab
    QVERIFY(h.item("tabMultiview") != nullptr);
    QVERIFY(visibleItem(h, "remoteView") != nullptr);
    QVERIFY(visibleItem(h, "audioRemoteButton") != nullptr);
    QCOMPARE(textOf(visibleItem(h, "audioRemoteButton")), QStringLiteral("Audio on"));
    QCOMPARE(textOf(h.item("gameTitle")), QStringLiteral("Session from Lena"));
    uitest::saveShot(h.window, QStringLiteral("p4-remote-alone"));

    // Remote frame arrives -> RemoteView redraws
    const quint64 nr = ctl->remoteFrameNumber();
    QImage img(256, 384, QImage::Format_RGB32);
    img.fill(QColor(10, 200, 30));
    QMetaObject::invokeMethod(ctl->viewer(), "frameReady", Qt::DirectConnection, Q_ARG(QImage, img));
    QCOMPARE(ctl->remoteFrameNumber(), nr + 1);

    // Diagnostics tab with statistics (fake): n/a where unavailable
    SessionStats remote;
    remote.active = true;
    remote.fps = 59.9;
    remote.connectionType = QStringLiteral("host");
    remote.rttMs = 14.0;
    remote.videoBitrateKbps = 5800;
    remote.packetLossPercent = 0.1;
    ctl->setStatsOverride(nullptr, &remote);
    ctl->setTab(QStringLiteral("diagnostics"));
    QQuickTest::qWaitForPolish(h.window);
    QQuickItem* row = visibleItem(h, "diagRow_remote");
    QVERIFY(row != nullptr);
    const QString t = textOf(row);
    QVERIFY2(t.contains(QStringLiteral("59.9 fps")) && t.contains(QStringLiteral("WebRTC direct (host)")) && t.contains(QStringLiteral("RTT 14 ms")) &&
                 t.contains(QStringLiteral("5.8 Mbit/s")) && t.contains(QStringLiteral("loss 0.1 %")),
             qPrintable(t));
    QVERIFY(visibleItem(h, "diagRow_local") == nullptr);  // no local game
    remote.rttMs.reset();
    remote.packetLossPercent.reset();
    ctl->setStatsOverride(nullptr, &remote);
    QVERIFY2(textOf(visibleItem(h, "diagRow_remote")).contains(QStringLiteral("RTT n/a")) &&
                 textOf(visibleItem(h, "diagRow_remote")).contains(QStringLiteral("loss n/a")),
             qPrintable(textOf(visibleItem(h, "diagRow_remote"))));
    uitest::saveShot(h.window, QStringLiteral("p4-diagnostics-remote"));
    ctl->setStatsOverride(nullptr, nullptr);

    // Hub removes the viewer: closed with a short notice, back to the Library
    hub.sendWs(QStringLiteral("viewer_left"), {{QStringLiteral("session_id"), QStringLiteral("s1")}, {QStringLiteral("viewer_id"), kViewerId},
                                               {QStringLiteral("reason"), QStringLiteral("removed")}});
    QTRY_VERIFY(!ctl->watching());
    QCOMPARE(ctl->message(), QStringLiteral("You were removed from the Session."));
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
    QTRY_VERIFY(h.item("sessionMessage")->isVisible());

    // Session ended while watching
    ctl->dismissMessage();
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.click("watch_s1"));
    QTRY_VERIFY(ctl->watching());
    hub.sendWs(QStringLiteral("session_ended"), {{QStringLiteral("session_id"), QStringLiteral("s1")}, {QStringLiteral("reason"), QStringLiteral("ended")}});
    QTRY_VERIFY(!ctl->watching());
    QCOMPARE(ctl->message(), QStringLiteral("The Session ended."));
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));

    // Leaving = DELETE viewer ("← Library")
    hub.sessions.insert(QStringLiteral("s5"), sessionObj(QStringLiteral("s5"), QStringLiteral("Lena"), QStringLiteral("Harbor Rally"), QStringLiteral("hub_users")));
    hub.sendWs(QStringLiteral("session_update"), {{QStringLiteral("session"), hub.sessions.value(QStringLiteral("s5"))}});
    QTRY_VERIFY(h.item("watch_s5") != nullptr);
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.click("watch_s5"));
    QTRY_VERIFY(ctl->watching());
    QTRY_COMPARE(h.controller->screen(), QStringLiteral("game"));
    QQuickTest::qWaitForPolish(h.window);
    QTRY_VERIFY(h.item("backToLibraryButton")->isVisible());
    QVERIFY(h.click("backToLibraryButton"));
    QTRY_VERIFY(!ctl->watching());
    QTRY_VERIFY(hub.count(QStringLiteral("/api/v1/sessions/s5/viewers/")) >= 1);
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
  }

  void formatDiagnosticsPure() {
    SessionStats local;
    local.active = true;
    local.encoderName = QStringLiteral("h264_nvenc");
    local.fps = 60.0;
    local.videoBitrateKbps = 6000;
    local.audioBitrateKbps = 128;
    const QVariantList rows = SessionController::formatDiagnostics(QStringLiteral("You · Game"), true, true, 59.0, local, QString(), false, SessionStats());
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("values")).toStringList(),
             (QStringList{QStringLiteral("60.0 fps"), QStringLiteral("encoder h264_nvenc"), QStringLiteral("H.264"), QStringLiteral("6.0 Mbit/s"),
                          QStringLiteral("Opus 128 kbit/s")}));
    // Not shared: encoder off, local fps from the game, other values n/a
    const QVariantList off = SessionController::formatDiagnostics(QStringLiteral("You · Game"), true, false, 59.0, SessionStats(), QString(), false, SessionStats());
    const QStringList v = off.at(0).toMap().value(QStringLiteral("values")).toStringList();
    QCOMPARE(v.at(0), QStringLiteral("59.0 fps"));
    QVERIFY(v.at(1).contains(QStringLiteral("off")));
    QCOMPARE(v.at(4), QStringLiteral("Opus n/a"));
  }

  // ---- with a running game (real core) ----

  void playAndShareThenPanelStates() {
    GameRig r;
    if (!startGame(r, true)) QSKIP("core or test ROM not available");
    Harness& h = r.h;
    FakeHub& hub = r.hub;
    SessionController* ctl = h.controller->sessions();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(ctl->shared(), 20000);
    QCOMPARE(lastBody(hub, "POST", QStringLiteral("/api/v1/sessions")).value(QStringLiteral("visibility")).toString(), QStringLiteral("hub_users"));
    QCOMPARE(lastBody(hub, "POST", QStringLiteral("/api/v1/sessions")).value(QStringLiteral("game_id")).toString(), QStringLiteral("t1"));
    // presence: in_game with the game id
    QTRY_VERIFY(std::any_of(hub.wsReceived.cbegin(), hub.wsReceived.cend(), [](const QJsonObject& e) {
      return e.value(QStringLiteral("type")).toString() == QLatin1String("presence_update") &&
             e.value(QStringLiteral("payload")).toObject().value(QStringLiteral("state")).toString() == QLatin1String("in_game") &&
             e.value(QStringLiteral("payload")).toObject().value(QStringLiteral("game_id")).toString() == QLatin1String("t1");
    }));
    QQuickTest::qWaitForPolish(h.window);

    // Shared, hub_users: pill, no invite block, Stop sharing
    QCOMPARE(ctl->tab(), QStringLiteral("session"));
    QVERIFY(h.item("sharedPill")->isVisible());
    QCOMPARE(textOf(h.item("sharedPill")->findChild<QQuickItem*>()), QStringLiteral("Session shared · 0 watching"));
    QCOMPARE(ctl->viewerCount(), 0);
    QVERIFY(h.item("tabSession") && h.item("tabMultiview") && h.item("tabDiagnostics"));
    QVERIFY(h.item("visibilitySegment")->isVisible());
    QVERIFY(!h.item("inviteBlock")->isVisible());
    QCOMPARE(textOf(h.item("shareButton")), QStringLiteral("Stop sharing"));
    QVERIFY(h.item("readOnlyNote")->isVisible());
    QVERIFY(h.item("endGameButton")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("p4-3g-shared"));

    // A viewer joins: offer goes out through the Hub, the pill counts
    hub.sendWs(QStringLiteral("viewer_joined"), {{QStringLiteral("session_id"), ownSessionId(hub)}, {QStringLiteral("viewer_id"), QStringLiteral("v1")},
                                                 {QStringLiteral("display_name"), QStringLiteral("Lena")}, {QStringLiteral("device_name"), QStringLiteral("PC")}});
    QTRY_VERIFY_WITH_TIMEOUT(ctl->host()->hasViewer(QStringLiteral("v1")), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(hub.wsReceived.cbegin(), hub.wsReceived.cend(), [](const QJsonObject& e) {
      return e.value(QStringLiteral("type")).toString() == QLatin1String("signal") &&
             e.value(QStringLiteral("payload")).toObject().value(QStringLiteral("kind")).toString() == QLatin1String("offer");
    }), 20000);

    // Visibility "Invite only" (PATCH), persisted in the settings under the data dir
    QVERIFY(h.click("visInviteOnly"));
    QTRY_COMPARE(ctl->visibility(), QStringLiteral("invite_only"));
    QTRY_COMPARE(lastBody(hub, "PATCH", QStringLiteral("/api/v1/sessions/")).value(QStringLiteral("visibility")).toString(), QStringLiteral("invite_only"));
    QFile settings(h.dir.path() + QStringLiteral("/player-settings.json"));
    QVERIFY(settings.open(QIODevice::ReadOnly));
    QVERIFY(settings.readAll().contains("invite_only"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("inviteBlock")->isVisible());

    // Hub sends the Session with viewers and invites
    QJsonObject own = hub.sessions.value(ownSessionId(hub));
    own.insert(QStringLiteral("visibility"), QStringLiteral("invite_only"));
    own.insert(QStringLiteral("viewer_count"), 1);
    own.insert(QStringLiteral("viewers"), QJsonArray{QJsonObject{{QStringLiteral("viewer_id"), QStringLiteral("v1")}, {QStringLiteral("display_name"), QStringLiteral("Lena")}, {QStringLiteral("device_name"), QStringLiteral("PC")}}});
    own.insert(QStringLiteral("invites"), QJsonArray{
        QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_lena")}, {QStringLiteral("display_name"), QStringLiteral("Lena")}, {QStringLiteral("state"), QStringLiteral("joined")}, {QStringLiteral("online"), true}},
        QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_jonas")}, {QStringLiteral("display_name"), QStringLiteral("Jonas")}, {QStringLiteral("state"), QStringLiteral("invited")}, {QStringLiteral("online"), false}},
        QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_mia")}, {QStringLiteral("display_name"), QStringLiteral("Mia")}, {QStringLiteral("state"), QStringLiteral("declined")}, {QStringLiteral("online"), true}}});
    hub.sendWs(QStringLiteral("session_update"), {{QStringLiteral("session"), own}});
    QTRY_COMPARE(ctl->participants().size(), 3);
    QCOMPARE(ctl->participantsTitle(), QStringLiteral("INVITED · 3"));
    QCOMPARE(ctl->viewerCount(), 1);
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("remove_v1")->isVisible());
    QVERIFY(h.item("withdraw_u_jonas")->isVisible());
    QVERIFY(h.item("withdraw_u_mia")->isVisible());
    const QVariantList parts = ctl->participants();
    QCOMPARE(parts.at(0).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("watching"));
    QCOMPARE(parts.at(1).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("invited · offline"));
    QCOMPARE(parts.at(2).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("declined"));
    uitest::saveShot(h.window, QStringLiteral("p4-3g-invite-only"));

    // Invite search over GET /users (self excluded), invite = PUT
    h.item("inviteField")->setProperty("text", QStringLiteral("sa"));
    QTRY_COMPARE(ctl->userResults().size(), 2);  // Sam, Sarah; "Tester" is the own user
    QCOMPARE(ctl->userResults().at(1).toMap().value(QStringLiteral("hint")).toString(),
             QStringLiteral("offline · receives the invite as long as the Session is running"));
    QQuickTest::qWaitForPolish(h.window);
    QTRY_VERIFY(h.item("invite_u_sam") != nullptr && h.item("invite_u_sam")->isVisible());
    QVERIFY(h.click("invite_u_sam"));
    QTRY_VERIFY_WITH_TIMEOUT(countContaining(hub, QStringLiteral("/invites/u_sam"), "PUT") == 1, 15000);

    // (the fake PUT answers with a bare Session: the Hub's next update restores viewers and invites)
    hub.sendWs(QStringLiteral("session_update"), {{QStringLiteral("session"), own}});
    QTRY_COMPARE(ctl->participants().size(), 3);
    QQuickTest::qWaitForPolish(h.window);
    // Remove (DELETE viewer) and Withdraw (DELETE invite)
    QVERIFY(h.click("remove_v1"));
    QTRY_VERIFY(countContaining(hub, QStringLiteral("/viewers/v1"), "DELETE") == 1);
    QVERIFY(!ctl->host()->hasViewer(QStringLiteral("v1")));
    QVERIFY(h.click("withdraw_u_jonas"));
    QTRY_VERIFY(countContaining(hub, QStringLiteral("/invites/u_jonas"), "DELETE") == 1);

    // Visibility "Private": only watchers are listed, no invite block
    QVERIFY(h.click("visPrivate"));
    QTRY_COMPARE(ctl->visibility(), QStringLiteral("private"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(!h.item("inviteBlock")->isVisible());
    QVERIFY(ctl->participantsTitle().startsWith(QStringLiteral("WATCHING")));

    // Diagnostics collapsible inside the Session tab
    QVERIFY(!anyVisible(h, "diagRow_local"));
    QVERIFY(h.click("diagToggle"));
    QQuickTest::qWaitForPolish(h.window);
    QTRY_VERIFY(anyVisible(h, "diagRow_local"));
    SessionStats local;
    local.active = true;
    local.encoderName = QStringLiteral("libx264");
    local.fps = 60.0;
    local.videoBitrateKbps = 2000;
    local.audioBitrateKbps = 96;
    ctl->setStatsOverride(&local, nullptr);
    const QString lt = textOf(visibleItem(h, "diagRow_local"));
    QVERIFY2(lt.contains(QStringLiteral("encoder libx264")) && lt.contains(QStringLiteral("H.264")) && lt.contains(QStringLiteral("2.0 Mbit/s")) &&
                 lt.contains(QStringLiteral("Opus 96 kbit/s")),
             qPrintable(lt));
    ctl->setStatsOverride(nullptr, nullptr);

    // Stop sharing -> DELETE session, pill gone, button back to "Share Session"
    QVERIFY(h.click("shareButton"));
    QTRY_VERIFY(!ctl->shared());
    QTRY_VERIFY(!hub.sessions.contains(QStringLiteral("00000000-0000-4000-8000-000000000002")) || ownSessionId(hub).isEmpty());
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(!h.item("sharedPill")->isVisible());
    QCOMPARE(textOf(h.item("shareButton")), QStringLiteral("Share Session"));
    // Share again with the remembered visibility, Hub ends it (replaced)
    QVERIFY(h.click("shareButton"));
    QTRY_VERIFY(ctl->shared());
    QCOMPARE(lastBody(hub, "POST", QStringLiteral("/api/v1/sessions")).value(QStringLiteral("visibility")).toString(), QStringLiteral("private"));
    hub.sendWs(QStringLiteral("session_ended"), {{QStringLiteral("session_id"), ownSessionId(hub)}, {QStringLiteral("reason"), QStringLiteral("replaced")}});
    QTRY_VERIFY(!ctl->shared());
    QVERIFY(ctl->message().contains(QStringLiteral("replaced")));

    // Quit ends the game (and a shared Session)
    QVERIFY(h.click("shareButton"));
    QTRY_VERIFY(ctl->shared());
    QVERIFY(h.click("endGameButton"));
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
    QVERIFY(!ctl->shared());
    QTRY_VERIFY(countContaining(hub, QStringLiteral("/api/v1/sessions/"), "DELETE") >= 2);  // stopped + ended with the game
  }

  void multiviewModesAndAudioFocus() {
    GameRig r;
    if (!startGame(r, false)) QSKIP("core or test ROM not available");
    Harness& h = r.h;
    SessionController* ctl = h.controller->sessions();
    GameSession* gs = h.controller->gameSession();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 20000);
    QQuickTest::qWaitForPolish(h.window);
    QCOMPARE(ctl->tab(), QStringLiteral("session"));
    QVERIFY(!ctl->shared());
    QCOMPARE(textOf(h.item("shareButton")), QStringLiteral("Share Session"));
    QVERIFY(!h.item("sharedPill")->isVisible());  // pill only while shared
    QVERIFY(!gs->audioMuted());

    // Watch from the Library list while playing: Multiview, PiP, local audible
    QTRY_COMPARE(ctl->sessions().size(), 1);
    ctl->watch(QStringLiteral("s1"));
    QTRY_VERIFY(ctl->watching());
    QCOMPARE(ctl->tab(), QStringLiteral("multiview"));
    QCOMPARE(ctl->multiviewMode(), QStringLiteral("pip"));
    QCOMPARE(h.controller->screen(), QStringLiteral("game"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("local"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("pipWindow")->isVisible());
    QVERIFY(!h.item("sideBySide")->isVisible());
    QVERIFY(visibleItem(h, "gameViewMulti") != nullptr);
    QVERIFY(visibleItem(h, "remoteView") != nullptr);
    QVERIFY(!h.item("mainAudioButton")->isVisible());   // main surface (local) already has the audio
    QVERIFY(h.item("pipAudioButton")->isVisible());     // PiP is muted
    QVERIFY(h.item("swapButton")->isVisible() && h.item("removeRemoteButton")->isVisible());
    // PiP bottom right
    const QPointF pipPos = h.item("pipWindow")->mapToItem(h.item("pipMode"), QPointF(h.item("pipWindow")->width(), h.item("pipWindow")->height()));
    QVERIFY(std::abs(pipPos.x() - (h.item("pipMode")->width() - 28)) < 1.5 && std::abs(pipPos.y() - (h.item("pipMode")->height() - 28)) < 1.5);
    uitest::saveShot(h.window, QStringLiteral("p4-3i-pip"));

    // Audio here (remote): exactly one audible surface
    QVERIFY(h.click("pipAudioButton"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("remote"));
    QVERIFY(gs->audioMuted());
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("mainAudioButton")->isVisible());
    QVERIFY(h.click("mainAudioButton"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("local"));
    QVERIFY(!gs->audioMuted());

    // Swap
    QVERIFY(h.click("swapButton"));
    QVERIFY(ctl->swapped());
    QVERIFY(h.click("swapButton"));
    QVERIFY(!ctl->swapped());

    // Side-by-side (3h)
    QVERIFY(h.click("modeSide"));
    QCOMPARE(ctl->multiviewMode(), QStringLiteral("side"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("sideBySide")->isVisible());
    QVERIFY(!h.item("pipWindow")->isVisible());
    QVERIFY(!h.item("audioLocalButton")->isEnabled() && h.item("audioLocalButton")->property("text").toString() == QLatin1String("Audio on"));
    QVERIFY(visibleItem(h, "audioRemoteButton")->isEnabled() && textOf(visibleItem(h, "audioRemoteButton")) == QLatin1String("Audio here"));
    uitest::saveShot(h.window, QStringLiteral("p4-3h-side"));
    QVERIFY(h.click("audioRemoteButton"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("remote"));
    QVERIFY(gs->audioMuted());
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.item("audioLocalButton")->isEnabled() && !visibleItem(h, "audioRemoteButton")->isEnabled());
    QVERIFY(h.click("audioLocalButton"));
    QCOMPARE(ctl->audioFocus(), QStringLiteral("local"));
    QVERIFY(!gs->audioMuted());

    // Diagnostics tab expanded under the multiview: local and remote rows
    SessionStats local, remote;
    local.active = true;
    local.encoderName = QStringLiteral("h264_nvenc");
    local.fps = 60.0;
    local.videoBitrateKbps = 6000;
    local.audioBitrateKbps = 128;
    remote.active = true;
    remote.fps = 59.9;
    remote.connectionType = QStringLiteral("srflx");
    remote.rttMs = 14;
    remote.videoBitrateKbps = 5800;
    remote.packetLossPercent = 0.1;
    ctl->setStatsOverride(&local, &remote);
    QVERIFY(h.click("tabDiagnostics"));
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(visibleItem(h, "diagnosticsPanel") != nullptr);
    QVERIFY(textOf(visibleItem(h, "diagRow_local")).contains(QStringLiteral("encoder h264_nvenc")));
    QVERIFY(textOf(visibleItem(h, "diagRow_remote")).contains(QStringLiteral("WebRTC direct (srflx)")));
    uitest::saveShot(h.window, QStringLiteral("p4-3h-diagnostics"));
    ctl->setStatsOverride(nullptr, nullptr);
    QVERIFY(h.click("tabMultiview"));
    QVERIFY(h.click("modePip"));

    // Remove (PiP): leaves the Session (DELETE viewer), local game alone again
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(h.click("removeRemoteButton"));
    QVERIFY(!ctl->watching());
    QTRY_VERIFY(r.hub.count(QStringLiteral("/api/v1/sessions/s1/viewers/")) >= 1);
    QCOMPARE(ctl->audioFocus(), QStringLiteral("local"));
    QVERIFY(!gs->audioMuted());
    QCOMPARE(h.controller->screen(), QStringLiteral("game"));
    QVERIFY(h.click("quitButton"));
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
  }
};

UITEST_MAIN(SessionsUiTest)
#include "sessions_ui_test.moc"
