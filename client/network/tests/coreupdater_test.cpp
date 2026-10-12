// CoreUpdater against the fake Hub: background update without a game start, only cached cores, running core kept,
// retry after a failed download. Dummy bytes only (no real core).
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "corecache.h"
#include "coreprovisioner.h"
#include "coreupdater.h"
#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "profilestore.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
const QByteArray kLib(2048, 'l');
const QByteArray kLicense(128, 'x');
}  // namespace

class CoreUpdaterTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<CoreCache> cache_;
  std::unique_ptr<CoreProvisioner> prov_;
  std::unique_ptr<CoreUpdater> upd_;
  QString platform_;
  QString key(const QString& version, const QString& platform, const QString& core = QStringLiteral("melonds_ds")) const {
    return QStringLiteral("%1/%2/%3").arg(core, version, platform);
  }

  void addPackage(const QString& version, const QString& platform, const QByteArray& libAdvertised, const QByteArray& libServed,
                  const QString& core = QStringLiteral("melonds_ds")) {
    const auto file = [](const QString& name, const QString& role, const QByteArray& content) {
      return QJsonObject{{QStringLiteral("name"), name},
                         {QStringLiteral("role"), role},
                         {QStringLiteral("size"), content.size()},
                         {QStringLiteral("sha256"), shaOf(content)},
                         {QStringLiteral("available"), true}};
    };
    hub_->corePackages.insert(key(version, platform, core),
                              QJsonObject{{QStringLiteral("core_id"), core},
                                          {QStringLiteral("version"), version},
                                          {QStringLiteral("platform"), platform},
                                          {QStringLiteral("license"), QStringLiteral("GPL-3.0")},
                                          {QStringLiteral("source_url"), QStringLiteral("https://example.invalid/src")},
                                          {QStringLiteral("source_ref"), QStringLiteral("v") + version},
                                          {QStringLiteral("files"),
                                           QJsonArray{file(QStringLiteral("dummy_libretro.so"), QStringLiteral("library"), libAdvertised),
                                                      file(QStringLiteral("LICENSE.txt"), QStringLiteral("license"), kLicense)}}});
    hub_->coreFiles.insert(key(version, platform, core) + QStringLiteral("/dummy_libretro.so"), libServed);
    hub_->coreFiles.insert(key(version, platform, core) + QStringLiteral("/LICENSE.txt"), kLicense);
  }

  // The Hub's systems list with one core for one system.
  static QList<SystemInfo> hubSystems(const QString& coreId, const QString& version) {
    SystemInfo s;
    s.id = QStringLiteral("nds");
    s.defaultCoreId = coreId;
    SystemCore c;
    c.coreId = coreId;
    c.displayName = QStringLiteral("Test Core");
    c.version = version;
    s.cores.append(c);
    return {s};
  }

  // A core "used before": the package is in the cache (through the on-demand path).
  void cacheOld(const QString& version, const QString& core = QStringLiteral("melonds_ds")) {
    addPackage(version, platform_, kLib, kLib, core);
    QSignalSpy spy(prov_.get(), &CoreProvisioner::finished);
    prov_->prepare(core, version);
    QVERIFY(spy.wait(8000) || !spy.isEmpty());
    QVERIFY(spy.takeFirst().at(0).value<CoreResult>().ok);
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->filePath(QStringLiteral("data")));
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->features = {QStringLiteral("saves_v1"), QStringLiteral("firmware_v1"), QStringLiteral("cores_v1")};
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
    cache_ = std::make_unique<CoreCache>(profiles_->coreCacheDir());
    prov_ = std::make_unique<CoreProvisioner>(conn_.get(), cache_.get());
    upd_ = std::make_unique<CoreUpdater>(conn_.get(), cache_.get());
    platform_ = CoreCache::currentPlatform();
    QVERIFY(!platform_.isEmpty());
  }
  void cleanup() {
    upd_.reset();
    prov_.reset();
    cache_.reset();
    conn_.reset();
    hub_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  void updatesCachedCoreWithoutGameStart() {
    cacheOld(QStringLiteral("1.4.0"));
    addPackage(QStringLiteral("1.5.0"), platform_, kLib, kLib);
    const int before = hub_->coreFileDownloads;
    QSignalSpy spy(upd_.get(), &CoreUpdater::updated);
    upd_->check(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0")));
    QVERIFY(upd_->busy());
    QVERIFY(spy.wait(8000) || !spy.isEmpty());
    QVERIFY(!upd_->busy());
    QCOMPARE(upd_->lastUpdate().version, QStringLiteral("1.5.0"));
    QCOMPARE(hub_->coreFileDownloads - before, 2);
    QVERIFY(!cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0"), platform_).isEmpty());
    QCOMPARE(cache_->versions(QStringLiteral("melonds_ds"), platform_).first(), QStringLiteral("1.5.0"));
    // Up to date now: a second check downloads nothing.
    upd_->check(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0")));
    QVERIFY(!upd_->busy());
    QCOMPARE(hub_->coreFileDownloads - before, 2);
  }

  void neverCachedCoresAreNotFetched() {
    cacheOld(QStringLiteral("1.4.0"));
    addPackage(QStringLiteral("2.0.0"), platform_, kLib, kLib, QStringLiteral("desmume"));
    const auto systems = hubSystems(QStringLiteral("desmume"), QStringLiteral("2.0.0"));
    QVERIFY(upd_->candidates(systems).isEmpty());
    const int before = hub_->coreFileDownloads;
    upd_->check(systems);
    QVERIFY(!upd_->busy());
    QTest::qWait(200);
    QCOMPARE(hub_->coreFileDownloads, before);
    QVERIFY(cache_->versions(QStringLiteral("desmume"), platform_).isEmpty());
    // Older or equal Hub version: nothing to do either.
    QVERIFY(upd_->candidates(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.4.0"))).isEmpty());
    QVERIFY(upd_->candidates(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.3.0"))).isEmpty());
  }

  void runningCoreStaysAvailableWhileNewOneIsStaged() {
    cacheOld(QStringLiteral("1.3.0"));
    cacheOld(QStringLiteral("1.4.0"));
    addPackage(QStringLiteral("1.5.0"), platform_, kLib, kLib);
    // A game runs with 1.3.0: the update is staged and 1.3.0 is not pruned (keep-newest-two would remove it).
    upd_->setInUseProvider([](const QString& id) { return id == QLatin1String("melonds_ds") ? QStringLiteral("1.3.0") : QString(); });
    QSignalSpy spy(upd_.get(), &CoreUpdater::updated);
    upd_->check(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0")));
    QVERIFY(spy.wait(8000) || !spy.isEmpty());
    const QStringList v = cache_->versions(QStringLiteral("melonds_ds"), platform_);
    QCOMPARE(v, (QStringList{QStringLiteral("1.5.0"), QStringLiteral("1.4.0"), QStringLiteral("1.3.0")}));
    QVERIFY(!cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.3.0"), platform_).isEmpty());
    // The next start asks for the Hub's version and finds it complete in the cache.
    QVERIFY(!cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0"), platform_).isEmpty());
  }

  void failedDownloadRetriesLater() {
    cacheOld(QStringLiteral("1.4.0"));
    addPackage(QStringLiteral("1.5.0"), platform_, kLib, QByteArray(kLib.size(), 'z'));  // hash mismatch
    upd_->setRetryIntervalMs(150);
    QSignalSpy spy(upd_.get(), &CoreUpdater::updated);
    upd_->check(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0")));
    QTRY_COMPARE_WITH_TIMEOUT(upd_->failedCount(), 1, 8000);
    QVERIFY(spy.isEmpty());
    QVERIFY(cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0"), platform_).isEmpty());
    QCOMPARE(cache_->versions(QStringLiteral("melonds_ds"), platform_).first(), QStringLiteral("1.4.0"));  // old core intact
    // The Hub serves a good file now: the retry picks it up on its own.
    hub_->coreFiles.insert(key(QStringLiteral("1.5.0"), platform_) + QStringLiteral("/dummy_libretro.so"), kLib);
    QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 8000);
    QCOMPARE(upd_->failedCount(), 0);
    QVERIFY(!cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0"), platform_).isEmpty());
  }

  void gameStartTakesOverRunningDownload() {
    cacheOld(QStringLiteral("1.4.0"));
    addPackage(QStringLiteral("1.5.0"), platform_, kLib, kLib);
    upd_->check(hubSystems(QStringLiteral("melonds_ds"), QStringLiteral("1.5.0")));
    QVERIFY(upd_->busy());
    QSignalSpy spy(upd_.get(), &CoreUpdater::updated);
    upd_->cancelFor(QStringLiteral("melonds_ds"));
    QVERIFY(!upd_->busy());
    QTest::qWait(300);
    QVERIFY(spy.isEmpty());  // the aborted run reports nothing; the on-demand path downloads
  }
};

QTEST_GUILESS_MAIN(CoreUpdaterTest)
#include "coreupdater_test.moc"
