// Probe behaviour without ROMs: no-game load only for cores that declare SET_SUPPORT_NO_GAME, and the crash guard.
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "core_options.h"

using namespace framebeam::emu;

class CoreProbeTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;
  QString sys() const { return m_dir.filePath(QStringLiteral("system")); }
  QString probeDir() const { return m_dir.filePath(QStringLiteral("probe")); }
  QString marker() const { return QDir(probeDir()).filePath(QStringLiteral("probe_running.json")); }
  QString unsafeFile() const { return QDir(probeDir()).filePath(QStringLiteral("unsafe_cores.json")); }

  static bool setMtime(const QString& path, const QDateTime& t) {
    QFile f(path);
    return f.open(QIODevice::ReadWrite) && f.setFileTime(t, QFileDevice::FileModificationTime);
  }

  // Private copy of a fake core so its mtime/size can be changed.
  QString copyCore(const QString& name) {
    const QString dst = m_dir.filePath(name + QLatin1Char('_') + QFileInfo(FB_FAKE_NOGAME_CORE_PATH).fileName());
    QFile::remove(dst);
    [&] { QVERIFY(QFile::copy(QStringLiteral(FB_FAKE_NOGAME_CORE_PATH), dst)); }();
    return dst;
  }

 private slots:
  void coreWithoutNoGameSupportIsNotLoadedWithNull() {
    // retro_load_game would dereference NULL and crash this process if it were called.
    const CoreProbe p = probeCore(QStringLiteral(FB_FAKE_NOGAME_CORE_PATH), sys(), probeDir());
    QVERIFY2(p.ok, qPrintable(p.error));
    QCOMPARE(p.info.name, QStringLiteral("fake_nogame"));
    QCOMPARE(p.options.size(), 1);  // registered in retro_init
  }

  void coreWithNoGameSupportGetsNoGameLoad() {
    const CoreProbe p = probeCore(QStringLiteral(FB_FAKE_NOGAME_DECLARED_CORE_PATH), sys(), probeDir());
    QVERIFY2(p.ok, qPrintable(p.error));
    QCOMPARE(p.options.size(), 1);  // only registered in retro_load_game(NULL)
    QCOMPARE(p.options.first().key, QStringLiteral("fake_opt"));
  }

  void normalGuardedProbeLeavesNoMarker() {
    const CoreProbe p = probeCoreGuarded(QStringLiteral(FB_FAKE_NOGAME_CORE_PATH), sys(), probeDir());
    QVERIFY(p.ok);
    QVERIFY(!QFile::exists(marker()));
    QVERIFY(!QFile::exists(unsafeFile()));
  }

  void leftoverMarkerMarksLibraryUnsafe() {
    const QString core = copyCore(QStringLiteral("crash"));
    QVERIFY(probeCoreGuarded(core, sys(), probeDir()).ok);

    // Simulate a crashed probe: write the marker the way the guard does (probe a copy, keep its marker key).
    // The guard stores path|size|mtime; recreate it from the file.
    const QFileInfo fi(core);
    const QString key = QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
    QFile m(marker());
    QVERIFY(m.open(QIODevice::WriteOnly));
    m.write(QJsonDocument(QJsonArray{key}).toJson());
    m.close();

    const CoreProbe skipped = probeCoreGuarded(core, sys(), probeDir());
    QVERIFY(!skipped.ok);
    QVERIFY(!skipped.error.isEmpty());
    QVERIFY(!QFile::exists(marker()));
    QVERIFY(QFile::exists(unsafeFile()));
    // Stays skipped on later calls.
    QVERIFY(!probeCoreGuarded(core, sys(), probeDir()).ok);

    // Changed mtime: probed again.
    QVERIFY(setMtime(core, fi.lastModified().addSecs(60)));
    QVERIFY(probeCoreGuarded(core, sys(), probeDir()).ok);

    // Changed size: probed again (restore the old mtime to prove size alone matters).
    {
      QFile f(core);
      QVERIFY(f.open(QIODevice::Append));
      f.write("x");
    }
    QVERIFY(setMtime(core, fi.lastModified()));
    QVERIFY(probeCoreGuarded(core, sys(), probeDir()).ok);
  }
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  CoreProbeTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "core_probe_test.moc"
