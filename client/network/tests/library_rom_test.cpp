#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "profilestore.h"
#include "romcache.h"
#include "romdownloader.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
// Dummy ROM (not a real ROM).
QByteArray dummyRom(char fill) { return QByteArray(20000, fill) + "FRAMEBEAM-DUMMY-ROM"; }
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
}  // namespace

class LibraryRomTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<HubLibrary> library_;
  std::unique_ptr<RomCache> cache_;
  std::unique_ptr<RomDownloader> dl_;
  QByteArray rom_;
  QString sha_;

  static QJsonObject gameJson(const QString& id, const QString& sha, qint64 size, const QString& file) {
    return {{QStringLiteral("id"), id},
            {QStringLiteral("title"), QStringLiteral("Homebrew ") + id},
            {QStringLiteral("system"), QStringLiteral("nds")},
            {QStringLiteral("rom"), QJsonObject{{QStringLiteral("sha256"), sha}, {QStringLiteral("size"), size}, {QStringLiteral("filename"), file}}},
            {QStringLiteral("uploaded_by"), QStringLiteral("u_test_1")},
            {QStringLiteral("added_at"), QStringLiteral("2026-01-01T12:00:00Z")}};
  }

  GameEntry loadGame() {
    QSignalSpy loaded(library_.get(), &HubLibrary::loaded);
    library_->reload();
    [&] { QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000); }();
    return library_->gameByRomSha(sha_).value();
  }

  bool waitFinal(const QString& sha, RomState want) {
    return QTest::qWaitFor([&]() { return dl_->status(*library_->gameByRomSha(sha)).state == want; }, 8000);
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    QVERIFY(hub_->start());
    rom_ = dummyRom('r');
    sha_ = shaOf(rom_);
    hub_->roms.insert(sha_, rom_);
    hub_->games = QJsonObject{{QStringLiteral("games"),
                               QJsonArray{gameJson(QStringLiteral("g1"), sha_, rom_.size(), QStringLiteral("demo.nds")),
                                          gameJson(QStringLiteral("g2"), QString(64, QLatin1Char('1')), 10, QStringLiteral("other.gba")),
                                          QJsonObject{{QStringLiteral("id"), QStringLiteral("broken")}}}}};
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
    library_ = std::make_unique<HubLibrary>(conn_.get());
    cache_ = std::make_unique<RomCache>(profiles_->romCacheDir());
    dl_ = std::make_unique<RomDownloader>(conn_.get(), cache_.get());

    conn_->connectToAddress(hub_->address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
    hub_->decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }
  void cleanup() {
    dl_.reset();
    cache_.reset();
    library_.reset();
    conn_.reset();
    hub_.reset();
    profiles_.reset();
    creds_.reset();
    dir_.reset();
  }

  void libraryLoadsGames() {
    QSignalSpy loaded(library_.get(), &HubLibrary::loaded);
    library_->reload();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);
    QCOMPARE(library_->games().size(), 2);
    QCOMPARE(library_->skippedEntries(), 1);
    const GameEntry g = library_->games().first();
    QCOMPARE(g.id, QStringLiteral("g1"));
    QCOMPARE(g.system, QStringLiteral("nds"));
    QCOMPARE(g.romSize, qint64(rom_.size()));
    QCOMPARE(g.romSha256, sha_);
    // Disconnecting clears the library (never across hubs)
    QSignalSpy cleared(library_.get(), &HubLibrary::cleared);
    conn_->disconnectFromHub();
    QCOMPARE(cleared.count(), 1);
    QVERIFY(library_->games().isEmpty());
  }

  void downloadVerifiesAndCaches() {
    const GameEntry g = loadGame();
    RomStatus st = dl_->status(g);
    QVERIFY(st.state == RomState::DownloadNeeded);
    QCOMPARE(st.totalBytes, qint64(rom_.size()));

    QSignalSpy progress(dl_.get(), &RomDownloader::progress);
    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    QVERIFY(progress.count() >= 1);
    st = dl_->status(g);
    QVERIFY(st.state == RomState::Ready);
    QCOMPARE(st.localPath, cache_->dir() + QStringLiteral("/") + sha_ + QStringLiteral(".nds"));
    QVERIFY(st.localPath.contains(QStringLiteral("cache/roms/")));
    QFile f(st.localPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), rom_);
    QVERIFY(!QFile::exists(cache_->partPath(sha_, QStringLiteral("nds"))));

    // Repeated start: no new ROM transfer
    const int before = hub_->count(QStringLiteral("/api/v1/roms/"));
    dl_->ensureRom(g);
    QCOMPARE(ready.count(), 2);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/roms/")), before);
  }

  void interruptedDownloadResumesWithRange() {
    hub_->truncateFirstRomAt = 7000;
    const GameEntry g = loadGame();
    dl_->ensureRom(g);
    QVERIFY(waitFinal(sha_, RomState::Failed));
    QCOMPARE(cache_->partSize(sha_, QStringLiteral("nds")), qint64(7000));
    QVERIFY(!QFile::exists(cache_->finalPath(sha_, QStringLiteral("nds"))));
    QVERIFY(dl_->status(g).receivedBytes == 7000);

    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    FakeRequest last;
    for (const FakeRequest& r : hub_->requests) {
      if (r.path.startsWith(QLatin1String("/api/v1/roms/"))) {
        last = r;
      }
    }
    QCOMPARE(last.headers.value(QStringLiteral("range")), QByteArray("bytes=7000-"));
    QFile f(dl_->status(g).localPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), rom_);
  }

  void serverIgnoringRangeRestartsFromZero() {
    hub_->truncateFirstRomAt = 5000;
    hub_->ignoreRange = true;
    const GameEntry g = loadGame();
    dl_->ensureRom(g);
    QVERIFY(waitFinal(sha_, RomState::Failed));
    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    QVERIFY(dl_->status(g).state == RomState::Ready);
  }

  void hashMismatchIsReportedAndPartRemoved() {
    QByteArray bad = rom_;
    bad[10] = 'Z';  // same length, different content
    hub_->roms.insert(sha_, bad);
    const GameEntry g = loadGame();
    QSignalSpy changed(dl_.get(), &RomDownloader::statusChanged);
    dl_->ensureRom(g);
    QVERIFY(waitFinal(sha_, RomState::HashMismatch));
    QVERIFY(!QFile::exists(cache_->partPath(sha_, QStringLiteral("nds"))));
    QVERIFY(!QFile::exists(cache_->finalPath(sha_, QStringLiteral("nds"))));
    QCOMPARE(dl_->status(g).errorCode, QStringLiteral("hash_mismatch"));

    // Hub repaired: retry succeeds
    hub_->roms.insert(sha_, rom_);
    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
  }

  bool waitNotValidating(const GameEntry& g) {
    return QTest::qWaitFor([&]() { return dl_->status(g).state != RomState::Validating; }, 8000);
  }

  void corruptedCacheFileIsNotAHit() {
    const GameEntry g = loadGame();
    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    const QString path = cache_->finalPath(sha_, QStringLiteral("nds"));
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::ReadWrite));
      f.seek(100);
      f.write("X");  // same size, different content
      f.setFileTime(QDateTime::currentDateTime().addSecs(30), QFileDevice::FileModificationTime);
    }
    QVERIFY(dl_->status(g).state == RomState::Validating);  // hashing runs off-thread
    QVERIFY(waitNotValidating(g));
    QVERIFY(dl_->status(g).state == RomState::DownloadNeeded);
    QVERIFY(!QFile::exists(path));
  }

  void missingSidecarIsValidatedAsyncWithoutTransfer() {
    const GameEntry g = loadGame();
    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    QVERIFY(QFile::remove(cache_->finalPath(sha_, QStringLiteral("nds")) + QStringLiteral(".ok")));
    const int before = hub_->count(QStringLiteral("/api/v1/roms/"));
    dl_->ensureRom(g);  // valid file, sidecar missing
    QVERIFY(dl_->status(g).state == RomState::Validating);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 8000);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/roms/")), before);
    QVERIFY(dl_->status(g).state == RomState::Ready);
  }

  void corruptedCacheFileIsRedownloadedByEnsureRom() {
    const GameEntry g = loadGame();
    QSignalSpy ready(dl_.get(), &RomDownloader::romReady);
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    const QString path = cache_->finalPath(sha_, QStringLiteral("nds"));
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::ReadWrite));
      f.seek(5);
      f.write("X");
      f.setFileTime(QDateTime::currentDateTime().addSecs(30), QFileDevice::FileModificationTime);
    }
    dl_->ensureRom(g);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 8000);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), rom_);
  }

  void hashMismatchAfterResumeIsDetected() {
    hub_->truncateFirstRomAt = 6000;
    const GameEntry g = loadGame();
    dl_->ensureRom(g);
    QVERIFY(waitFinal(sha_, RomState::Failed));
    QByteArray bad = rom_;
    bad[15000] = 'Z';  // rest after the .part portion is changed
    hub_->roms.insert(sha_, bad);
    dl_->ensureRom(g);
    QVERIFY(waitFinal(sha_, RomState::HashMismatch));
    QVERIFY(!QFile::exists(cache_->partPath(sha_, QStringLiteral("nds"))));
  }

  void notConnectedFailsCleanly() {
    const GameEntry g = loadGame();
    conn_->disconnectFromHub();
    dl_->ensureRom(g);
    QVERIFY(dl_->status(g).state == RomState::Failed);
    QCOMPARE(dl_->status(g).errorCode, QStringLiteral("not_connected"));
  }
};

QTEST_GUILESS_MAIN(LibraryRomTest)
#include "library_rom_test.moc"
