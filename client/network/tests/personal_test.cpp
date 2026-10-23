// Per-person library data (Uzume 0.19): `personal` of the game list, favourites, collections, `run_id` in the presence message,
// account and run play time. FakeHub serves the routes in memory.
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "accountapi.h"
#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "hubsocket.h"
#include "personalapi.h"
#include "profilestore.h"
#include "runtypes.h"

using namespace uzume;
using State = HubConnection::State;

namespace {
const QString kSha = QString(64, QLatin1Char('a'));

QJsonObject gameJson(const QString& id, const QJsonValue& personal) {
  QJsonObject o{{QStringLiteral("id"), id},
                {QStringLiteral("title"), QStringLiteral("Homebrew ") + id},
                {QStringLiteral("system"), QStringLiteral("nds")},
                {QStringLiteral("rom"), QJsonObject{{QStringLiteral("sha256"), kSha}, {QStringLiteral("size"), 10}, {QStringLiteral("filename"), id + QStringLiteral(".nds")}}},
                {QStringLiteral("added_at"), QStringLiteral("2026-01-01T12:00:00Z")}};
  if (!personal.isUndefined()) o.insert(QStringLiteral("personal"), personal);
  return o;
}
}  // namespace

class PersonalTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;

  PersonalResult call(const std::function<void(PersonalApi&, PersonalApi::Callback)>& fn, PersonalApi& api) {
    PersonalResult out;
    bool done = false;
    fn(api, [&](const PersonalResult& r) {
      out = r;
      done = true;
    });
    if (!QTest::qWaitFor([&]() { return done; }, 8000)) out.errorCode = QStringLiteral("test_timeout");
    return out;
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->collectionsUnsupported = false;
    QVERIFY(hub_->start());
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
    conn_->connectToAddress(hub_->address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
    hub_->decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }
  void cleanup() {
    conn_.reset();
    hub_.reset();
    profiles_.reset();
    creds_.reset();
    dir_.reset();
  }

  void personalIsParsedAndMissingMeansUnknown() {
    const QJsonObject personal{{QStringLiteral("play_seconds"), 5400},
                               {QStringLiteral("sessions"), 3},
                               {QStringLiteral("last_played_at"), QStringLiteral("2026-10-01T18:00:00Z")},
                               {QStringLiteral("favorite"), true},
                               {QStringLiteral("collection_ids"), QJsonArray{QStringLiteral("c1"), QStringLiteral("c2")}}};
    hub_->games = QJsonObject{{QStringLiteral("games"), QJsonArray{gameJson(QStringLiteral("g1"), personal), gameJson(QStringLiteral("g2"), QJsonValue::Undefined),
                                                                   gameJson(QStringLiteral("g3"), QJsonObject{{QStringLiteral("last_played_at"), QJsonValue::Null}})}}};
    HubLibrary library(conn_.get());
    QSignalSpy loaded(&library, &HubLibrary::loaded);
    library.reload();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);
    const GamePersonal a = library.gameById(QStringLiteral("g1"))->personal;
    QVERIFY(a.known);
    QCOMPARE(a.playSeconds, qint64(5400));
    QCOMPARE(a.sessions, 3);
    QCOMPARE(a.lastPlayedAt, QStringLiteral("2026-10-01T18:00:00Z"));
    QVERIFY(a.favorite);
    QCOMPARE(a.collectionIds, (QStringList{"c1", "c2"}));
    // An older server sends none: unknown, zeros, empty.
    const GamePersonal b = library.gameById(QStringLiteral("g2"))->personal;
    QVERIFY(!b.known);
    QCOMPARE(b.playSeconds, qint64(0));
    QVERIFY(!b.favorite);
    QVERIFY(b.collectionIds.isEmpty());
    // Never played: null last_played_at.
    const GamePersonal c = library.gameById(QStringLiteral("g3"))->personal;
    QVERIFY(c.known);
    QVERIFY(c.lastPlayedAt.isEmpty());
  }

  void favoriteSendsPutAndDelete() {
    PersonalApi api(conn_.get());
    auto r = call([](PersonalApi& a, auto cb) { a.setFavorite(QStringLiteral("g1"), true, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.status, 204);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/games/g1/favorite"), "PUT"), 1);
    QCOMPARE(hub_->favoriteGames, QStringList{"g1"});
    r = call([](PersonalApi& a, auto cb) { a.setFavorite(QStringLiteral("g1"), false, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/games/g1/favorite"), "DELETE"), 1);
    QVERIFY(hub_->favoriteGames.isEmpty());
  }

  void collectionRequests() {
    PersonalApi api(conn_.get());
    auto r = call([](PersonalApi& a, auto cb) { a.createCollection(QStringLiteral("  Couch co-op "), {QStringLiteral("g1")}, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.status, 201);
    QCOMPARE(r.collection.name, QStringLiteral("Couch co-op"));  // trimmed
    QCOMPARE(r.collection.gameIds, QStringList{"g1"});
    const QString id = r.collection.id;
    QVERIFY(!id.isEmpty());
    // 409 = the name is already used
    r = call([](PersonalApi& a, auto cb) { a.createCollection(QStringLiteral("couch CO-OP"), {}, cb); }, api);
    QVERIFY(!r.ok);
    QVERIFY(r.nameTaken());
    r = call([&](PersonalApi& a, auto cb) { a.renameCollection(id, QStringLiteral("Nuzlocke"), cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.collection.name, QStringLiteral("Nuzlocke"));
    r = call([&](PersonalApi& a, auto cb) { a.setInCollection(id, QStringLiteral("g2"), true, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/collections/") + id + QStringLiteral("/games/g2"), "PUT"), 1);
    r = call([](PersonalApi& a, auto cb) { a.listCollections(cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.collections.size(), 1);
    QCOMPARE(r.collections.first().gameIds, (QStringList{"g1", "g2"}));
    r = call([&](PersonalApi& a, auto cb) { a.setInCollection(id, QStringLiteral("g1"), false, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/collections/") + id + QStringLiteral("/games/g1"), "DELETE"), 1);
    r = call([&](PersonalApi& a, auto cb) { a.deleteCollection(id, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.status, 204);
    QVERIFY(hub_->collections.isEmpty());
    // An unknown collection
    r = call([](PersonalApi& a, auto cb) { a.renameCollection(QStringLiteral("nope"), QStringLiteral("X"), cb); }, api);
    QCOMPARE(r.status, 404);
  }

  void olderServerAnswers404() {
    hub_->collectionsUnsupported = true;
    PersonalApi api(conn_.get());
    const auto r = call([](PersonalApi& a, auto cb) { a.listCollections(cb); }, api);
    QVERIFY(!r.ok);
    QCOMPARE(r.status, 404);
  }

  void presenceCarriesRunIdOnlyInGame() {
    HubSocket ws(conn_.get());
    QSignalSpy acked(&ws, &HubSocket::helloAcked);
    ws.start();
    QTRY_COMPARE_WITH_TIMEOUT(acked.count(), 1, 8000);
    ws.sendPresence(QStringLiteral("in_game"), QStringLiteral("g1"), QStringLiteral("run-1"));
    QTRY_VERIFY_WITH_TIMEOUT(hub_->wsReceived.size() >= 2, 8000);
    QJsonObject p = hub_->wsReceived.last().value(QStringLiteral("payload")).toObject();
    QCOMPARE(p.value(QStringLiteral("state")).toString(), QStringLiteral("in_game"));
    QCOMPARE(p.value(QStringLiteral("run_id")).toString(), QStringLiteral("run-1"));
    // No run id: the field is left out; a state other than in_game never carries one.
    ws.sendPresence(QStringLiteral("in_game"), QStringLiteral("g1"));
    QTRY_VERIFY_WITH_TIMEOUT(hub_->wsReceived.size() >= 3, 8000);
    QVERIFY(!hub_->wsReceived.last().value(QStringLiteral("payload")).toObject().contains(QStringLiteral("run_id")));
    ws.sendPresence(QStringLiteral("online"), {}, QStringLiteral("run-1"));
    QTRY_VERIFY_WITH_TIMEOUT(hub_->wsReceived.size() >= 4, 8000);
    QVERIFY(!hub_->wsReceived.last().value(QStringLiteral("payload")).toObject().contains(QStringLiteral("run_id")));
    ws.stop();
  }

  void accountAndRunPlayTime() {
    const auto acc = parseAccount({{QStringLiteral("user_id"), QStringLiteral("u1")},
                                   {QStringLiteral("play_seconds"), 7200},
                                   {QStringLiteral("sessions"), 4},
                                   {QStringLiteral("last_played_at"), QStringLiteral("2026-10-02T10:00:00Z")}});
    QVERIFY(acc.has_value());
    QCOMPARE(acc->playSeconds, qint64(7200));
    QCOMPARE(acc->sessions, 4);
    QVERIFY(acc->lastPlayedAt.isValid());
    const auto none = parseAccount({{QStringLiteral("user_id"), QStringLiteral("u1")}});
    QCOMPARE(none->playSeconds, qint64(0));
    QVERIFY(!none->lastPlayedAt.isValid());

    const QString runId = QStringLiteral("00000000-0000-4000-8000-000000000001");
    const auto run = parseRun({{QStringLiteral("run_id"), runId},
                               {QStringLiteral("name"), QStringLiteral("Ember Route")},
                               {QStringLiteral("status"), QStringLiteral("active")},
                               {QStringLiteral("play_seconds"), 3600},
                               {QStringLiteral("participants"),
                                QJsonArray{QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u1")},
                                                       {QStringLiteral("user_name"), QStringLiteral("Lena")},
                                                       {QStringLiteral("status"), QStringLiteral("joined")},
                                                       {QStringLiteral("play_seconds"), 1800},
                                                       {QStringLiteral("last_played_at"), QStringLiteral("2026-10-03T08:00:00Z")}}}}});
    QVERIFY(run.has_value());
    QCOMPARE(run->playSeconds, qint64(3600));
    QCOMPARE(run->participants.first().playSeconds, qint64(1800));
    QCOMPARE(run->participants.first().lastPlayedAt, QStringLiteral("2026-10-03T08:00:00Z"));
    // The cache keeps them
    const auto again = parseRun(runToJson(*run));
    QCOMPARE(again->playSeconds, qint64(3600));
    QCOMPARE(again->participants.first().playSeconds, qint64(1800));
  }
};

QTEST_MAIN(PersonalTest)
#include "personal_test.moc"
