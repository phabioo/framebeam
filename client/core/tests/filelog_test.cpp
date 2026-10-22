#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "filelog.h"

using namespace uzume;

class FileLogTest : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> keep_;
 private slots:
  void formatHasTimestampLevelCategory() {
    const QString l = filelog::formatLine(QtWarningMsg, "uzume.test", "a\nb");
    QVERIFY(l.contains("[W] uzume.test: a\\nb"));
    QVERIFY(QDateTime::fromString(l.left(23), Qt::ISODateWithMs).isValid());
  }
  void rotateKeepsThree() {
    QTemporaryDir t;
    const QString p = t.filePath("player.log");
    for (int round = 1; round <= 5; ++round) {
      QFile f(p);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray::number(round));
      f.close();
      filelog::rotate(p);
    }
    QVERIFY(!QFile::exists(p));
    auto read = [&](const QString& n) { QFile f(t.filePath(n)); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
    QCOMPARE(read("player.1.log"), QByteArray("5"));
    QCOMPARE(read("player.2.log"), QByteArray("4"));
    QCOMPARE(read("player.3.log"), QByteArray("3"));
    QVERIFY(!QFile::exists(t.filePath("player.4.log")));
  }
  void handlerWritesBufferedAndLaterLines() {
    keep_ = std::make_unique<QTemporaryDir>();  // lives on for the next test (the log directory is fixed once per process)
    QTemporaryDir& t = *keep_;
    filelog::install();
    qWarning("before directory");
    QVERIFY(filelog::setDirectory(t.path(), 400));
    qInfo("after directory");
    {
      QFile f(t.filePath("logs/player.log"));
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QByteArray all = f.readAll();
      QVERIFY(all.contains("before directory"));  // buffered line was flushed
      QVERIFY(all.contains("after directory"));
    }
    QCOMPARE(filelog::path(), QDir(t.path()).filePath("logs/player.log"));
    for (int i = 0; i < 40; ++i) qWarning("filler line number %d to exceed the size cap", i);  // forces a rotation
    QVERIFY(QFile::exists(t.filePath("logs/player.1.log")));
    QFile f(t.filePath("logs/player.log"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.size() <= 400 + 200);
  }
  // PN-2: when the log file cannot be reopened after a rotation, logging must not deadlock on the handler's own warning.
  // The state of the previous test is reused (the directory is fixed once per process).
  void failedReopenAfterRotationDoesNotDeadlock() {
    const QString logs = filelog::path().section(QLatin1Char('/'), 0, -2);
    QVERIFY(!logs.isEmpty() && QDir(logs).exists());
#ifdef Q_OS_WIN
    // Windows cannot rename a directory with an open file: block every rotation step instead (player.3.log is a non-empty
    // directory, so no rename succeeds) and make player.log read-only, so the reopen after the rotation fails.
    const QString log = filelog::path();
    QFile::remove(QDir(logs).filePath(QStringLiteral("player.3.log")));  // the previous test's rotations may have left it
    QVERIFY(QDir(logs).mkpath(QStringLiteral("player.3.log/keep")));
    for (const char* name : {"player.1.log", "player.2.log"}) {
      QFile f(QDir(logs).filePath(QLatin1String(name)));
      QVERIFY(f.open(QIODevice::Append));
    }
    QVERIFY(QFile::setPermissions(log, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    auto restore = qScopeGuard([log]() {
      QFile::setPermissions(log, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
    });
#else
    // Replace the logs directory by a plain file: the open handle keeps working until the next rotation, which fails.
    QVERIFY(QDir().rename(logs, logs + QStringLiteral("_moved")));
    QFile blocker(logs);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
#endif
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> fut = done->get_future();
    std::thread worker([done]() {
      for (int i = 0; i < 80; ++i) qWarning("line %d after the log directory vanished, long enough to force a rotation attempt", i);
      done->set_value();
    });
    const bool finished = fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    if (finished) worker.join(); else worker.detach();
    QVERIFY2(finished, "logging deadlocked after the log file could not be reopened");
    qWarning("still alive");
  }
};

QTEST_GUILESS_MAIN(FileLogTest)
#include "filelog_test.moc"
