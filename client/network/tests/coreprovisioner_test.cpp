// CoreProvisioner against the fake Hub: success, not_on_hub, incompatible, not_cached_on_hub, invalid_hash/size, cache hit.
// Dummy bytes only (no real core).
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include <openssl/evp.h>

#include "corecache.h"
#include "coreprovisioner.h"
#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "profilestore.h"
#include "updatesig.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
const QByteArray kLib(2048, 'l');
const QByteArray kLicense(128, 'x');

// Ed25519 test key pair (generated per test run, never a real signing key).
struct TestKey {
  QByteArray pub;
  EVP_PKEY* pkey = nullptr;
  TestKey() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);
    size_t n = 32;
    pub.resize(32);
    EVP_PKEY_get_raw_public_key(pkey, reinterpret_cast<unsigned char*>(pub.data()), &n);
  }
  ~TestKey() { EVP_PKEY_free(pkey); }
  TestKey(const TestKey&) = delete;
  TestKey& operator=(const TestKey&) = delete;
  QByteArray signLine(const QByteArray& data) const {
    EVP_MD_CTX* c = EVP_MD_CTX_new();
    EVP_DigestSignInit(c, nullptr, nullptr, nullptr, pkey);
    size_t n = 64;
    QByteArray sig(64, 0);
    EVP_DigestSign(c, reinterpret_cast<unsigned char*>(sig.data()), &n, reinterpret_cast<const unsigned char*>(data.constData()),
                   static_cast<size_t>(data.size()));
    EVP_MD_CTX_free(c);
    return "ed25519 " + framebeam::update::keyId(pub).toLatin1() + " " + sig.toBase64() + "\n";
  }
};
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
  bool indexFeature_ = false;

  // Signed index (corepkg format, schema 1) listing exactly the packages registered on the fake Hub.
  QByteArray indexFor(const QString& version, const QString& platform, const QByteArray& lib = kLib, const QString& listedVersion = QString()) const {
    QJsonArray files;
    files.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("dummy_libretro.so")}, {QStringLiteral("role"), QStringLiteral("library")},
                             {QStringLiteral("size"), lib.size()}, {QStringLiteral("sha256"), shaOf(lib)}, {QStringLiteral("url"), QStringLiteral("https://example.invalid/l")}});
    files.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("LICENSE.txt")}, {QStringLiteral("role"), QStringLiteral("license")},
                             {QStringLiteral("size"), kLicense.size()}, {QStringLiteral("sha256"), shaOf(kLicense)}, {QStringLiteral("url"), QStringLiteral("https://example.invalid/x")}});
    const QJsonObject pkg{{QStringLiteral("core_id"), QStringLiteral("melonds_ds")},
                          {QStringLiteral("version"), listedVersion.isEmpty() ? version : listedVersion},
                          {QStringLiteral("platform"), platform},
                          {QStringLiteral("license"), QStringLiteral("GPL-3.0")},
                          {QStringLiteral("source_url"), QStringLiteral("https://example.invalid/src")},
                          {QStringLiteral("source_ref"), QStringLiteral("v") + version},
                          {QStringLiteral("origin"), QStringLiteral("test")},
                          {QStringLiteral("files"), files}};
    return QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1}, {QStringLiteral("generated_at"), QStringLiteral("2026-01-01T00:00:00Z")},
                                     {QStringLiteral("packages"), QJsonArray{pkg}}})
        .toJson(QJsonDocument::Compact);
  }
  // Reconnects to a Hub that advertises cores_index_v1.
  void withIndexFeature() {
    cleanup();
    indexFeature_ = true;
    init();
    QVERIFY(conn_->hubHasFeature(QStringLiteral("cores_index_v1")));
  }

  QString key(const QString& version, const QString& platform) const { return QStringLiteral("melonds_ds/%1/%2").arg(version, platform); }

  // Registers a package on the fake Hub. libAdvertised: content the metadata describes; served: what the file endpoint returns.
  void addPackage(const QString& version, const QString& platform, const QByteArray& libAdvertised, const QByteArray& libServed,
                  bool libAvailable = true) {
    const auto file = [](const QString& name, const QString& role, const QByteArray& content, bool available) {
      return QJsonObject{{QStringLiteral("name"), name},
                         {QStringLiteral("role"), role},
                         {QStringLiteral("size"), content.size()},
                         {QStringLiteral("sha256"), shaOf(content)},
                         {QStringLiteral("available"), available}};
    };
    hub_->corePackages.insert(key(version, platform),
                              QJsonObject{{QStringLiteral("core_id"), QStringLiteral("melonds_ds")},
                                          {QStringLiteral("version"), version},
                                          {QStringLiteral("platform"), platform},
                                          {QStringLiteral("license"), QStringLiteral("GPL-3.0")},
                                          {QStringLiteral("source_url"), QStringLiteral("https://example.invalid/src")},
                                          {QStringLiteral("source_ref"), QStringLiteral("v") + version},
                                          {QStringLiteral("files"),
                                           QJsonArray{file(QStringLiteral("dummy_libretro.so"), QStringLiteral("library"), libAdvertised, libAvailable),
                                                      file(QStringLiteral("LICENSE.txt"), QStringLiteral("license"), kLicense, true)}}});
    if (libAvailable) hub_->coreFiles.insert(key(version, platform) + QStringLiteral("/dummy_libretro.so"), libServed);
    hub_->coreFiles.insert(key(version, platform) + QStringLiteral("/LICENSE.txt"), kLicense);
  }

  CoreResult run(const QString& version) {
    QSignalSpy spy(prov_.get(), &CoreProvisioner::finished);
    prov_->prepare(QStringLiteral("melonds_ds"), version);
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
    if (indexFeature_) {
      hub_->features.append(QStringLiteral("cores_index_v1"));
    }
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
    indexFeature_ = false;
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

  // ---------------------------------------------------------------- signed core index (ADR 0012 D6)

  void noIndexFeatureKeepsOldBehavior() {
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY2(r.ok, qPrintable(r.problem + r.detail));
    QCOMPARE(hub_->coreIndexRequests, 0);
  }

  void goodIndexInstalls() {
    withIndexFeature();
    TestKey k;
    prov_->setTrustedKeys({k.pub});
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    hub_->coreIndex = indexFor(QStringLiteral("1.4.0"), platform_);
    hub_->coreIndexSig = k.signLine(hub_->coreIndex);
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY2(r.ok, qPrintable(r.problem + r.detail));
    QCOMPARE(hub_->coreIndexRequests, 2);
    QVERIFY(!r.libraryPath.isEmpty());
  }

  void badSignatureIsUntrustedAndNothingInstalled() {
    withIndexFeature();
    TestKey trusted, attacker;
    prov_->setTrustedKeys({trusted.pub});
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    hub_->coreIndex = indexFor(QStringLiteral("1.4.0"), platform_);
    hub_->coreIndexSig = attacker.signLine(hub_->coreIndex);  // untrusted key id
    CoreResult r = run(QStringLiteral("1.4.0"));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QStringLiteral("untrusted"));
    QCOMPARE(hub_->coreFileDownloads, 0);
    QVERIFY(cache_->libraryPath(QStringLiteral("melonds_ds"), QStringLiteral("1.4.0"), platform_).isEmpty());
    // Right key id but altered index bytes: signature mismatch
    hub_->coreIndexSig = trusted.signLine(hub_->coreIndex);
    hub_->coreIndex.replace("GPL-3.0", "GPL-2.0");
    r = run(QStringLiteral("1.4.0"));
    QCOMPARE(r.problem, QStringLiteral("untrusted"));
    QCOMPARE(hub_->coreFileDownloads, 0);
  }

  void hashMismatchAndMissingEntryAreUntrusted() {
    withIndexFeature();
    TestKey k;
    prov_->setTrustedKeys({k.pub});
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);
    // Signed index lists a different library hash than the package the Hub serves
    hub_->coreIndex = indexFor(QStringLiteral("1.4.0"), platform_, QByteArray(2048, 'z'));
    hub_->coreIndexSig = k.signLine(hub_->coreIndex);
    CoreResult r = run(QStringLiteral("1.4.0"));
    QCOMPARE(r.problem, QStringLiteral("untrusted"));
    QVERIFY(r.detail.contains(QStringLiteral("dummy_libretro.so")));
    // Index has no entry for this version
    hub_->coreIndex = indexFor(QStringLiteral("1.4.0"), platform_, kLib, QStringLiteral("1.3.0"));
    hub_->coreIndexSig = k.signLine(hub_->coreIndex);
    r = run(QStringLiteral("1.4.0"));
    QCOMPARE(r.problem, QStringLiteral("untrusted"));
    // Different size
    hub_->coreIndex = indexFor(QStringLiteral("1.4.0"), platform_, QByteArray(100, 'l'));
    hub_->coreIndexSig = k.signLine(hub_->coreIndex);
    r = run(QStringLiteral("1.4.0"));
    QCOMPARE(r.problem, QStringLiteral("untrusted"));
    QCOMPARE(hub_->coreFileDownloads, 0);
  }

  void hubWithoutVerifiedIndexIsUntrusted() {
    withIndexFeature();
    addPackage(QStringLiteral("1.4.0"), platform_, kLib, kLib);  // coreIndex stays empty: 404
    const CoreResult r = run(QStringLiteral("1.4.0"));
    QCOMPARE(r.problem, QStringLiteral("untrusted"));
    QCOMPARE(hub_->coreFileDownloads, 0);
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
