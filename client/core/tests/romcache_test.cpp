#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "romcache.h"

using namespace framebeam;

namespace {
QByteArray dummyRom() { return QByteArray(5000, 'x') + "FRAMEBEAM-DUMMY"; }
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
void writeFile(const QString& path, const QByteArray& d) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(d);
}
}  // namespace

class RomCacheTest : public QObject {
  Q_OBJECT
 private slots:
  void extensionAndHashValidation() {
    QCOMPARE(RomCache::extensionFromFilename(QStringLiteral("Demo.NDS")), QStringLiteral("nds"));
    QCOMPARE(RomCache::extensionFromFilename(QStringLiteral("noext")), QStringLiteral("bin"));
    QCOMPARE(RomCache::extensionFromFilename(QStringLiteral("a.b/../x")), QStringLiteral("bin"));
    QVERIFY(RomCache::isValidSha256(shaOf("a")));
    QVERIFY(!RomCache::isValidSha256(QStringLiteral("../etc")));
    QVERIFY(!RomCache::isValidSha256(shaOf("a").toUpper()));
  }

  void commitHitAndSidecar() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QByteArray rom = dummyRom();
    const QString sha = shaOf(rom);
    writeFile(c.partPath(sha, QStringLiteral("nds")), rom);
    QCOMPARE(c.partSize(sha, QStringLiteral("nds")), qint64(rom.size()));
    QVERIFY(c.verifyAndCommit(sha, QStringLiteral("nds")) == RomCache::CommitResult::Ok);
    QVERIFY(!QFile::exists(c.partPath(sha, QStringLiteral("nds"))));
    QString path;
    QVERIFY(c.lookup(sha, QStringLiteral("nds"), rom.size(), &path));
    QVERIFY(QFile::exists(path + QStringLiteral(".ok")));
    QVERIFY(!c.lookup(sha, QStringLiteral("nds"), rom.size() + 1));  // falsche Groesse: verworfen
    QVERIFY(!QFile::exists(path));
  }

  void mismatchDeletesPart() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString sha = shaOf(dummyRom());
    writeFile(c.partPath(sha, QStringLiteral("nds")), QByteArray("falsch"));
    QVERIFY(c.verifyAndCommit(sha, QStringLiteral("nds")) == RomCache::CommitResult::HashMismatch);
    QVERIFY(!QFile::exists(c.partPath(sha, QStringLiteral("nds"))));
    QVERIFY(!QFile::exists(c.finalPath(sha, QStringLiteral("nds"))));
  }

  void corruptedCacheFileIsRejected() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QByteArray rom = dummyRom();
    const QString sha = shaOf(rom);
    writeFile(c.finalPath(sha, QStringLiteral("nds")), QByteArray(rom.size(), 'y'));  // gleiche Groesse, falscher Inhalt
    QVERIFY(!c.lookup(sha, QStringLiteral("nds"), rom.size()));
    QVERIFY(!QFile::exists(c.finalPath(sha, QStringLiteral("nds"))));
  }
};

QTEST_GUILESS_MAIN(RomCacheTest)
#include "romcache_test.moc"
