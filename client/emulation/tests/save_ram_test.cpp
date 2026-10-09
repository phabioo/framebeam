// SAVE_RAM persistence of LibretroBackend with a tiny fake core (no ROM, no melonDS).
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "emulation_runner.h"
#include "libretro_backend.h"

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

using namespace framebeam::emu;

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

class SaveRamTest : public QObject {
  Q_OBJECT
  QTemporaryDir dir_;
  QString saveDir() const { return dir_.filePath(QStringLiteral("save")); }
  QString save() const { return saveDir() + QStringLiteral("/game.sav"); }
  QString game() const { return dir_.filePath(QStringLiteral("game.bin")); }
  QStringList baks(const QString& pattern) const { return QDir(saveDir()).entryList({pattern}); }

  // Starts the fake core, runs n frames, unloads (which flushes).
  void play(int frames, const char* size, const char* delay, const char* write) {
    qputenv("FB_FAKE_SRAM_SIZE", size);
    qputenv("FB_FAKE_SRAM_DELAY_FRAMES", delay);
    if (write != nullptr) {
      qputenv("FB_FAKE_SRAM_WRITE", write);
    } else {
      qunsetenv("FB_FAKE_SRAM_WRITE");
    }
    LibretroBackend be;
    be.setSaveDirectory(saveDir());
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(game(), &err), qPrintable(err));
    for (int i = 0; i < frames; ++i) {
      QVERIFY(be.runFrame());
    }
    be.unloadCore();
  }

 private slots:
  void init() {
    QDir(saveDir()).removeRecursively();
    QVERIFY(QDir().mkpath(saveDir()));
    writeFile(game(), "dummy");
  }

  void writesAndReloadsSave() {
    play(3, "8", "0", "7");
    QCOMPARE(readFile(save()), QByteArray("\x07\0\0\0\0\0\0\0", 8));
    writeFile(save(), QByteArray("ABCDEFGH"));
    play(3, "8", "0", nullptr);  // existing file is loaded and written back unchanged
    QCOMPARE(readFile(save()), QByteArray("ABCDEFGH"));
  }

  void delayedSaveMemoryNeverOverwritesExistingFile() {
    writeFile(save(), QByteArray("ABCDEFGH"));
    // Save memory is missing for the first 5 frames. Unloading before it appears must not flush;
    // after it appears the file is applied first, then the game's change is written on top of it.
    play(2, "8", "5", "9");
    QCOMPARE(readFile(save()), QByteArray("ABCDEFGH"));
    play(8, "8", "5", "9");
    QCOMPARE(readFile(save()), QByteArray("\x09" "BCDEFGH", 8));
  }

  void sizeMismatchIsBackedUpBeforeWriting() {
    writeFile(save(), QByteArray("ABCDEFGHIJ"));  // 10 bytes vs 8
    play(3, "8", "0", "1");
    const QStringList b = baks(QStringLiteral("game.sav.size-mismatch-*.bak"));
    QCOMPARE(b.size(), 1);
    QCOMPARE(readFile(saveDir() + QLatin1Char('/') + b.first()), QByteArray("ABCDEFGHIJ"));
    QCOMPARE(readFile(save()).size(), 8);
  }

  // Live replacement reloads the game: the new file is loaded into memory like at a normal start, and the
  // periodic flush cannot write the old memory back over it.
  void applySaveReloadsTheGameWithTheNewSave() {
    writeFile(save(), QByteArray("ABCDEFGH"));
    qputenv("FB_FAKE_SRAM_SIZE", "8");
    qputenv("FB_FAKE_SRAM_DELAY_FRAMES", "0");
    qputenv("FB_FAKE_SRAM_WRITE", "9");  // the "game" keeps writing 9 into byte 0 every frame
    LibretroBackend be;
    be.setSaveDirectory(saveDir());
    QString err;
    QVERIFY2(be.loadCore(QStringLiteral(FB_FAKE_CORE_PATH), &err), qPrintable(err));
    QVERIFY2(be.loadGame(game(), &err), qPrintable(err));
    QVERIFY(be.runFrame());  // memory: "9BCDEFGH", not flushed yet
    QCOMPARE(be.saveMemorySize(), qint64(8));
    QVERIFY(!be.applySave(QByteArray("short")));  // other size: refused, nothing changed
    QCOMPARE(readFile(save()), QByteArray("ABCDEFGH"));
    QVERIFY(be.applySave(QByteArray("12345678")));
    QCOMPARE(readFile(save()), QByteArray("12345678"));  // the stale "9BCDEFGH" was not flushed
    // After the reload the memory holds the new bytes: the game writes 9 into byte 0 again, the rest is new.
    QVERIFY(be.runFrame());
    be.flushSave();
    QCOMPARE(readFile(save()), QByteArray("\t2345678"));
    be.unloadCore();
  }

  void runnerAppliesSaveByReloadingLive() {
    writeFile(save(), QByteArray("ABCDEFGH"));
    qputenv("FB_FAKE_SRAM_SIZE", "8");
    qputenv("FB_FAKE_SRAM_DELAY_FRAMES", "0");
    qunsetenv("FB_FAKE_SRAM_WRITE");
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QImage last;
    connect(&runner, &EmulationRunner::frameReady, this, [&last](const QImage& img, quint64) { last = img; });
    EmulationRunner::StartRequest req;
    req.corePath = QStringLiteral(FB_FAKE_CORE_PATH);
    req.gamePath = game();
    req.systemDir = dir_.filePath(QStringLiteral("sys"));
    req.saveDir = saveDir();
    runner.start(req);
    QTRY_VERIFY_WITH_TIMEOUT(runner.state() == EmulationRunner::State::Running && !last.isNull(), 5000);
    const int loads = qBlue(last.pixel(1, 0)) >> 3;
    QCOMPARE(qBlue(last.pixel(2, 0)) >> 3, 'A' & 0x1f);  // the start loaded the old file into memory
    QVERIFY(runner.saveMemoryAccepts(8));
    QVERIFY(!runner.saveMemoryAccepts(9));
    QVERIFY(!runner.applySaveAndReset(QByteArray("tooLongForTheCore")));
    QCOMPARE(readFile(save()), QByteArray("ABCDEFGH"));
    QVERIFY(runner.applySaveAndReset(QByteArray("12345678")));
    QCOMPARE(readFile(save()), QByteArray("12345678"));
    // The game was loaded again (load counter) and its memory holds the new save.
    QTRY_VERIFY_WITH_TIMEOUT((qBlue(last.pixel(1, 0)) >> 3) == loads + 1 && (qBlue(last.pixel(2, 0)) >> 3) == ('1' & 0x1f), 3000);
    // Also while paused
    runner.pause();
    QTRY_COMPARE_WITH_TIMEOUT(runner.state(), EmulationRunner::State::Paused, 3000);
    QVERIFY(runner.applySaveAndReset(QByteArray("7bcdefgh")));
    QCOMPARE(readFile(save()), QByteArray("7bcdefgh"));
    runner.stop();
    QCOMPARE(readFile(save()), QByteArray("7bcdefgh"));  // stopping flushes nothing old over it
    QVERIFY(!runner.applySaveAndReset(QByteArray("zzzzzzzz")));  // not running: refused
    QCOMPARE(readFile(save()), QByteArray("7bcdefgh"));
  }

  void unreadableFileIsNeverOverwritten() {
#ifdef Q_OS_WIN
    QSKIP("permissions test is POSIX only");
#else
    if (geteuid() == 0) {
      QSKIP("permissions are not enforced for root");
    }
    writeFile(save(), QByteArray("ABCDEFGH"));
    QFile::setPermissions(save(), QFileDevice::Permissions());
    play(3, "8", "0", "1");
    QFile::setPermissions(save(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QCOMPARE(readFile(save()), QByteArray("ABCDEFGH"));
#endif
  }
};

QTEST_GUILESS_MAIN(SaveRamTest)
#include "save_ram_test.moc"
