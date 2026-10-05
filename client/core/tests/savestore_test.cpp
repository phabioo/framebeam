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

  void stateRoundTripHasNoSecrets() {
    QTemporaryDir tmp;
    SyncState s;
    s.baseRevision = 4;
    s.lastSyncedSha256 = QStringLiteral("ab");
    s.pending = true;
    s.conflictId = QStringLiteral("c1");
    QVERIFY(SaveStore::saveState(tmp.path(), s));
    const SyncState r = SaveStore::loadState(tmp.path());
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
};

QTEST_GUILESS_MAIN(SaveStoreTest)
#include "savestore_test.moc"
