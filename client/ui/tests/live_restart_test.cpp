// GameSession::restartWithSave with the fake core (SAVE_RAM only, no ROM): a live save is a full restart of the game.
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "gamesession.h"

using namespace framebeam::ui;

namespace {
QByteArray readFile(const QString& p) {
  QFile f(p);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
void writeFile(const QString& p, const QByteArray& d) {
  QFile f(p);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(d);
}
}  // namespace

class LiveRestartTest : public QObject {
  Q_OBJECT
  QTemporaryDir dir_;
  QString saveDir() const { return dir_.filePath(QStringLiteral("save")); }
  QString save() const { return saveDir() + QStringLiteral("/game.sav"); }
  GameSession::LaunchConfig config() const {
    GameSession::LaunchConfig c;
    c.title = QStringLiteral("Fake");
    c.corePath = QStringLiteral(FB_FAKE_CORE_PATH);
    c.gamePath = dir_.filePath(QStringLiteral("game.bin"));
    c.systemDir = dir_.filePath(QStringLiteral("sys"));
    c.saveDir = saveDir();
    return c;
  }

 private slots:
  void init() {
    QDir(saveDir()).removeRecursively();
    QVERIFY(QDir().mkpath(saveDir()));
    writeFile(dir_.filePath(QStringLiteral("game.bin")), "dummy");
    qputenv("FB_FAKE_SRAM_SIZE", "8");
    qputenv("FB_FAKE_SRAM_DELAY_FRAMES", "0");
    qputenv("FB_FAKE_SRAM_WRITE", "9");  // the "game" keeps writing 9 into byte 0
  }

  void restartLoadsTheNewSaveAndKeepsTheSession() {
    writeFile(save(), QByteArray("ABCDEFGH"));
    GameSession gs;
    QSignalSpy started(&gs, &GameSession::started);
    QSignalSpy finished(&gs, &GameSession::finished);
    QSignalSpy failed(&gs, &GameSession::startFailed);
    gs.start(config());
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(gs.hasFrame(), 5000);
    QCOMPARE(started.count(), 1);
    QVERIFY(gs.liveSaveAccepts(8));
    QVERIFY(!gs.liveSaveAccepts(9));
    QSignalSpy frames(&gs, &GameSession::frameChanged);

    QVERIFY(gs.restartWithSave(save(), QByteArray("12345678")));
    QVERIFY(gs.isActive());  // Starting until the new core runs; never Idle/Failed
    QCOMPARE(readFile(save()), QByteArray("12345678"));  // the old memory was flushed first, the new file is not overwritten by it
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 5000);  // the restarted game delivers frames again
    QCOMPARE(started.count(), 1);  // same Session: no second started()
    QCOMPARE(finished.count(), 0);
    QCOMPARE(failed.count(), 0);

    gs.stop();
    QCOMPARE(readFile(save()), QByteArray("\t2345678"));  // only the game's own write on top of the new save
    QVERIFY(!gs.restartWithSave(save(), QByteArray("zzzzzzzz")));  // not running: refused
    QCOMPARE(readFile(save()), QByteArray("\t2345678"));
  }

  void restartWorksWhilePaused() {
    writeFile(save(), QByteArray("ABCDEFGH"));
    GameSession gs;
    gs.start(config());
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 5000);
    gs.pause();
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Paused, 5000);
    QVERIFY(gs.restartWithSave(save(), QByteArray("7bcdefgh")));
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 5000);
    gs.stop();
    QCOMPARE(readFile(save()), QByteArray("\tbcdefgh"));
  }

  void unwritableFileRestartsWithTheOldSave() {
    writeFile(save(), QByteArray("ABCDEFGH"));
    GameSession gs;
    gs.start(config());
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 5000);
    QVERIFY(!gs.restartWithSave(dir_.filePath(QStringLiteral("nope/x.sav")), QByteArray("12345678")));
    QTRY_COMPARE_WITH_TIMEOUT(gs.state(), GameSession::Running, 5000);  // the game runs again
    gs.stop();
    QCOMPARE(readFile(save()), QByteArray("\tBCDEFGH"));
  }
};

QTEST_MAIN(LiveRestartTest)
#include "live_restart_test.moc"
