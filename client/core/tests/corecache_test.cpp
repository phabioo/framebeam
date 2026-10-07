// CoreCache: insert (temp + size + sha256 + rename), package.json, libraryPath only for valid files, versions().
// Dummy bytes only (no real core).
#include <QCryptographicHash>
#include <QJsonArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "corecache.h"

using namespace framebeam;

namespace {
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }

CorePackageInfo makePackage(const QString& version, const QByteArray& lib, const QByteArray& lic) {
  CorePackageInfo p;
  p.coreId = QStringLiteral("melonds_ds");
  p.version = version;
  p.platform = QStringLiteral("linux-x64");
  p.license = QStringLiteral("GPL-3.0");
  p.sourceUrl = QStringLiteral("https://example.invalid/src");
  p.sourceRef = QStringLiteral("v") + version;
  p.files.append({QStringLiteral("dummy_libretro.so"), QStringLiteral("library"), lib.size(), shaOf(lib), true});
  p.files.append({QStringLiteral("LICENSE.txt"), QStringLiteral("license"), lic.size(), shaOf(lic), true});
  return p;
}
}  // namespace

class CoreCacheTest : public QObject {
  Q_OBJECT
 private slots:
  void identifiersAndPlatform() {
    QVERIFY(isValidCoreId(QStringLiteral("melonds_ds")));
    QVERIFY(!isValidCoreId(QStringLiteral("../x")));
    QVERIFY(isValidCoreVersion(QStringLiteral("1.4.0")));
    QVERIFY(!isValidCoreVersion(QStringLiteral(".1")));
    QVERIFY(isValidCorePlatform(QStringLiteral("windows-x64")));
    QVERIFY(!isValidCorePlatform(QStringLiteral("windows")));
    QVERIFY(!isValidCoreFileName(QStringLiteral("a/b")));
    QVERIFY(!isValidCoreFileName(QStringLiteral("a..b")));
    QVERIFY(isValidCorePlatform(CoreCache::currentPlatform()));
    CoreCache c(QStringLiteral("/tmp/x"));
    QVERIFY(c.packageDir(QStringLiteral("../a"), QStringLiteral("1"), QStringLiteral("linux-x64")).isEmpty());
    QVERIFY(c.filePath(QStringLiteral("a"), QStringLiteral("1"), QStringLiteral("linux-x64"), QStringLiteral("../x")).isEmpty());
  }

  void versionOrder() {
    QVERIFY(CoreCache::compareVersions(QStringLiteral("1.10.0"), QStringLiteral("1.9.5")) > 0);
    QVERIFY(CoreCache::compareVersions(QStringLiteral("1.4"), QStringLiteral("1.4.0")) < 0);
    QCOMPARE(CoreCache::compareVersions(QStringLiteral("2.0"), QStringLiteral("2.0")), 0);
  }

