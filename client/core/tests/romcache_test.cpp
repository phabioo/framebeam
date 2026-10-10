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
    QVERIFY(!c.lookup(sha, QStringLiteral("nds"), rom.size() + 1));  // wrong size: discarded
    QVERIFY(!QFile::exists(path));
  }

  void mismatchDeletesPart() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString sha = shaOf(dummyRom());
    writeFile(c.partPath(sha, QStringLiteral("nds")), QByteArray("wrong"));
    QVERIFY(c.verifyAndCommit(sha, QStringLiteral("nds")) == RomCache::CommitResult::HashMismatch);
    QVERIFY(!QFile::exists(c.partPath(sha, QStringLiteral("nds"))));
    QVERIFY(!QFile::exists(c.finalPath(sha, QStringLiteral("nds"))));
  }

  void corruptedCacheFileIsRejected() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QByteArray rom = dummyRom();
    const QString sha = shaOf(rom);
    writeFile(c.finalPath(sha, QStringLiteral("nds")), QByteArray(rom.size(), 'y'));  // same size, wrong content
    QVERIFY(!c.lookup(sha, QStringLiteral("nds"), rom.size()));
    QVERIFY(!QFile::exists(c.finalPath(sha, QStringLiteral("nds"))));
  }

  // ---- Size limit / LRU ----
 private:
  // Adds a finished, verified file of `size` bytes (dummy content) and returns its hash.
  static QString addRom(RomCache& c, char fill, int size, qint64 lastUsedMs, const QString& ext = QStringLiteral("3ds")) {
    const QByteArray d(size, fill);
    const QString sha = shaOf(d);
    writeFile(c.partPath(sha, ext), d);
    if (c.verifyAndCommit(sha, ext) != RomCache::CommitResult::Ok) return {};
    c.touch(sha, ext, lastUsedMs);
    return sha;
  }

 private slots:
  void entriesAreSortedLeastRecentlyUsedFirst() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString a = addRom(c, 'a', 1000, 3000);
    const QString b = addRom(c, 'b', 1000, 1000);
    const QString d = addRom(c, 'c', 1000, 2000);
    const auto list = c.entries();
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0).sha256, b);
    QCOMPARE(list.at(1).sha256, d);
    QCOMPARE(list.at(2).sha256, a);
    QCOMPARE(c.totalSize(), qint64(3000));
    writeFile(c.partPath(shaOf("x"), QStringLiteral("nds")), QByteArray(500, 'p'));  // .part files are not entries
    QCOMPARE(c.entries().size(), 3);
  }

  void touchMovesAFileToTheEnd() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString a = addRom(c, 'a', 1000, 1000);
    const QString b = addRom(c, 'b', 1000, 2000);
    QCOMPARE(c.entries().first().sha256, a);
    c.touch(a, QStringLiteral("3ds"), 5000);
    QCOMPARE(c.entries().first().sha256, b);
    QCOMPARE(c.entries().last().lastUsedMs, qint64(5000));
    // The record still validates the file without hashing again.
    QVERIFY(c.probe(a, QStringLiteral("3ds"), 1000) == RomCache::Probe::Valid);
    c.touch(shaOf("missing"), QStringLiteral("3ds"));  // missing file: ignored
  }

  void evictsOldestFirstUntilTheLimitFits() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString a = addRom(c, 'a', 1000, 4000);
    const QString b = addRom(c, 'b', 1000, 1000);
    const QString d = addRom(c, 'c', 1000, 2000);
    const QString e = addRom(c, 'd', 1000, 3000);
    const auto r = c.trimToLimit(2500);  // 4000 -> must drop two files: b (1000), d (2000)
    QCOMPARE(r.removedFiles, 2);
    QCOMPARE(r.freedBytes, qint64(2000));
    QCOMPARE(r.remainingBytes, qint64(2000));
    QVERIFY(!QFile::exists(c.finalPath(b, QStringLiteral("3ds"))));
    QVERIFY(!QFile::exists(c.finalPath(b, QStringLiteral("3ds")) + QStringLiteral(".ok")));
    QVERIFY(!QFile::exists(c.finalPath(d, QStringLiteral("3ds"))));
    QVERIFY(QFile::exists(c.finalPath(e, QStringLiteral("3ds"))));
    QVERIFY(QFile::exists(c.finalPath(a, QStringLiteral("3ds"))));
    // Already within the limit, or unlimited (0): nothing happens.
    QCOMPARE(c.trimToLimit(2500).removedFiles, 0);
    QCOMPARE(c.trimToLimit(0).removedFiles, 0);
    QCOMPARE(c.entries().size(), 2);
  }

  void runningGameAndDownloadsAreNeverEvicted() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString oldRunning = addRom(c, 'a', 1000, 1000);  // oldest, but the game running now
    const QString mid = addRom(c, 'b', 1000, 2000);
    const QString downloading = addRom(c, 'c', 1000, 3000);
    writeFile(c.partPath(downloading, QStringLiteral("3ds")), QByteArray(10, 'p'));  // a new download of this ROM is under way
    const QString newest = addRom(c, 'd', 1000, 4000);
    // Limit 1000 would remove everything; the protected ones stay, the rest goes in LRU order.
    const auto r = c.trimToLimit(1000, {oldRunning});
    QVERIFY(QFile::exists(c.finalPath(oldRunning, QStringLiteral("3ds"))));
    QVERIFY(QFile::exists(c.finalPath(downloading, QStringLiteral("3ds"))));  // .part next to it
    QVERIFY(!QFile::exists(c.finalPath(mid, QStringLiteral("3ds"))));
    QVERIFY(!QFile::exists(c.finalPath(newest, QStringLiteral("3ds"))));
    QCOMPARE(r.removedFiles, 2);
    QCOMPARE(r.remainingBytes, qint64(2000));  // still above the limit: nothing protected is touched
    QVERIFY(QFile::exists(c.partPath(downloading, QStringLiteral("3ds"))));
  }

  void clearRemovesEverythingExceptProtected() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QString running = addRom(c, 'a', 1000, 1000);
    addRom(c, 'b', 2000, 2000);
    addRom(c, 'c', 3000, 3000, QStringLiteral("nds"));
    QCOMPARE(c.clearableSize({running}), qint64(5000));
    QCOMPARE(c.clearableSize(), qint64(6000));
    const auto r = c.clear({running});
    QCOMPARE(r.freedBytes, qint64(5000));  // what the confirmation showed
    QCOMPARE(r.removedFiles, 2);
    QCOMPARE(r.remainingBytes, qint64(1000));
    QCOMPARE(c.entries().size(), 1);
    QCOMPARE(c.entries().first().sha256, running);
    QCOMPARE(c.clear().freedBytes, qint64(1000));
    QCOMPARE(c.entries().size(), 0);
  }

  void filesWithoutRecordCountAsUsedAtTheirModificationTime() {
    QTemporaryDir dir;
    RomCache c(dir.path());
    const QByteArray d(100, 'z');
    writeFile(c.finalPath(shaOf(d), QStringLiteral("3ds")), d);  // placed by hand, no sidecar
    QCOMPARE(c.entries().size(), 1);
    QVERIFY(c.entries().first().lastUsedMs > 0);
    writeFile(dir.path() + QStringLiteral("/notes.txt"), "x");  // foreign files are ignored
    QCOMPARE(c.entries().size(), 1);
  }
};

QTEST_GUILESS_MAIN(RomCacheTest)
#include "romcache_test.moc"
