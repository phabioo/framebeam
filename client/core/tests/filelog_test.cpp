#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "filelog.h"

using namespace framebeam;

class FileLogTest : public QObject {
  Q_OBJECT
 private slots:
  void formatHasTimestampLevelCategory() {
    const QString l = filelog::formatLine(QtWarningMsg, "framebeam.test", "a\nb");
    QVERIFY(l.contains("[W] framebeam.test: a\\nb"));
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
    QTemporaryDir t;
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
};

QTEST_GUILESS_MAIN(FileLogTest)
#include "filelog_test.moc"
