// Smoke test with the real DeSmuME core and the self-generated homebrew test ROM (ADR 0020 D6 profile).
// Without FRAMEBEAM_DESMUME_CORE: return code 77 (ctest: SKIP).
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>

#include "core_locator.h"
#include "libretro_backend.h"
#include "system_manifest.h"

using namespace framebeam::emu;

class DesmumeCoreTest : public QObject {
  Q_OBJECT
  SystemManifest m_nds;
  QString m_corePath;
  QTemporaryDir m_dirs;

 private slots:
  void initTestCase() {
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("system"))));
    QVERIFY(QDir().mkpath(m_dirs.filePath(QStringLiteral("save"))));
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const auto m = reg.resolve(QStringLiteral("nds"), QStringLiteral("desmume"));
    QVERIFY(m && !m->experimental);
    m_nds = *m;
    const CoreLocation loc = CoreLocator().locate(m_nds);
    QVERIFY2(loc.found(), "Core not found");
    QCOMPARE(loc.source, QStringLiteral("env"));
    m_corePath = loc.path;
  }

  void profileOptionsAreRealAndFrameIs256x384() {
    LibretroBackend be;
    QString err;
    be.setSystemDirectory(m_dirs.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dirs.filePath(QStringLiteral("save")));
    QVERIFY2(be.loadCore(m_corePath, &err), qPrintable(err));
    QVERIFY2(be.coreInfo().name.contains(QStringLiteral("DeSmuME"), Qt::CaseInsensitive), qPrintable(be.coreInfo().name));
    for (auto it = m_nds.coreOptions.cbegin(); it != m_nds.coreOptions.cend(); ++it)
      QVERIFY2(be.setCoreOption(it.key(), it.value()), qPrintable(it.key() + QStringLiteral("=") + it.value()));
    QVERIFY2(be.loadGame(QStringLiteral(FB_TEST_ROM_PATH), &err), qPrintable(err));
    // Profile defaults must be real core options with a valid value.
    const QList<CoreOption> opts = be.coreOptions();
    for (auto it = m_nds.coreOptions.cbegin(); it != m_nds.coreOptions.cend(); ++it) {
      bool found = false;
      for (const CoreOption& o : opts) {
        if (o.key != it.key()) continue;
        found = true;
        QVERIFY2(std::any_of(o.values.cbegin(), o.values.cend(), [&](const CoreOptionValue& v) { return v.value == it.value(); }),
                 qPrintable(o.key + QStringLiteral("=") + it.value()));
      }
      QVERIFY2(found, qPrintable(it.key()));
    }
    for (int i = 0; i < 90; ++i) QVERIFY(be.runFrame());
    QVERIFY(be.frameCount() >= 90);
    QCOMPARE(be.videoFrame().size(), m_nds.display.frameSize());  // 256x384: top above bottom, no gap
    QCOMPARE(m_nds.display.frameSize(), QSize(256, 384));
    // DeSmuME manages its cartridge save itself (<content basename>.dsv): no libretro SAVE_RAM, the profile says core_file.
    QVERIFY(be.saveMemorySize() <= 0);
    QCOMPARE(m_nds.saveSource, QStringLiteral("core_file"));
    QCOMPARE(m_nds.saveExtension, QStringLiteral(".dsv"));
    QCOMPARE(m_nds.saveFormat, QStringLiteral("desmume_dsv"));
    be.unloadCore();
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("FRAMEBEAM_DESMUME_CORE")) {
    std::puts("FRAMEBEAM_DESMUME_CORE not set - test skipped");
    return 77;
  }
  QCoreApplication app(argc, argv);
  DesmumeCoreTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "core_desmume_test.moc"
