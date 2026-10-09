// CoreProvisioner against the fake Hub: success, not_on_hub, incompatible, not_cached_on_hub, invalid_hash/size, cache hit.
// Dummy bytes only (no real core).
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "corecache.h"
#include "coreprovisioner.h"
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

class CoreProvisionerTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<CoreCache> cache_;
  std::unique_ptr<CoreProvisioner> prov_;
  QString platform_;
  QString key(const QString& version, const QString& platform, const QString& core = QStringLiteral("melonds_ds")) const {
    return QStringLiteral("%1/%2/%3").arg(core, version, platform);
  }

  // Registers a package on the fake Hub. libAdvertised: content the metadata describes; served: what the file endpoint returns.
  void addPackage(const QString& version, const QString& platform, const QByteArray& libAdvertised, const QByteArray& libServed,
                  bool libAvailable = true, const QString& core = QStringLiteral("melonds_ds")) {
    const auto file = [](const QString& name, const QString& role, const QByteArray& content, bool available) {
      return QJsonObject{{QStringLiteral("name"), name},
                         {QStringLiteral("role"), role},
                         {QStringLiteral("size"), content.size()},
                         {QStringLiteral("sha256"), shaOf(content)},
                         {QStringLiteral("available"), available}};
    };
    hub_->corePackages.insert(key(version, platform, core),
                              QJsonObject{{QStringLiteral("core_id"), core},
                                          {QStringLiteral("version"), version},
                                          {QStringLiteral("platform"), platform},
                                          {QStringLiteral("license"), QStringLiteral("GPL-3.0")},
                                          {QStringLiteral("source_url"), QStringLiteral("https://example.invalid/src")},
                                          {QStringLiteral("source_ref"), QStringLiteral("v") + version},
                                          {QStringLiteral("files"),
                                           QJsonArray{file(QStringLiteral("dummy_libretro.so"), QStringLiteral("library"), libAdvertised, libAvailable),
                                                      file(QStringLiteral("LICENSE.txt"), QStringLiteral("license"), kLicense, true)}}});
    if (libAvailable) hub_->coreFiles.insert(key(version, platform, core) + QStringLiteral("/dummy_libretro.so"), libServed);
    hub_->coreFiles.insert(key(version, platform, core) + QStringLiteral("/LICENSE.txt"), kLicense);
  }

  CoreResult run(const QString& version, const QString& core = QStringLiteral("melonds_ds")) {
    QSignalSpy spy(prov_.get(), &CoreProvisioner::finished);
    prov_->prepare(core, version);
    if (!spy.wait(8000) && spy.isEmpty()) {
      QTest::qFail("no result", __FILE__, __LINE__);
      return {};
    }
    return spy.takeFirst().at(0).value<CoreResult>();
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
    QVERIFY(conn_->hubHasFeature(QStringLiteral("cores_v1")));
    cache_ = std::make_unique<CoreCache>(profiles_->coreCacheDir());
    prov_ = std::make_unique<CoreProvisioner>(conn_.get(), cache_.get());
    platform_ = CoreCache::currentPlatform();
    QVERIFY(!platform_.isEmpty());
  }
  void cleanup() {
    prov_.reset();
    cache_.reset();
    conn_.reset();
    hub_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  void downloadsAndCaches() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY2(r.ok, qPrintable(r.problem + r.detail));
    QVERIFY(r.downloaded);
    QCOMPARE(hub_->coreFileDownloads, 2);
    QVERIFY(r.libraryPath.contains(QLatin1String("/cache/cores/melonds_ds/1.4.0/") + platform_));
    QFile f(r.libraryPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), kLib);
    QCOMPARE(cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.4.0"), platform_), r.libraryPath);
    QVERIFY(QFileInfo::exists(QFileInfo(r.libraryPath).absolutePath() + QStringLiteral("/LICENSE.txt")));
    QVERIFY(QFileInfo::exists(QFileInfo(r.libraryPath).absolutePath() + QStringLiteral("/package.json")));
    // Request went to the platform of this build.
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/cores/melonds_ds/packages/1.4.0/") + platform_, "GET"), 3);  // metadata + 2 files
  }

  void secondPrepareDoesNotDownload() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    QVERIFY(run(QStringLiteral("1.4.0")).ok);
    QCOMPARE(hub_->coreFileDownloads, 2);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(r.ok);
    QVERIFY(!r.downloaded);
    QCOMPARE(hub_->coreFileDownloads, 2);
  }

  void redownloadsOnlyInvalidFile() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    const CoreResult first = run(QStringLiteral("1.4.0"));
    QVERIFY(first.ok);
    {
      QFile f(first.libraryPath);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray(kLib.size(), 'z'));  // corrupted, same size
    }
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(r.ok);
    QVERIFY(r.downloaded);
    QCOMPARE(hub_->coreFileDownloads, 3);  // 2 + the library again, not the license
    QFile f(r.libraryPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), kLib);
  }

  void packageNotFoundIsNotOnHub() {
    const CoreResult r = run(QStringLiteral("9.9.9"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("not_on_hub"));
    QVERIFY(r.libraryPath.isEmpty());
    QCOMPARE(hub_->coreFileDownloads, 0);
  }

  void otherPlatformOnlyIsIncompatible() {
    const QString other = platform_ == QLatin1String("windows-x64") ? QStringLiteral("macos-arm64") : QStringLiteral("windows-x64");
    addPackage(QStringLiteral("1.4.0"), other, kLib, kLib);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("incompatible"));
    QCOMPARE(hub_->coreFileDownloads, 0);
  }

  void fileNotAvailableFlagIsNotCachedOnHub() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib, false);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("not_cached_on_hub"));
    QVERIFY(cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.4.0"), platform_).isEmpty());
  }

  void file404IsNotCachedOnHub() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    hub_->coreFiles.remove(key(QStringLiteral("1.4.0"), platform_) + QStringLiteral("/dummy_libretro.so"));  // metadata says available, file is gone
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("not_cached_on_hub"));
  }

  void wrongHashIsRejectedAndNotCached() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, QByteArray(kLib.size(), 'm'));  // same size, other content
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("invalid_hash"));
    QVERIFY(r.libraryPath.isEmpty());
    QVERIFY(cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.4.0"), platform_).isEmpty());
    QVERIFY(!QFileInfo::exists(cache_->filePath(QStringLiteral("melonds_ds"), QStringLiteral("1.4.0"), platform_, QStringLiteral("dummy_libretro.so"))));
    QVERIFY(cache_->versions(QStringLiteral("melonds_ds"), platform_).isEmpty());
  }

  void wrongSizeIsRejected() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib.left(100));
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("invalid_size"));
    QVERIFY(cache_->versions(QStringLiteral("melonds_ds"), platform_).isEmpty());
  }

  void invalidMetadataFails() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    QJsonObject o = hub_->corePackages.value(key(QStringLiteral("1.4.0"), platform_));
    o.insert(QStringLiteral("files"), QJsonArray());  // no library
    hub_->corePackages.insert(key(QStringLiteral("1.4.0"), platform_), o);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("download_failed"));
  }

  // ---------------------------------------------------------------- no core index check (ADR 0020 D6)

  void installsWithoutAnyIndexOrSignature() {
    // Size and SHA-256 of the Hub metadata are the only checks; no index or signature is requested.
    addPackage(QStringLiteral("2026.10.09"), platform_, kLib, kLib);
    const CoreResult r = run(QStringLiteral("2026.10.09"));
    QVERIFY2(r.ok, qPrintable(r.problem + r.detail));
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/cores/index"), "GET"), 0);
    QVERIFY(!r.libraryPath.isEmpty());
  }

  void coreIdWithDashProvisions() {
    const QString core = QStringLiteral("my-core_2");
    addPackage(QStringLiteral("2026.10.09.2"), platform_, kLib, kLib, true, core);
    const CoreResult r = run(QStringLiteral("2026.10.09.2"), core);
    QVERIFY2(r.ok, qPrintable(r.problem + r.detail));
    QCOMPARE(r.coreId, core);
    QVERIFY(r.libraryPath.contains(QLatin1String("/cache/cores/my-core_2/2026.10.09.2/") + platform_));
    QCOMPARE(cache_->libraryPath(core, QStringLiteral("2026.10.09.2"), platform_), r.libraryPath);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/cores/my-core_2/packages/2026.10.09.2/") + platform_, "GET"), 3);
    // Invalid ids never reach the network.
    const int before = hub_->corePackageRequests;
    QCOMPARE(run(QStringLiteral("1.0.0"), QStringLiteral("-bad")).problem, QStringLiteral("not_on_hub"));
    QCOMPARE(run(QStringLiteral("1.0.0"), QStringLiteral("../x")).problem, QStringLiteral("not_on_hub"));
    QCOMPARE(hub_->corePackageRequests, before);
  }

  void twoCoresOfOneSystemCoexistInTheCache() {
    addPackage(QStringLiteral("2026.10.09"), platform_, kLib, kLib, true, QStringLiteral("melondsds"));
    addPackage(QStringLiteral("2026.10.08"), platform_, kLib, kLib, true, QStringLiteral("desmume"));
    QVERIFY(run(QStringLiteral("2026.10.09"), QStringLiteral("melondsds")).ok);
    QVERIFY(run(QStringLiteral("2026.10.08"), QStringLiteral("desmume")).ok);
    QCOMPARE(cache_->versions(QStringLiteral("melondsds"), platform_), QStringList{QStringLiteral("2026.10.09")});
    QCOMPARE(cache_->versions(QStringLiteral("desmume"), platform_), QStringList{QStringLiteral("2026.10.08")});
  }

  void parsesSystemCoresV2() {
    const QJsonObject o{
        {QStringLiteral("id"), QStringLiteral("nds")},
        {QStringLiteral("preferred_core_id"), QStringLiteral("melondsds")},
        {QStringLiteral("core_package_version"), QStringLiteral("2026.10.09")},
        {QStringLiteral("default_core_id"), QStringLiteral("melondsds")},
        {QStringLiteral("cores"),
         QJsonArray{QJsonObject{{QStringLiteral("core_id"), QStringLiteral("melondsds")},
                                {QStringLiteral("display_name"), QStringLiteral("Nintendo DS (melonDS DS)")},
                                {QStringLiteral("version"), QStringLiteral("2026.10.09")},
                                {QStringLiteral("license"), QStringLiteral("GPLv3")},
                                {QStringLiteral("experimental"), false},
                                {QStringLiteral("required_hw_api"), QStringLiteral("OpenGL Core >= 3.2")},
                                {QStringLiteral("origin"), QStringLiteral("libretro-buildbot")},
                                {QStringLiteral("build_date"), QStringLiteral("2026-10-09")}},
                    QJsonObject{{QStringLiteral("core_id"), QStringLiteral("desmume")},
                                {QStringLiteral("version"), QStringLiteral("2026.10.08")},
                                {QStringLiteral("experimental"), true},
                                {QStringLiteral("required_hw_api"), QJsonValue::Null}},
                    QJsonObject{{QStringLiteral("core_id"), QStringLiteral("Bad Id")}, {QStringLiteral("version"), QStringLiteral("1")}},
                    QJsonObject{{QStringLiteral("core_id"), QStringLiteral("novers")}}}}};
    const auto s = parseSystemInfo(o);
    QVERIFY(s.has_value());
    QCOMPARE(s->defaultCoreId, QStringLiteral("melondsds"));
    QCOMPARE(s->cores.size(), 2);  // invalid entries are dropped
    QCOMPARE(s->core(QStringLiteral("melondsds"))->requiredHwApi, QStringLiteral("OpenGL Core >= 3.2"));
    QCOMPARE(s->core(QStringLiteral("melondsds"))->buildDate, QStringLiteral("2026-10-09"));
    QVERIFY(s->core(QStringLiteral("desmume"))->experimental);
    QVERIFY(s->core(QStringLiteral("desmume"))->requiredHwApi.isEmpty());
    QCOMPARE(s->defaultCore().coreId, QStringLiteral("melondsds"));
    QCOMPARE(s->defaultCore().version, QStringLiteral("2026.10.09"));
  }

  void legacyHubWithoutCoresV2UsesPreferredCore() {
    const auto s = parseSystemInfo(QJsonObject{{QStringLiteral("id"), QStringLiteral("nds")},
                                               {QStringLiteral("preferred_core_id"), QStringLiteral("melonds_ds")},
                                               {QStringLiteral("core_package_version"), QStringLiteral("1.4.0")}});
    QVERIFY(s.has_value());
    QVERIFY(s->cores.isEmpty());
    QCOMPARE(s->defaultCore().coreId, QStringLiteral("melonds_ds"));
    QCOMPARE(s->defaultCore().version, QStringLiteral("1.4.0"));
    const auto none = parseSystemInfo(QJsonObject{{QStringLiteral("id"), QStringLiteral("nds")}});
    QVERIFY(none->defaultCore().coreId.isEmpty());
  }

  void parsesCorePackageVersionOfSystems() {
    const auto s = parseSystemInfo(QJsonObject{{QStringLiteral("id"), QStringLiteral("nds")},
                                               {QStringLiteral("core_package_version"), QStringLiteral("1.4.0")}});
    QVERIFY(s.has_value());
    QCOMPARE(s->corePackageVersion, QStringLiteral("1.4.0"));
    const auto n = parseSystemInfo(QJsonObject{{QStringLiteral("id"), QStringLiteral("nds")}, {QStringLiteral("core_package_version"), QJsonValue::Null}});
    QVERIFY(n.has_value());
    QVERIFY(n->corePackageVersion.isEmpty());
  }
};

QTEST_GUILESS_MAIN(CoreProvisionerTest)
#include "coreprovisioner_test.moc"
