#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "firmwarecache.h"

using namespace framebeam;

namespace {
// Dummy bytes of the right size (not real firmware).
QByteArray dummy(int size, char fill) { return QByteArray(size, fill); }
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
}  // namespace

class FirmwareCacheTest : public QObject {
  Q_OBJECT
 private slots:
  void validation() {
    QVERIFY(FirmwareCache::isValidSystemId(QStringLiteral("nds")));
    QVERIFY(!FirmwareCache::isValidSystemId(QStringLiteral("../nds")));
    QVERIFY(!FirmwareCache::isValidSystemId(QString()));
    QVERIFY(FirmwareCache::isValidSha256(shaOf("a")));
    QVERIFY(!FirmwareCache::isValidSha256(shaOf("a").toUpper()));
    FirmwareCache c(QStringLiteral("/tmp/x"));
    QVERIFY(c.path(QStringLiteral("nds"), QStringLiteral("../../etc")).isEmpty());
    QVERIFY(c.path(QStringLiteral("a/b"), shaOf("a")).isEmpty());
  }

  void storeAndLookup() {
    QTemporaryDir dir;
    FirmwareCache c(dir.path());
    const QByteArray d = dummy(4096, 'a');
    const QString sha = shaOf(d);
    QVERIFY(!c.probe(QStringLiteral("nds"), sha, d.size()));
    QVERIFY(!c.lookup(QStringLiteral("nds"), sha, d.size()));
    QCOMPARE(c.store(QStringLiteral("nds"), sha, d.size(), d), FirmwareCache::StoreResult::Ok);
    QVERIFY(c.path(QStringLiteral("nds"), sha).endsWith(QLatin1String("nds/") + sha));
    QVERIFY(c.probe(QStringLiteral("nds"), sha, d.size()));
    QString path;
    QVERIFY(c.lookup(QStringLiteral("nds"), sha, d.size(), &path));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), d);
    // Same hash in another system is a separate entry.
    QVERIFY(!c.probe(QStringLiteral("gba"), sha, d.size()));
  }

  void rejectsWrongSizeAndHash() {
    QTemporaryDir dir;
    FirmwareCache c(dir.path());
    const QByteArray d = dummy(4096, 'a');
    QCOMPARE(c.store(QStringLiteral("nds"), shaOf(d), d.size() + 1, d), FirmwareCache::StoreResult::SizeMismatch);
    QCOMPARE(c.store(QStringLiteral("nds"), shaOf(dummy(4096, 'b')), d.size(), d), FirmwareCache::StoreResult::HashMismatch);
    QCOMPARE(c.store(QStringLiteral("nds"), QStringLiteral("zz"), d.size(), d), FirmwareCache::StoreResult::InvalidArgument);
    QVERIFY(!c.probe(QStringLiteral("nds"), shaOf(d), d.size()));  // nothing was written
  }

  void corruptedEntryIsRemoved() {
    QTemporaryDir dir;
    FirmwareCache c(dir.path());
    const QByteArray d = dummy(4096, 'a');
    const QString sha = shaOf(d);
    QCOMPARE(c.store(QStringLiteral("nds"), sha, d.size(), d), FirmwareCache::StoreResult::Ok);
    QFile f(c.path(QStringLiteral("nds"), sha));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(dummy(4096, 'z'));  // same size, other content
    f.close();
    QVERIFY(c.probe(QStringLiteral("nds"), sha, d.size()));  // probe does not hash
    QVERIFY(!c.lookup(QStringLiteral("nds"), sha, d.size()));
    QVERIFY(!QFile::exists(c.path(QStringLiteral("nds"), sha)));
  }
};

QTEST_GUILESS_MAIN(FirmwareCacheTest)
#include "firmwarecache_test.moc"
