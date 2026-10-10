#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "fsutil.h"
#include "controllerprofiles.h"
#include "playersettings.h"
#include "emulationsettings.h"

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
QStringList corruptCopies(const QString& dir, const QString& name) {
  return QDir(dir).entryList({name + QStringLiteral(".corrupt-*")}, QDir::Files);
}
}  // namespace

class FsUtilTest : public QObject {
  Q_OBJECT
 private slots:
  void validJsonIsReadAndMissingIsEmpty() {
    QTemporaryDir t;
    writeFile(t.filePath(QStringLiteral("a.json")), R"({"k": 1})");
    QCOMPARE(fsutil::readJsonObject(t.filePath(QStringLiteral("a.json"))).value(QStringLiteral("k")).toInt(), 1);
    QVERIFY(fsutil::readJsonObject(t.filePath(QStringLiteral("none.json"))).isEmpty());
    QVERIFY(corruptCopies(t.path(), QStringLiteral("a.json")).isEmpty());
  }

  void corruptJsonIsMovedAsideAndNeverOverwritten() {
    QTemporaryDir t;
    const QString p = t.filePath(QStringLiteral("profiles.json"));
    writeFile(p, "{ this is not json");
    QVERIFY(fsutil::readJsonObject(p).isEmpty());
    QVERIFY(!QFileInfo::exists(p));
    const QStringList kept = corruptCopies(t.path(), QStringLiteral("profiles.json"));
    QCOMPARE(kept.size(), 1);
    QCOMPARE(readFile(t.filePath(kept.first())), QByteArray("{ this is not json"));
    // a JSON array is not an object either, and a second broken file gets its own name
    writeFile(p, "[1,2]");
    QVERIFY(fsutil::readJsonObject(p).isEmpty());
    QVERIFY(corruptCopies(t.path(), QStringLiteral("profiles.json")).size() == 2);
  }

  void settingsFilesKeepTheBrokenFileBeforeTheFirstSave() {
    QTemporaryDir t;
    const QString player = t.filePath(QStringLiteral("settings/player.json"));
    const QString emu = t.filePath(QStringLiteral("settings/emulation.json"));
    const QString ctl = t.filePath(QStringLiteral("settings/controllers.json"));
    writeFile(player, "{broken");
    writeFile(emu, "{broken");
    writeFile(ctl, "{broken");
    {
      PlayerSettings p(t.path());
      EmulationSettings e(t.path());
      ControllerProfiles c(t.path());
    }
    for (const QString& n : {QStringLiteral("player.json"), QStringLiteral("emulation.json"), QStringLiteral("controllers.json")}) {
      QCOMPARE(corruptCopies(t.filePath(QStringLiteral("settings")), n).size(), 1);
      QVERIFY(!QFileInfo::exists(t.filePath(QStringLiteral("settings/") + n)));
    }
  }

  void replaceFileReplacesOnlyWithAnExistingSource() {
    QTemporaryDir t;
    const QString a = t.filePath(QStringLiteral("a"));
    const QString b = t.filePath(QStringLiteral("b"));
    writeFile(a, "new");
    writeFile(b, "old");
    QVERIFY(fsutil::replaceFile(a, b));
    QCOMPARE(readFile(b), QByteArray("new"));
    QVERIFY(!QFileInfo::exists(a));
    // missing source: the target is not touched
    QVERIFY(!fsutil::replaceFile(a, b));
    QCOMPARE(readFile(b), QByteArray("new"));
    // target does not exist yet
    writeFile(a, "x");
    QVERIFY(fsutil::replaceFile(a, t.filePath(QStringLiteral("sub-not-there"))));
  }

  void envPathReadsTheEnvironment() {
    qputenv("FRAMEBEAM_TEST_ENV_PATH", "/tmp/x y");
    QCOMPARE(fsutil::envPath("FRAMEBEAM_TEST_ENV_PATH"), QStringLiteral("/tmp/x y"));
    qunsetenv("FRAMEBEAM_TEST_ENV_PATH");
    QVERIFY(fsutil::envPath("FRAMEBEAM_TEST_ENV_PATH").isEmpty());
  }
};

QTEST_GUILESS_MAIN(FsUtilTest)
#include "fsutil_test.moc"
