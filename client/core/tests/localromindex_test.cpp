// Local ROM scanner and hash cache on dummy files (no real ROMs).
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "localromindex.h"
#include "playersettings.h"

using namespace framebeam;

namespace {
QString writeFile(const QString& path, const QByteArray& d) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  f.open(QIODevice::WriteOnly);
  f.write(d);
  return path;
}
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
}  // namespace

class LocalRomIndexTest : public QObject {
  Q_OBJECT
 private slots:
  void scansHashesAndReadsIdentity() {
    QTemporaryDir dir;
    const QByteArray a(5000, 'a');
    writeFile(dir.filePath("sub/game.NDS"), a);
    writeFile(dir.filePath("notes.txt"), "x");
    writeFile(dir.filePath("empty.nds"), "");
    LocalScanOptions opt;
    opt.folders = {dir.path(), dir.filePath("sub")};  // overlapping folders: file listed once
    opt.extensionToSystem = {{".nds", "nds"}};
    int calls = 0;
    opt.identity = [&](const QString&, const QString& system) {
      ++calls;
      return LocalRomIdentity{system == "nds" ? QStringLiteral("ABCD") : QString(), 2};
    };
    RomHashCache cache;
    const LocalScanResult r = scanLibraryFolders(opt, cache);
    QCOMPARE(r.files.size(), 1);
    QCOMPARE(r.files.first().sha256, shaOf(a));
    QCOMPARE(r.files.first().system, QStringLiteral("nds"));
    QCOMPARE(r.files.first().gameCode, QStringLiteral("ABCD"));
    QCOMPARE(r.files.first().gameVersion, 2);
    QCOMPARE(r.hashed, 1);
    QCOMPARE(calls, 1);
  }

  void hashCacheSkipsUnchangedFiles() {
    QTemporaryDir dir;
    const QString cacheFile = dir.filePath("cache/hashes.json");
    const QString rom = writeFile(dir.filePath("roms/a.nds"), QByteArray(3000, 'a'));
    LocalScanOptions opt;
    opt.folders = {dir.filePath("roms")};
    opt.extensionToSystem = {{".nds", "nds"}};
    {
      RomHashCache cache(cacheFile);
      QCOMPARE(scanLibraryFolders(opt, cache).hashed, 1);
      QVERIFY(cache.save());
    }
    {
      RomHashCache cache(cacheFile);  // persisted: a new process does not hash again
      const LocalScanResult r = scanLibraryFolders(opt, cache);
      QCOMPARE(r.hashed, 0);
      QCOMPARE(r.files.size(), 1);
      QCOMPARE(r.files.first().sha256, shaOf(QByteArray(3000, 'a')));
    }
    {
      // Content and size change: hashed again
      writeFile(rom, QByteArray(3100, 'b'));
      RomHashCache cache(cacheFile);
      const LocalScanResult r = scanLibraryFolders(opt, cache);
      QCOMPARE(r.hashed, 1);
      QCOMPARE(r.files.first().sha256, shaOf(QByteArray(3100, 'b')));
    }
    {
      // Same size, new mtime: hashed again
      RomHashCache cache(cacheFile);
      scanLibraryFolders(opt, cache);
      QVERIFY(cache.save());
      QFile f(rom);
      QVERIFY(f.open(QIODevice::ReadWrite));
      QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
      f.close();
      RomHashCache cache2(cacheFile);
      QCOMPARE(scanLibraryFolders(opt, cache2).hashed, 1);
    }
  }

  void removedFilesLeaveTheIndex() {
    QTemporaryDir dir;
    LocalScanOptions opt;
    opt.folders = {dir.path()};
    opt.extensionToSystem = {{".nds", "nds"}};
    const QString p = writeFile(dir.filePath("a.nds"), QByteArray(100, 'q'));
    RomHashCache cache;
    QCOMPARE(scanLibraryFolders(opt, cache).files.size(), 1);
    QFile::remove(p);
    QCOMPARE(scanLibraryFolders(opt, cache).files.size(), 0);
    QCOMPARE(cache.size(), 0);
  }

  void asyncIndexFindsPathBySha() {
    QTemporaryDir dir;
    const QByteArray a(2048, 'z');
    const QString p = writeFile(dir.filePath("roms/z.nds"), a);
    LocalRomIndex index(dir.filePath("data/hashes.json"));
    LocalScanOptions opt;
    opt.folders = {dir.filePath("roms")};
    opt.extensionToSystem = {{".nds", "nds"}};
    QSignalSpy done(&index, &LocalRomIndex::scanFinished);
    QVERIFY(index.rescan(opt));
    QVERIFY(!index.rescan(opt));  // one scan at a time
    QTRY_COMPARE(done.count(), 1);
    QCOMPARE(index.pathForSha(shaOf(a)), QFileInfo(p).absoluteFilePath());
    QVERIFY(index.pathForSha(QString(64, '0')).isEmpty());
    // The file changes behind the index: not offered any more
    writeFile(p, QByteArray(10, 'c'));
    QVERIFY(index.pathForSha(shaOf(a)).isEmpty());
    // A new index instance starts from the persisted cache
    LocalRomIndex again(dir.filePath("data/hashes.json"));
    QCOMPARE(again.files().size(), 1);
  }

  // PN-3: deleting the index while a scan still hashes a big file: the worker keeps the cancel flag alive, stops at the next
  // chunk and the destructor does not wait for the whole file.
  void deletingTheIndexMidScanIsSafeAndPrompt() {
    QTemporaryDir dir;
    const QString big = dir.filePath("roms/big.nds");
    QDir().mkpath(QFileInfo(big).absolutePath());
    QFile f(big);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.resize(qint64(1) << 30));  // sparse dummy file, hashing it takes seconds
    f.close();
    auto* index = new LocalRomIndex(dir.filePath("data/hashes.json"));
    LocalScanOptions opt;
    opt.folders = {dir.filePath("roms")};
    opt.extensionToSystem = {{".nds", "nds"}};
    QVERIFY(index->rescan(opt));
    QTest::qSleep(50);
    QElapsedTimer t;
    t.start();
    delete index;
    QVERIFY2(t.elapsed() < 1000, qPrintable(QStringLiteral("delete took %1 ms").arg(t.elapsed())));
    QTest::qWait(300);  // the worker finishes against its own copy of the flag
  }

  void settingsKeepLibraryFolders() {
    QTemporaryDir dir;
    {
      PlayerSettings s(dir.path());
      QVERIFY(s.libraryFolders().isEmpty());
      QVERIFY(!s.setLibraryFolders({QStringLiteral("relative/dir")}));
      QVERIFY(s.setLibraryFolders({dir.filePath("a"), dir.filePath("a/"), dir.filePath("b")}));
      QCOMPARE(s.libraryFolders().size(), 2);
    }
    PlayerSettings again(dir.path());
    QCOMPARE(again.libraryFolders(), QStringList({dir.filePath("a"), dir.filePath("b")}));
  }
};

QTEST_GUILESS_MAIN(LocalRomIndexTest)
#include "localromindex_test.moc"