  void parsePackage() {
    const QByteArray lib(100, 'l');
    const QByteArray lic(10, 'x');
    const CorePackageInfo p = makePackage(QStringLiteral("1.4.0"), lib, lic);
    const auto parsed = parseCorePackage(p.toJson());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->files.size(), 2);
    QVERIFY(parsed->library() != nullptr);
    // Two libraries / bad hash / bad name are rejected.
    QJsonObject o = p.toJson();
    QJsonArray files = o.value(QStringLiteral("files")).toArray();
    QJsonObject f1 = files.at(1).toObject();
    f1.insert(QStringLiteral("role"), QStringLiteral("library"));
    files.replace(1, f1);
    o.insert(QStringLiteral("files"), files);
    QVERIFY(!parseCorePackage(o).has_value());
    o = p.toJson();
    files = o.value(QStringLiteral("files")).toArray();
    QJsonObject f0 = files.at(0).toObject();
    f0.insert(QStringLiteral("name"), QStringLiteral("../evil.so"));
    files.replace(0, f0);
    o.insert(QStringLiteral("files"), files);
    QVERIFY(!parseCorePackage(o).has_value());
  }

  void storeAndLibraryPath() {
    QTemporaryDir dir;
    CoreCache c(dir.path());
    const QByteArray lib(4096, 'l');
    const QByteArray lic(64, 'x');
    const CorePackageInfo p = makePackage(QStringLiteral("1.4.0"), lib, lic);
    QVERIFY(c.libraryPath(p.coreId, p.version, p.platform).isEmpty());
    QVERIFY(!c.fileValid(p, p.files.at(0)));
    QCOMPARE(c.store(p, p.files.at(0), lib), CoreCache::StoreResult::Ok);
    QCOMPARE(c.store(p, p.files.at(1), lic), CoreCache::StoreResult::Ok);
    QVERIFY(c.fileValid(p, p.files.at(0)));
    QVERIFY(c.libraryPath(p.coreId, p.version, p.platform).isEmpty());  // no package.json yet
    QVERIFY(c.writePackage(p));
    const QString path = c.libraryPath(p.coreId, p.version, p.platform);
    QVERIFY(path.endsWith(QLatin1String("melonds_ds/1.4.0/linux-x64/dummy_libretro.so")));
    QVERIFY(QFileInfo(path).isFile());
    QVERIFY(c.readPackage(p.coreId, p.version, p.platform).has_value());
    QCOMPARE(c.versions(p.coreId, p.platform), QStringList{QStringLiteral("1.4.0")});
    QVERIFY(c.versions(p.coreId, QStringLiteral("windows-x64")).isEmpty());
    // No temp files left behind.
    QCOMPARE(QDir(QFileInfo(path).absolutePath()).entryList(QDir::Files).size(), 3);  // library, license, package.json
  }

  void rejectsWrongSizeOrHash() {
    QTemporaryDir dir;
    CoreCache c(dir.path());
    const QByteArray lib(256, 'l');
    const CorePackageInfo p = makePackage(QStringLiteral("1.0.0"), lib, QByteArray(8, 'x'));
    QCOMPARE(c.store(p, p.files.at(0), QByteArray(255, 'l')), CoreCache::StoreResult::SizeMismatch);
    QCOMPARE(c.store(p, p.files.at(0), QByteArray(256, 'm')), CoreCache::StoreResult::HashMismatch);
    QVERIFY(!QFileInfo::exists(c.filePath(p.coreId, p.version, p.platform, p.files.at(0).name)));
    QVERIFY(c.versions(p.coreId, p.platform).isEmpty());
  }

  void corruptedFileRejected() {
    QTemporaryDir dir;
    CoreCache c(dir.path());
    const QByteArray lib(512, 'l');
    const CorePackageInfo p = makePackage(QStringLiteral("1.0.0"), lib, QByteArray(8, 'x'));
    QCOMPARE(c.store(p, p.files.at(0), lib), CoreCache::StoreResult::Ok);
    QCOMPARE(c.store(p, p.files.at(1), QByteArray(8, 'x')), CoreCache::StoreResult::Ok);
    QVERIFY(c.writePackage(p));
    QVERIFY(!c.libraryPath(p.coreId, p.version, p.platform).isEmpty());
    // Same size, different content (e.g. bit rot or tampering).
    const QString path = c.filePath(p.coreId, p.version, p.platform, p.files.at(0).name);
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray(512, 'z'));
    }
    QVERIFY(!c.fileValid(p, p.files.at(0)));
    QVERIFY(c.libraryPath(p.coreId, p.version, p.platform).isEmpty());
    // Truncated file.
    {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray(10, 'l'));
    }
    QVERIFY(c.libraryPath(p.coreId, p.version, p.platform).isEmpty());
    // Re-inserting a good file repairs the cache.
    QCOMPARE(c.store(p, p.files.at(0), lib), CoreCache::StoreResult::Ok);
    QVERIFY(!c.libraryPath(p.coreId, p.version, p.platform).isEmpty());
  }

  void versionsNewestFirst() {
    QTemporaryDir dir;
    CoreCache c(dir.path());
    for (const char* v : {"1.9.0", "1.10.0", "1.4.0"}) {
      const CorePackageInfo p = makePackage(QString::fromLatin1(v), QByteArray(16, 'l'), QByteArray(4, 'x'));
      QVERIFY(c.writePackage(p));
    }
    QCOMPARE(c.versions(QStringLiteral("melonds_ds"), QStringLiteral("linux-x64")),
             (QStringList{QStringLiteral("1.10.0"), QStringLiteral("1.9.0"), QStringLiteral("1.4.0")}));
  }
};

QTEST_GUILESS_MAIN(CoreCacheTest)
#include "corecache_test.moc"
