// Screenshots (Uzume 0.19): the API calls against FakeHub (upload with and without a run, list, content, delete, the refusals) and
// the pending queue (a capture is written to disk first, stays when the upload fails, goes after a retry, and a refused image is
// dropped). Synthetic PNG bytes only.
#include <QBuffer>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "profilestore.h"
#include "screenshotapi.h"
#include "screenshotqueue.h"

using namespace uzume;
using State = HubConnection::State;

namespace {
// A valid PNG header and IHDR for a w x h image followed by filler: the fake server only looks at those bytes.
QByteArray fakePng(int w, int h, int filler = 64) {
  QByteArray b("\x89PNG\r\n\x1a\n", 8);
  const auto be32 = [](quint32 v) {
    QByteArray o;
    for (int s = 24; s >= 0; s -= 8) o.append(char((v >> s) & 0xff));
    return o;
  };
  b += be32(13) + "IHDR" + be32(quint32(w)) + be32(quint32(h)) + QByteArray(5, '\0');
  b += QByteArray(filler, 'x');
  return b;
}
}  // namespace

class ScreenshotsTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;

  ScreenshotResult call(const std::function<void(ScreenshotApi&, ScreenshotApi::Callback)>& fn, ScreenshotApi& api) {
    ScreenshotResult out;
    bool done = false;
    fn(api, [&](const ScreenshotResult& r) {
      out = r;
      done = true;
    });
    if (!QTest::qWaitFor([&]() { return done; }, 8000)) out.errorCode = QStringLiteral("test_timeout");
    return out;
  }
  QString pendingDir() const { return QDir(dir_->path()).filePath(QStringLiteral("screenshots/pending")); }
  int filesIn(const QString& dir) const { return QDir(dir).entryList(QDir::Files).size(); }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->screenshotsUnsupported = false;
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

  void parseScreenshotReadsEveryField() {
    const auto s = parseScreenshot({{"id", "s1"}, {"game_id", "g1"}, {"run_id", QJsonValue::Null}, {"user_id", "u1"}, {"user_name", "Lena"},
                                    {"created_at", "2026-10-20T10:00:00Z"}, {"width", 256}, {"height", 384}, {"size_bytes", 1234}});
    QVERIFY(s.has_value());
    QCOMPARE(s->id, QStringLiteral("s1"));
    QVERIFY(s->runId.isEmpty());
    QCOMPARE(s->userName, QStringLiteral("Lena"));
    QCOMPARE(s->width, 256);
    QCOMPARE(s->sizeBytes, qint64(1234));
    QVERIFY(!parseScreenshot({{"game_id", "g1"}}).has_value());
  }

  void uploadListContentDelete() {
    ScreenshotApi api(conn_.get());
    const QByteArray png = fakePng(256, 192);
    auto r = call([&](ScreenshotApi& a, auto cb) { a.upload(QStringLiteral("g1"), {}, png, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.status, 201);
    QCOMPARE(r.screenshot.gameId, QStringLiteral("g1"));
    QVERIFY(r.screenshot.runId.isEmpty());
    QCOMPARE(r.screenshot.width, 256);
    QCOMPARE(r.screenshot.height, 192);
    QCOMPARE(r.screenshot.sizeBytes, qint64(png.size()));
    QCOMPARE(r.screenshot.userName, QStringLiteral("Tester"));
    const QString first = r.screenshot.id;
    // Run-tagged: the run id travels as a query parameter
    r = call([&](ScreenshotApi& a, auto cb) { a.upload(QStringLiteral("g1"), QStringLiteral("run-1"), fakePng(10, 10), cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.screenshot.runId, QStringLiteral("run-1"));
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/games/g1/screenshots?run_id=run-1"), "POST"), 1);
    QCOMPARE(hub_->requests.last().headers.value(QStringLiteral("content-type")), QByteArray("image/png"));
    // Other game
    r = call([&](ScreenshotApi& a, auto cb) { a.upload(QStringLiteral("g2"), {}, fakePng(8, 8), cb); }, api);
    QVERIFY(r.ok);

    r = call([](ScreenshotApi& a, auto cb) { a.listGame(QStringLiteral("g1"), cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.screenshots.size(), 2);
    QCOMPARE(r.screenshots.first().runId, QStringLiteral("run-1"));  // newest first
    QCOMPARE(r.screenshots.last().id, first);
    QVERIFY(r.totalBytes > 0);
    r = call([](ScreenshotApi& a, auto cb) { a.listRun(QStringLiteral("run-1"), cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.screenshots.size(), 1);

    r = call([&](ScreenshotApi& a, auto cb) { a.content(first, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.content, png);

    r = call([&](ScreenshotApi& a, auto cb) { a.remove(first, cb); }, api);
    QVERIFY(r.ok);
    QCOMPARE(r.status, 204);
    r = call([](ScreenshotApi& a, auto cb) { a.listGame(QStringLiteral("g1"), cb); }, api);
    QCOMPARE(r.screenshots.size(), 1);
    r = call([&](ScreenshotApi& a, auto cb) { a.remove(first, cb); }, api);
    QCOMPARE(r.status, 404);
  }

  void refusalsAreRejectedForGood() {
    ScreenshotApi api(conn_.get());
    auto r = call([](ScreenshotApi& a, auto cb) { a.upload(QStringLiteral("g1"), {}, QByteArray("not an image at all, just text"), cb); }, api);
    QVERIFY(!r.ok);
    QCOMPARE(r.status, 400);
    QCOMPARE(r.errorCode, QStringLiteral("invalid_image"));
    QVERIFY(r.rejected());
    hub_->screenshotMaxBytes = 100;
    r = call([](ScreenshotApi& a, auto cb) { a.upload(QStringLiteral("g1"), {}, fakePng(4, 4, 500), cb); }, api);
    QCOMPARE(r.status, 413);
    QCOMPARE(r.errorCode, QStringLiteral("payload_too_large"));
    QVERIFY(r.rejected());
    hub_->screenshotFailNext = 1;
    r = call([](ScreenshotApi& a, auto cb) { a.upload(QStringLiteral("g1"), {}, fakePng(4, 4), cb); }, api);
    QVERIFY(!r.ok);
    QVERIFY(!r.rejected());
    QVERIFY(r.transient());
  }

  void olderServerAnswers404() {
    hub_->screenshotsUnsupported = true;
    ScreenshotApi api(conn_.get());
    const auto r = call([](ScreenshotApi& a, auto cb) { a.listGame(QStringLiteral("g1"), cb); }, api);
    QVERIFY(!r.ok);
    QCOMPARE(r.status, 404);
  }

  void queueKeepsAFailedUploadAndRetriesIt() {
    ScreenshotApi api(conn_.get());
    ScreenshotQueue queue(&api, pendingDir());
    const QString hub = conn_->hubId();
    const QString user = conn_->hubUserId();
    QSignalSpy uploaded(&queue, &ScreenshotQueue::uploaded);
    QSignalSpy failed(&queue, &ScreenshotQueue::failed);

    QString error;
    const QString id = queue.enqueue(fakePng(32, 32), QStringLiteral("g1"), QStringLiteral("run-9"), hub, user, &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(filesIn(pendingDir()), 2);  // the PNG first, with its sidecar
    QCOMPARE(queue.count(hub, user), 1);
    QCOMPARE(queue.count(QStringLiteral("other-hub"), user), 0);  // another server's entries are not ours

    hub_->screenshotFailNext = 1;  // server error: the file stays
    queue.upload(id);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
    QCOMPARE(uploaded.count(), 0);
    QCOMPARE(filesIn(pendingDir()), 2);
    QCOMPARE(queue.count(hub, user), 1);

    queue.retryAll(hub, user);  // the next connection: it works
    QTRY_COMPARE_WITH_TIMEOUT(uploaded.count(), 1, 8000);
    QCOMPARE(uploaded.first().at(0).toString(), id);
    QCOMPARE(uploaded.first().at(1).value<ScreenshotInfo>().runId, QStringLiteral("run-9"));
    QCOMPARE(filesIn(pendingDir()), 0);  // deleted only after the 201
    QCOMPARE(hub_->screenshots.size(), 1);
    QCOMPARE(hub_->screenshots.first().meta.value(QStringLiteral("game_id")).toString(), QStringLiteral("g1"));
  }

  void queueRetriesOldestFirstAndStopsWhenTheServerIsDown() {
    ScreenshotApi api(conn_.get());
    ScreenshotQueue queue(&api, pendingDir());
    const QString hub = conn_->hubId();
    const QString user = conn_->hubUserId();
    for (int i = 0; i < 3; ++i) {
      QVERIFY(!queue.enqueue(fakePng(10 + i, 10), QStringLiteral("g1"), {}, hub, user).isEmpty());
      QTest::qWait(5);
    }
    hub_->screenshotFailNext = 100;
    QSignalSpy failed(&queue, &ScreenshotQueue::failed);
    queue.retryAll(hub, user);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
    QTest::qWait(150);
    QCOMPARE(failed.count(), 1);  // a round ends at the first failure
    QCOMPARE(queue.count(hub, user), 3);
    hub_->screenshotFailNext = 0;
    QSignalSpy uploaded(&queue, &ScreenshotQueue::uploaded);
    queue.retryAll(hub, user);
    QTRY_COMPARE_WITH_TIMEOUT(uploaded.count(), 3, 8000);
    QCOMPARE(queue.count(hub, user), 0);
    QCOMPARE(filesIn(pendingDir()), 0);
    // oldest first: the widths follow the order of capture
    QCOMPARE(hub_->screenshots.at(0).meta.value(QStringLiteral("width")).toInt(), 10);
    QCOMPARE(hub_->screenshots.at(2).meta.value(QStringLiteral("width")).toInt(), 12);
  }

  void queueDropsARefusedImageWithAReason() {
    ScreenshotApi api(conn_.get());
    ScreenshotQueue queue(&api, pendingDir());
    const QString hub = conn_->hubId();
    const QString user = conn_->hubUserId();
    const QString bad = queue.enqueue(QByteArray("this is not a png"), QStringLiteral("g1"), {}, hub, user);
    QVERIFY(!bad.isEmpty());
    QSignalSpy dropped(&queue, &ScreenshotQueue::dropped);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dropped.*invalid_image")));
    queue.upload(bad);
    QTRY_COMPARE_WITH_TIMEOUT(dropped.count(), 1, 8000);
    QVERIFY(dropped.first().at(1).toString().contains(QStringLiteral("400")));
    QCOMPARE(filesIn(pendingDir()), 0);
    QCOMPARE(hub_->screenshots.size(), 0);
  }

  void queueWithoutConnectionKeepsEverything() {
    ScreenshotApi api(conn_.get());
    ScreenshotQueue queue(&api, pendingDir());
    const QString hub = conn_->hubId();
    const QString user = conn_->hubUserId();
    const QString id = queue.enqueue(fakePng(6, 6), QStringLiteral("g1"), {}, hub, user);
    conn_->disconnectFromHub();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() != State::Connected, 4000);
    QSignalSpy failed(&queue, &ScreenshotQueue::failed);
    queue.upload(id);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 4000);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("not_connected"));
    QCOMPARE(filesIn(pendingDir()), 2);
  }
};

QTEST_MAIN(ScreenshotsTest)
#include "screenshots_test.moc"
