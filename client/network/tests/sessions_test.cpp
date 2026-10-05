// SessionApi and HubSocket against the in-process FakeHub (REST + minimal WSS on the same pinned TLS port).
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "hubsocket.h"
#include "profilestore.h"
#include "sessionapi.h"

using namespace framebeam;
using State = HubConnection::State;
using K = SessionApiResult::Kind;

namespace {
const QString kSession = QStringLiteral("00000000-0000-4000-8000-000000000001");
const QString kViewer = QStringLiteral("11111111-1111-4111-8111-111111111111");

QJsonObject sessionJson() {
  return {{QStringLiteral("session_id"), kSession},
          {QStringLiteral("game_id"), QStringLiteral("9a1b2c3d-4e5f-4a6b-8c7d-0e1f2a3b4c5d")},
          {QStringLiteral("game_title"), QStringLiteral("Demo Homebrew")},
          {QStringLiteral("owner"), QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u1")},
                                                {QStringLiteral("display_name"), QStringLiteral("Fabio")},
                                                {QStringLiteral("device_name"), QStringLiteral("PC")}}},
          {QStringLiteral("visibility"), QStringLiteral("hub_users")},
          {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:00:00Z")},
          {QStringLiteral("viewer_count"), 1},
          {QStringLiteral("viewers"), QJsonArray{QJsonObject{{QStringLiteral("viewer_id"), kViewer},
                                                             {QStringLiteral("display_name"), QStringLiteral("Anna")},
                                                             {QStringLiteral("device_name"), QStringLiteral("Laptop")}}}},
          {QStringLiteral("is_owner"), true},
          {QStringLiteral("invited"), false}};
}
}  // namespace

class SessionsTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<HubConnection> conn_;

  void connectTo(FakeHub& hub) {
    hub.decision = FakeHub::Decision::Approve;
    conn_->connectToAddress(hub.address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }

  SessionApiResult call(const std::function<void(SessionApi&, SessionApi::Callback)>& fn, SessionApi& api) {
    SessionApiResult out;
    bool done = false;
    fn(api, [&](const SessionApiResult& r) {
      out = r;
      done = true;
    });
    if (!QTest::qWaitFor([&]() { return done; }, 8000)) {
      out.errorCode = QStringLiteral("test_timeout");
    }
    return out;
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
  }
  void cleanup() {
    conn_.reset();
    profiles_.reset();
    creds_.reset();
    dir_.reset();
  }

  void sessionApiEndpoints() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("sessions_v1")};
    QVERIFY(hub.start());
    connectTo(hub);
    QVERIFY(conn_->hubHasFeature(QString::fromLatin1(kSessionsFeature)));
    SessionApi api(conn_.get());

    auto r = call([](SessionApi& a, auto cb) { a.publish(QStringLiteral("g-1"), QStringLiteral("hub_users"), cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    QVERIFY(r.session.has_value());
    QCOMPARE(r.session->visibility, QStringLiteral("hub_users"));
    QCOMPARE(r.session->gameId, QStringLiteral("g-1"));
    QVERIFY(r.session->isOwner);
    const QString id = r.session->sessionId;

    r = call([](SessionApi& a, auto cb) { a.list(cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    QCOMPARE(r.sessions.size(), 1);
    r = call([&](SessionApi& a, auto cb) { a.get(id, cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    r = call([&](SessionApi& a, auto cb) { a.setVisibility(id, QStringLiteral("private"), cb); }, api);
    QCOMPARE(r.session->visibility, QStringLiteral("private"));
    r = call([&](SessionApi& a, auto cb) { a.invite(id, QStringLiteral("u_x"), cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    r = call([&](SessionApi& a, auto cb) { a.withdrawInvite(id, QStringLiteral("u_x"), cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    r = call([&](SessionApi& a, auto cb) { a.decline(id, cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    r = call([&](SessionApi& a, auto cb) { a.join(id, cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    QCOMPARE(r.join->viewerId, kViewer);
    QVERIFY(!r.join->permissions.sendInput);
    QCOMPARE(r.join->iceServers, QStringList{QStringLiteral("stun:stun.example.org:3478")});
    r = call([&](SessionApi& a, auto cb) { a.removeViewer(id, kViewer, cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    r = call([](SessionApi& a, auto cb) { a.listUsers(cb); }, api);
    QCOMPARE(r.users.size(), 1);
    QCOMPARE(r.users.first().displayName, QStringLiteral("Tester"));

    hub.joinFull = true;
    r = call([&](SessionApi& a, auto cb) { a.join(id, cb); }, api);
    QCOMPARE(r.kind, K::Full);
    QCOMPARE(r.errorCode, QStringLiteral("session_full"));
    hub.publishCapabilityMissing = true;
    r = call([](SessionApi& a, auto cb) { a.publish(QStringLiteral("g-1"), QStringLiteral("private"), cb); }, api);
    QCOMPARE(r.kind, K::CapabilityMissing);
    r = call([](SessionApi& a, auto cb) { a.get(QStringLiteral("nope"), cb); }, api);
    QCOMPARE(r.kind, K::NotFound);

    r = call([&](SessionApi& a, auto cb) { a.end(id, cb); }, api);
    QCOMPARE(r.kind, K::Ok);
    QVERIFY(hub.sessions.isEmpty());
    // Bearer on every request
    for (const FakeRequest& q : hub.requests) {
      if (q.path.contains(QLatin1String("/sessions"))) {
        QVERIFY(q.headers.value(QStringLiteral("authorization")).startsWith("Bearer fba_"));
      }
    }
  }

  void hubSocketHelloAndTypedMessages() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    connectTo(hub);
    HubSocket ws(conn_.get());
    QSignalSpy acked(&ws, &HubSocket::helloAcked);
    QSignalSpy presence(&ws, &HubSocket::presenceUpdated);
    QSignalSpy updated(&ws, &HubSocket::sessionUpdated);
    QSignalSpy invited(&ws, &HubSocket::sessionInvited);
    QSignalSpy ended(&ws, &HubSocket::sessionEnded);
    QSignalSpy joined(&ws, &HubSocket::viewerJoined);
    QSignalSpy left(&ws, &HubSocket::viewerLeft);
    QSignalSpy signals_(&ws, &HubSocket::signalReceived);
    QSignalSpy errors(&ws, &HubSocket::hubError);
    ws.start();
    QTRY_COMPARE_WITH_TIMEOUT(acked.count(), 1, 8000);
    QVERIFY(ws.isOpen());
    QVERIFY(hub.lastWsAuthorization.startsWith("Bearer fba_"));
    QCOMPARE(hub.wsReceived.first().value(QStringLiteral("type")).toString(), QStringLiteral("hello"));
    const QJsonObject hello = hub.wsReceived.first().value(QStringLiteral("payload")).toObject();
    QCOMPARE(hello.value(QStringLiteral("device_id")).toString(), conn_->deviceId());
    QCOMPARE(hello.value(QStringLiteral("protocol_version")).toInt(), 1);
    QVERIFY(ws.helloAck().features.contains(QStringLiteral("sessions_v1")));

    hub.sendWs(QStringLiteral("presence_update"),
               {{QStringLiteral("user_id"), QStringLiteral("u2")}, {QStringLiteral("device_id"), QStringLiteral("d2")},
                {QStringLiteral("state"), QStringLiteral("in_game")}, {QStringLiteral("game_id"), QStringLiteral("g1")}});
    hub.sendWs(QStringLiteral("session_update"), {{QStringLiteral("session"), sessionJson()}});
    hub.sendWs(QStringLiteral("session_invite"), {{QStringLiteral("session"), sessionJson()}});
    hub.sendWs(QStringLiteral("session_ended"), {{QStringLiteral("session_id"), kSession}, {QStringLiteral("reason"), QStringLiteral("replaced")}});
    hub.sendWs(QStringLiteral("viewer_joined"), {{QStringLiteral("session_id"), kSession}, {QStringLiteral("viewer_id"), kViewer},
                                                 {QStringLiteral("display_name"), QStringLiteral("Anna")},
                                                 {QStringLiteral("device_name"), QStringLiteral("Laptop")}});
    hub.sendWs(QStringLiteral("viewer_left"), {{QStringLiteral("session_id"), kSession}, {QStringLiteral("viewer_id"), kViewer},
                                               {QStringLiteral("reason"), QStringLiteral("removed")}});
    hub.sendWs(QStringLiteral("signal"), {{QStringLiteral("session_id"), kSession}, {QStringLiteral("viewer_id"), kViewer},
                                          {QStringLiteral("kind"), QStringLiteral("candidate")},
                                          {QStringLiteral("candidate"), QStringLiteral("candidate:1 1 UDP 1 127.0.0.1 9 typ host")},
                                          {QStringLiteral("mid"), QStringLiteral("video")}});
    hub.sendWs(QStringLiteral("error"), {{QStringLiteral("code"), QStringLiteral("not_found")}, {QStringLiteral("message"), QStringLiteral("peer offline")}});
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 8000);
    QCOMPARE(presence.count(), 1);
    QCOMPARE(presence.first().first().value<PresenceUpdate>().state, QStringLiteral("in_game"));
    QCOMPARE(updated.count(), 1);
    const auto s = updated.first().first().value<SessionInfo>();
    QCOMPARE(s.sessionId, kSession);
    QCOMPARE(s.viewers.size(), 1);
    QCOMPARE(s.viewers.first().displayName, QStringLiteral("Anna"));
    QVERIFY(s.isOwner);
    QCOMPARE(invited.count(), 1);
    QCOMPARE(ended.first().first().value<SessionEnded>().reason, QStringLiteral("replaced"));
    QCOMPARE(joined.first().first().value<ViewerJoined>().viewerId, kViewer);
    QCOMPARE(left.first().first().value<ViewerLeft>().reason, QStringLiteral("removed"));
    const auto sig = signals_.first().first().value<SessionSignal>();
    QCOMPARE(sig.kind, QStringLiteral("candidate"));
    QCOMPARE(sig.mid, QStringLiteral("video"));
    QCOMPARE(errors.first().at(0).toString(), QStringLiteral("not_found"));

    // Player -> Hub
    ws.sendSignal({kSession, kViewer, QStringLiteral("offer"), QStringLiteral("v=0"), {}, {}});
    ws.sendPresence(QStringLiteral("in_game"), QStringLiteral("g1"));
    QTRY_VERIFY_WITH_TIMEOUT(hub.wsReceived.size() >= 3, 8000);
    const QJsonObject sigOut = hub.wsReceived.at(1).value(QStringLiteral("payload")).toObject();
    QCOMPARE(hub.wsReceived.at(1).value(QStringLiteral("type")).toString(), QStringLiteral("signal"));
    QCOMPARE(sigOut.value(QStringLiteral("kind")).toString(), QStringLiteral("offer"));
    QCOMPARE(sigOut.value(QStringLiteral("viewer_id")).toString(), kViewer);
    QCOMPARE(hub.wsReceived.at(2).value(QStringLiteral("payload")).toObject().value(QStringLiteral("game_id")).toString(), QStringLiteral("g1"));
    ws.stop();
    QCOMPARE(ws.state(), HubSocket::State::Stopped);
  }

  void reconnectsWithBackoffAndRenewsRejectedToken() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    connectTo(hub);
    HubSocket ws(conn_.get());
    ws.setBackoffMs(50, 200);
    QSignalSpy acked(&ws, &HubSocket::helloAcked);
    QSignalSpy errs(&ws, &HubSocket::connectionError);
    ws.start();
    QTRY_COMPARE_WITH_TIMEOUT(acked.count(), 1, 8000);

    hub.closeWsClients();  // connection drop: reconnect
    QTRY_COMPARE_WITH_TIMEOUT(acked.count(), 2, 8000);
    QCOMPARE(hub.wsConnections, 2);

    const int tokensBefore = hub.tokenRequests();
    hub.refuseWs = true;  // upgrade rejected with 401: report, renew token, keep trying
    hub.closeWsClients();
    QTRY_VERIFY_WITH_TIMEOUT(!errs.isEmpty() && errs.last().at(0).toString() == QLatin1String("unauthorized"), 8000);
    QTRY_VERIFY_WITH_TIMEOUT(hub.tokenRequests() > tokensBefore, 8000);
    hub.refuseWs = false;
    QTRY_COMPARE_WITH_TIMEOUT(acked.count(), 3, 8000);
    QVERIFY(ws.isOpen());
  }
};

QTEST_GUILESS_MAIN(SessionsTest)
#include "sessions_test.moc"
