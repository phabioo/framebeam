#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "profilestore.h"
#include "savestore.h"

using namespace framebeam;

namespace {
void writeFile(const QString& path, const QByteArray& d) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(d);
}
QByteArray readFile(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

class SaveStoreTest : public QObject {
  Q_OBJECT
 private slots:
  void pathsSeparatedByHubAndUser() {
    QTemporaryDir tmp;
    ProfileStore ps(tmp.path());
    const QString a = SaveStore::gameDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("g1"));
    const QString b = SaveStore::gameDir(ps, QStringLiteral("hub-b"), QStringLiteral("u1"), QStringLiteral("g1"));
    const QString c = SaveStore::gameDir(ps, QStringLiteral("hub-a"), QStringLiteral("u2"), QStringLiteral("g1"));
    QVERIFY(a.contains(QStringLiteral("hubs/hub-a/users/u1/saves/g1")));
    QVERIFY(a != b && a != c);
    QVERIFY(SaveStore::gameDir(ps, QStringLiteral("hub-a"), QStringLiteral("../x"), QStringLiteral("g1")).isEmpty());
    QVERIFY(SaveStore::gameDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("a/b")).isEmpty());
    QVERIFY(SaveStore::gameDir(ps, QStringLiteral("hub-a"), QString(), QStringLiteral("g1")).isEmpty());
  }

  void shortCoreDirIsShortAndSeparated() {
    QTemporaryDir tmp;
    ProfileStore ps(tmp.path());
    const auto d = [&](const char* h, const char* u, const char* g, const char* s) {
      return SaveStore::shortCoreDir(ps, QString::fromLatin1(h), QString::fromLatin1(u), QString::fromLatin1(g), QString::fromLatin1(s));
    };
    const QString a = d("hub-a", "u1", "g1", "default");
    QCOMPARE(a, d("hub-a", "u1", "g1", "default"));
    QVERIFY(a.startsWith(QDir(tmp.path()).filePath(QStringLiteral("c/"))));
    QCOMPARE(a.size(), QDir(tmp.path()).filePath(QStringLiteral("c/")).size() + 16);
    QVERIFY(a != d("hub-b", "u1", "g1", "default") && a != d("hub-a", "u2", "g1", "default") &&
            a != d("hub-a", "u1", "g2", "default") && a != d("hub-a", "u1", "g1", "boss"));
    QVERIFY(d("hub-a", "../x", "g1", "default").isEmpty());
    QVERIFY(d("hub-a", "u1", "g1", "Bad Slot").isEmpty());
  }

  void windowsAliasNamesAreRejected() {
    for (const char* bad : {"con", "CON", "Nul", "aux", "prn", "com1", "COM9", "lpt3", "con.txt", "nul.x.y", "abc.", "abc "}) {
      const QString n = QString::fromLatin1(bad);
      QVERIFY2(SaveStore::isWindowsAliasName(n), bad);
      QVERIFY2(!SaveStore::isSafeId(n), bad);
    }
    for (const char* ok : {"console", "com", "com10", "com0", "lpt", "abc", "a.b", "g-1_2", "nul1"}) {
      QVERIFY2(!SaveStore::isWindowsAliasName(QString::fromLatin1(ok)), ok);
    }
    QVERIFY(SaveStore::isSafeId(QStringLiteral("c3a1d9f0-1111-2222-3333-444455556666")));
    QVERIFY(!SaveStore::isValidSlotName(QStringLiteral("nul")));
    QVERIFY(!SaveStore::isValidSlotName(QStringLiteral("com1")));
    QVERIFY(SaveStore::isValidSlotName(QStringLiteral("boss-run")));
    QVERIFY(SaveStore::isValidSlotName(QStringLiteral("default")));
    QTemporaryDir tmp;
    ProfileStore ps(tmp.path());
    QVERIFY(!ProfileStore::isValidHubId(QStringLiteral("con")));
    QVERIFY(!ProfileStore::isValidHubId(QStringLiteral("LPT1")));
    QVERIFY(ProfileStore::isValidHubId(QStringLiteral("hub-a")));
    QVERIFY(SaveStore::gameDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("nul")).isEmpty());
    QVERIFY(SaveStore::slotDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("g1"), QStringLiteral("aux")).isEmpty());
  }

  void migrateCoreFileMovesOnceAndNeverOverwrites() {
    QTemporaryDir tmp;
    const QString oldDir = tmp.filePath(QStringLiteral("old"));
    const QString newDir = tmp.filePath(QStringLiteral("new"));
    const QString name = QStringLiteral("rom.dsv");
    QCOMPARE(int(SaveStore::migrateCoreFile(oldDir, newDir, name)), int(SaveStore::FileMove::None));  // nothing to move
    writeFile(oldDir + QStringLiteral("/rom.dsv"), "one");
    writeFile(oldDir + QStringLiteral("/rom.sav"), "raw");
    QCOMPARE(int(SaveStore::migrateCoreFile(oldDir, newDir, name)), int(SaveStore::FileMove::Moved));
    QCOMPARE(readFile(newDir + QStringLiteral("/rom.dsv")), QByteArray("one"));
    QVERIFY(!QFileInfo::exists(oldDir + QStringLiteral("/rom.dsv")));
    QCOMPARE(readFile(oldDir + QStringLiteral("/rom.sav")), QByteArray("raw"));  // other files stay
    // both exist: the target wins, the source is kept
    writeFile(oldDir + QStringLiteral("/rom.dsv"), "late");
    QCOMPARE(int(SaveStore::migrateCoreFile(oldDir, newDir, name)), int(SaveStore::FileMove::KeptBoth));
    QCOMPARE(readFile(newDir + QStringLiteral("/rom.dsv")), QByteArray("one"));
    QCOMPARE(readFile(oldDir + QStringLiteral("/rom.dsv")), QByteArray("late"));
    QCOMPARE(int(SaveStore::migrateCoreFile(oldDir, oldDir, name)), int(SaveStore::FileMove::None));  // same directory
    QCOMPARE(int(SaveStore::migrateCoreFile(oldDir, newDir, QStringLiteral("../x"))), int(SaveStore::FileMove::None));
  }

  void legacyPathLimitCheck() {
    const QString dir = QStringLiteral("/a/b/c");
    const int len = QDir::toNativeSeparators(QDir(dir).absolutePath()).size();
    QVERIFY(!SaveStore::exceedsLegacyPathLimit(dir, 259 - len - 1));
    QVERIFY(SaveStore::exceedsLegacyPathLimit(dir, 259 - len));
    QVERIFY(!SaveStore::exceedsLegacyPathLimit(QString(), 1000));
  }

  void migrateCoreSubfolderMovesOnceAndNeverOverwrites() {
    QTemporaryDir tmp;
    const QString oldDir = tmp.filePath(QStringLiteral("old"));
    const QString newDir = tmp.filePath(QStringLiteral("new"));
    writeFile(oldDir + QStringLiteral("/Azahar/nand/a/b.bin"), "one");
    writeFile(oldDir + QStringLiteral("/other.sav"), "keep");
    QVERIFY(SaveStore::migrateCoreSubfolder(oldDir, newDir, QStringLiteral("Azahar")));
    QCOMPARE(readFile(newDir + QStringLiteral("/Azahar/nand/a/b.bin")), QByteArray("one"));
    QVERIFY(!QFileInfo::exists(oldDir + QStringLiteral("/Azahar")));
    QCOMPARE(readFile(oldDir + QStringLiteral("/other.sav")), QByteArray("keep"));
    // existing new dir is not overwritten
    writeFile(oldDir + QStringLiteral("/Azahar/x.bin"), "late");
    QVERIFY(!SaveStore::migrateCoreSubfolder(oldDir, newDir, QStringLiteral("Azahar")));
    QVERIFY(QFileInfo::exists(oldDir + QStringLiteral("/Azahar/x.bin")));
    QVERIFY(!QFileInfo::exists(newDir + QStringLiteral("/Azahar/x.bin")));
    QCOMPARE(readFile(newDir + QStringLiteral("/Azahar/nand/a/b.bin")), QByteArray("one"));
  }

  void stateRoundTripHasNoSecrets() {
    QTemporaryDir tmp;
    SyncState s;
    s.baseRevision = 4;
    s.lastSyncedSha256 = QStringLiteral("ab");
    s.pending = true;
    s.conflictId = QStringLiteral("c1");
    s.writerCoreId = QStringLiteral("my-core");
    s.writerCoreVersion = QStringLiteral("2026.10.09.2");
    QVERIFY(SaveStore::saveState(tmp.path(), s));
    const SyncState r = SaveStore::loadState(tmp.path());
    QCOMPARE(r.writerCoreId, QStringLiteral("my-core"));
    QCOMPARE(r.writerCoreVersion, QStringLiteral("2026.10.09.2"));
    QVERIFY(SaveStore::loadState(tmp.path() + QStringLiteral("/none")).writerCoreId.isEmpty());
    QCOMPARE(r.baseRevision, 4);
    QCOMPARE(r.lastSyncedSha256, QStringLiteral("ab"));
    QVERIFY(r.pending);
    QCOMPARE(r.conflictId, QStringLiteral("c1"));
    QVERIFY(r.updatedAt.isValid());
    const QByteArray raw = readFile(SaveStore::stateFilePath(tmp.path())).toLower();
    QVERIFY(!raw.contains("token") && !raw.contains("credential") && !raw.contains("secret"));
    QCOMPARE(SaveStore::loadState(tmp.path() + QStringLiteral("/none")).baseRevision, 0);
  }

  void findSaveFile() {
    QTemporaryDir tmp;
    QCOMPARE(SaveStore::expectedSaveName(QStringLiteral("/x/cache/abc.nds")), QStringLiteral("abc.sav"));
    QVERIFY(SaveStore::findSaveFile(tmp.path(), QStringLiteral("abc.sav")).isEmpty());
    writeFile(tmp.path() + QStringLiteral("/sync.json"), "{}");
    writeFile(tmp.path() + QStringLiteral("/abc.sav.bak"), "b");
    writeFile(tmp.path() + QStringLiteral("/x.tmp"), "b");
    QVERIFY(SaveStore::findSaveFile(tmp.path(), QStringLiteral("abc.sav")).isEmpty());
    writeFile(tmp.path() + QStringLiteral("/other.sav"), "o");
    QStringList warn;
    QVERIFY(SaveStore::findSaveFile(tmp.path(), QStringLiteral("abc.sav"), &warn).endsWith(QStringLiteral("other.sav")));
    QVERIFY(warn.isEmpty());  // a single candidate is no warning
    writeFile(tmp.path() + QStringLiteral("/abc.sav"), "a");
    QVERIFY(SaveStore::findSaveFile(tmp.path(), QStringLiteral("abc.sav")).endsWith(QStringLiteral("abc.sav")));
  }

  void legacyMigrationCopiesNeverDeletes() {
    QTemporaryDir tmp;
    const QString legacy = tmp.path() + QStringLiteral("/legacy");
    const QString game = tmp.path() + QStringLiteral("/game");
    writeFile(legacy + QStringLiteral("/rom1.sav"), "legacy-data");
    QVERIFY(!SaveStore::migrateLegacy(legacy, {QStringLiteral("nomatch")}, game, QStringLiteral("abc.sav")));
    QVERIFY(SaveStore::migrateLegacy(legacy, {QStringLiteral("nomatch"), QStringLiteral("rom1")}, game, QStringLiteral("abc.sav")));
    QCOMPARE(readFile(game + QStringLiteral("/abc.sav")), QByteArray("legacy-data"));
    QVERIFY(QFileInfo::exists(legacy + QStringLiteral("/rom1.sav")));
    // does not overwrite an existing save
    writeFile(game + QStringLiteral("/abc.sav"), "new");
    QVERIFY(!SaveStore::migrateLegacy(legacy, {QStringLiteral("rom1")}, game, QStringLiteral("abc.sav")));
    QCOMPARE(readFile(game + QStringLiteral("/abc.sav")), QByteArray("new"));
  }

  void backups() {
    QTemporaryDir tmp;
    const QString f = tmp.path() + QStringLiteral("/a.sav");
    writeFile(f, "one");
    const QString b1 = SaveStore::backupFile(f);
    QVERIFY(b1.endsWith(QStringLiteral("a.sav.bak")));
    const QString b2 = SaveStore::backupFile(f, QStringLiteral("local"));
    QVERIFY(b2.contains(QStringLiteral("a.sav.local-")) && b2.endsWith(QStringLiteral(".bak")));
    QCOMPARE(readFile(b2), QByteArray("one"));
    QVERIFY(SaveStore::atomicWrite(f, "two"));
    QCOMPARE(readFile(f), QByteArray("two"));
    QCOMPARE(SaveStore::sha256OfFile(f), SaveStore::sha256Of("two"));
  }

  void slotNamesAndDirs() {
    QTemporaryDir tmp;
    ProfileStore ps(tmp.path());
    for (const char* ok : {"default", "a", "slot-2", "my_slot", "0123456789012345678901234567890"}) {
      QVERIFY2(SaveStore::isValidSlotName(QString::fromLatin1(ok)), ok);
    }
    for (const QString& bad : {QString(), QStringLiteral("Upper"), QStringLiteral("a b"), QStringLiteral("a/b"), QStringLiteral(".."),
                               QStringLiteral("a\n"), QString(33, QLatin1Char('a'))}) {
      QVERIFY2(!SaveStore::isValidSlotName(bad), qPrintable(bad));
    }
    const QString game = SaveStore::gameDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("g1"));
    // Migration: existing (pre-0.4) files stay in the game dir and belong to slot "default"
    QCOMPARE(SaveStore::slotDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("g1"), QStringLiteral("default")), game);
    writeFile(game + QStringLiteral("/rom.sav"), "legacy");
    writeFile(game + QStringLiteral("/sync.json"), "{\"base_revision\":2,\"last_synced_sha256\":\"x\"}");  // no slot key
    QCOMPARE(SaveStore::loadState(game).slot, QStringLiteral("default"));
    QCOMPARE(SaveStore::loadState(game).baseRevision, 2);
    const QString other = SaveStore::slotDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("g1"), QStringLiteral("boss"));
    QVERIFY(other != game && other.startsWith(game + QStringLiteral("/slots/")));
    QVERIFY(SaveStore::slotDir(ps, QStringLiteral("hub-a"), QStringLiteral("u1"), QStringLiteral("g1"), QStringLiteral("../x")).isEmpty());
    writeFile(other + QStringLiteral("/rom.sav"), "boss-save");
    QCOMPARE(SaveStore::localSlots(game), (QStringList{QStringLiteral("default"), QStringLiteral("boss")}));
    // The default slot's save lookup never picks up files of other slots
    QCOMPARE(SaveStore::findSaveFile(game, QStringLiteral("rom.sav")), game + QStringLiteral("/rom.sav"));
    QCOMPARE(readFile(SaveStore::findSaveFile(other, QStringLiteral("rom.sav"))), QByteArray("boss-save"));
    QCOMPARE(readFile(game + QStringLiteral("/rom.sav")), QByteArray("legacy"));
  }
};

QTEST_GUILESS_MAIN(SaveStoreTest)
#include "savestore_test.moc"
