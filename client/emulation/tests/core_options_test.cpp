// Core option capture with the real melonDS DS core (no game), cache round trip and launch option resolution.
// Without FRAMEBEAM_MELONDS_DS_CORE: return code 77 (ctest: SKIP).
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>

#include "core_locator.h"
#include "core_options.h"
#include "system_manifest.h"

using namespace framebeam::emu;

class CoreOptionsTest : public QObject {
  Q_OBJECT
  SystemManifest m_nds;
  QString m_corePath;
  QTemporaryDir m_dirs;

 private slots:
  void initTestCase() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    m_nds = *reg.find(QStringLiteral("nds"));
    const CoreLocation loc = CoreLocator().locate(m_nds);
    QVERIFY2(loc.found(), "Core not found");
    m_corePath = loc.path;
  }

  void captureWithoutGame() {
    const CoreProbe p = probeCore(m_corePath, m_dirs.filePath(QStringLiteral("system")), m_dirs.filePath(QStringLiteral("save")));
    QVERIFY2(p.ok, qPrintable(p.error));
    QVERIFY(!p.info.name.isEmpty());
    QVERIFY(!p.info.version.isEmpty());
    QVERIFY2(p.options.size() > 10, "melonDS DS reports many options");
    // Categories (V2), values and defaults are part of the capture.
    QVERIFY(!p.categories.isEmpty());
    const CoreOption* mode = nullptr;
    const CoreOption* render = nullptr;
    for (const CoreOption& o : p.options) {
      QVERIFY(!o.key.isEmpty());
      QVERIFY(!o.description.isEmpty());
      if (o.key == QLatin1String("melonds_sysfile_mode")) mode = &o;
      if (o.key == QLatin1String("melonds_render_mode")) render = &o;
    }
    QVERIFY(mode != nullptr);
    QVERIFY(render != nullptr);
    QCOMPARE(mode->values.size(), 2);
    QCOMPARE(mode->defaultValue, QStringLiteral("native"));  // core default; FrameBeam sets the mode from the Hub
    QVERIFY(!render->categoryKey.isEmpty());
    // The manifest keys that FrameBeam controls exist in the core; the locked ones are recognised.
    for (const QString& k : m_nds.lockedCoreOptions) {
      bool found = false;
      for (const CoreOption& o : p.options) found = found || o.key == k;
      QVERIFY2(found, qPrintable(k));
      QVERIFY(isLockedCoreOption(m_nds, k));
    }
    QVERIFY(isLockedCoreOption(m_nds, QStringLiteral("melonds_sysfile_mode")));
    QVERIFY(isLockedCoreOption(m_nds, QStringLiteral("melonds_firmware_nds_path")));
    QVERIFY(!isLockedCoreOption(m_nds, QStringLiteral("melonds_render_mode")));  // user choice (0.5): Software | OpenGL
    QCOMPARE(m_nds.coreOptions.value(QStringLiteral("melonds_render_mode")), QStringLiteral("software"));

    // Cache round trip keeps everything the page needs.
    const CoreProbe c = coreProbeFromJson(coreProbeToJson(p));
    QVERIFY(c.ok);
    QCOMPARE(c.info.version, p.info.version);
    QCOMPARE(c.options.size(), p.options.size());
    QCOMPARE(c.categories.size(), p.categories.size());
    QCOMPARE(c.options.first().key, p.options.first().key);
    QCOMPARE(c.options.first().values.size(), p.options.first().values.size());
    QCOMPARE(c.options.first().defaultValue, p.options.first().defaultValue);

    // A second probe works (the core is unloaded again).
    QVERIFY(probeCore(m_corePath, m_dirs.filePath(QStringLiteral("system")), m_dirs.filePath(QStringLiteral("save"))).ok);
  }
};

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("FRAMEBEAM_MELONDS_DS_CORE")) {
    std::puts("FRAMEBEAM_MELONDS_DS_CORE not set - test skipped");
    return 77;
  }
  QCoreApplication app(argc, argv);
  CoreOptionsTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "core_options_test.moc"
