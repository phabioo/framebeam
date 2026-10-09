// Manifest parsing and CoreLocator (without core).
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core_locator.h"
#include "system_manifest.h"

using namespace framebeam::emu;

class ManifestTest : public QObject {
  Q_OBJECT
 private slots:
  void builtinNds() {
    ManifestRegistry reg;
    QString err;
    QVERIFY2(reg.loadBuiltin(&err), qPrintable(err));
    const SystemManifest* nds = reg.find(QStringLiteral("nds"));
    QVERIFY(nds);
    QCOMPARE(nds->displayName, QStringLiteral("Nintendo DS"));
    QVERIFY(nds->coreId.isEmpty());  // the system manifest names no core
    QCOMPARE(nds->extensions, QStringList{QStringLiteral(".nds")});
    QVERIFY(!nds->firmware.required);
    QCOMPARE(nds->inputProfile, QStringLiteral("nds"));
    QCOMPARE(nds->displayProfile, QStringLiteral("dual_screen"));
    QCOMPARE(nds->display.frameSize(), QSize(256, 384));
    QCOMPARE(nds->display.touchScreenIndex(), 1);
    QCOMPARE(nds->display.screenRect(1), QRect(0, 192, 256, 192));
    QCOMPARE(nds->display.toFrameNormalized(1, {0.5, 0.5}), QPointF(0.5, 0.75));
    QVERIFY(nds->supportsExtension(QStringLiteral("NDS")));
    QVERIFY(reg.forExtension(QStringLiteral(".nds")) == nds);
    QVERIFY(!reg.forExtension(QStringLiteral(".gba")));
    QVERIFY(nds->firmware.fileById(QStringLiteral("firmware")));
  }

  void coreProfilesAndAliases() {
    ManifestRegistry reg;
    QString err;
    QVERIFY2(reg.loadBuiltin(&err), qPrintable(err));
    const CoreProfile* mel = reg.profile(QStringLiteral("melondsds"));
    QVERIFY(mel);
    QCOMPARE(mel->libraryBasename, QStringLiteral("melondsds_libretro"));
    QVERIFY(reg.profile(QStringLiteral("melonds_ds")) == mel);  // legacy alias
    QCOMPARE(reg.canonicalCoreId(QStringLiteral("melonds_ds")), QStringLiteral("melondsds"));
    QCOMPARE(reg.canonicalCoreId(QStringLiteral("noods")), QStringLiteral("noods"));
    QCOMPARE(mel->coreOptions.value(QStringLiteral("melonds_render_mode")), QStringLiteral("software"));
    QVERIFY(mel->lockedCoreOptions.contains(QStringLiteral("melonds_screen_layout1")));
    QCOMPARE(mel->sysfileOption, QStringLiteral("melonds_sysfile_mode"));
    QCOMPARE(mel->fileOptions.value(QStringLiteral("firmware")), QStringLiteral("melonds_firmware_nds_path"));

    const CoreProfile* des = reg.profile(QStringLiteral("desmume"));
    QVERIFY(des);
    QCOMPARE(des->libraryBasename, QStringLiteral("desmume_libretro"));
    QCOMPARE(des->coreOptions.value(QStringLiteral("desmume_screens_layout")), QStringLiteral("top/bottom"));
    QCOMPARE(des->coreOptions.value(QStringLiteral("desmume_screens_gap")), QStringLiteral("0"));
    QCOMPARE(des->coreOptions.value(QStringLiteral("desmume_pointer_type")), QStringLiteral("touch"));
    QCOMPARE(des->saveSource, QStringLiteral("core_file"));
    QCOMPARE(des->saveExtension, QStringLiteral(".dsv"));
    QCOMPARE(des->saveFormat, QStringLiteral("desmume_dsv"));
    QCOMPARE(mel->saveSource, QStringLiteral("save_ram"));
    QCOMPARE(mel->saveExtension, QStringLiteral(".sav"));
    QCOMPARE(mel->saveFormat, QStringLiteral("raw"));
    QCOMPARE(des->sysfileOption, QStringLiteral("desmume_use_external_bios"));
    QCOMPARE(des->sysfileNative, QStringLiteral("enabled"));
    QCOMPARE(des->sysfileBuiltin, QStringLiteral("disabled"));

    QCOMPARE(reg.profilesForSystem(QStringLiteral("nds")).size(), 2);
    QVERIFY(reg.profilesForSystem(QStringLiteral("gba")).isEmpty());
  }

  void resolveProfiledCore() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const auto m = reg.resolve(QStringLiteral("nds"), QStringLiteral("melondsds"));
    QVERIFY(m);
    QVERIFY(!m->experimental);
    QCOMPARE(m->coreId, QStringLiteral("melondsds"));
    QCOMPARE(m->coreLibraryBasename, QStringLiteral("melondsds_libretro"));
    QCOMPARE(m->firmware.sysfileOption, QStringLiteral("melonds_sysfile_mode"));
    QCOMPARE(m->firmware.fileById(QStringLiteral("firmware"))->coreOption, QStringLiteral("melonds_firmware_nds_path"));
    QVERIFY(m->firmware.fileById(QStringLiteral("bios7"))->coreOption.isEmpty());
    QCOMPARE(m->display.frameSize(), QSize(256, 384));
    // A legacy id keeps the id the Hub serves, the profile is the same.
    const auto legacy = reg.resolve(QStringLiteral("nds"), QStringLiteral("melonds_ds"));
    QVERIFY(legacy);
    QCOMPARE(legacy->coreId, QStringLiteral("melonds_ds"));
    QVERIFY(!legacy->experimental);
    QVERIFY(legacy->coreAliases.contains(QStringLiteral("melondsds")));
    QCOMPARE(legacy->coreOptions, m->coreOptions);
    // DeSmuME composes the same 256x384 frame.
    const auto d = reg.resolve(QStringLiteral("nds"), QStringLiteral("desmume"));
    QVERIFY(d);
    QCOMPARE(d->display.frameSize(), QSize(256, 384));
    QCOMPARE(d->firmware.sysfileOption, QStringLiteral("desmume_use_external_bios"));
    QVERIFY(!reg.resolve(QStringLiteral("gba"), QStringLiteral("melondsds")));
  }

  void unknownCoreIsExperimental() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const auto m = reg.resolve(QStringLiteral("nds"), QStringLiteral("noods"));
    QVERIFY(m);
    QVERIFY(m->experimental);
    QCOMPARE(m->coreId, QStringLiteral("noods"));
    QCOMPARE(m->coreLibraryBasename, QStringLiteral("noods_libretro"));
    QCOMPARE(m->saveSource, QStringLiteral("auto"));  // SAVE_RAM when the core reports it, else the single file it writes
    QVERIFY(m->coreOptions.isEmpty());
    QVERIFY(m->lockedCoreOptions.isEmpty());
    QVERIFY(m->alwaysShownCoreOptions.isEmpty());
    QVERIFY(m->firmware.sysfileOption.isEmpty());
    for (const FirmwareFile& f : m->firmware.files) QVERIFY(f.coreOption.isEmpty());
    QCOMPARE(m->display.layout, QStringLiteral("single"));
    QVERIFY(m->display.screens.isEmpty());  // raw framebuffer as one screen
    QCOMPARE(m->display.touchScreenIndex(), -1);  // touch off
    QCOMPARE(m->extensions, reg.find(QStringLiteral("nds"))->extensions);
    // A profiled core of another system is experimental for this one.
    ManifestRegistry reg2;
    CoreProfile p;
    p.coreId = QStringLiteral("other-core");
    p.libraryBasename = QStringLiteral("other_libretro");
    p.systemIds = {QStringLiteral("gba")};
    QVERIFY(reg2.addProfile(p));
    QVERIFY(!reg2.addProfile(p));  // duplicate
  }

  void profileParsingIgnoresSourceAndRejectsInvalid() {
    QString err;
    const auto p = ManifestRegistry::parseProfile(
        R"({"_source":"x","core_id":"a-b","aliases":["a_b"],"library_basename":"a_b_libretro","system_ids":["nds"],
            "core_options":{"k":"v"},"firmware":{"sysfile_option":"o","file_options":{"firmware":"fo"}}})", &err);
    QVERIFY2(p, qPrintable(err));
    QCOMPARE(p->coreId, QStringLiteral("a-b"));
    QCOMPARE(p->sysfileNative, QStringLiteral("native"));
    QCOMPARE(p->fileOptions.value(QStringLiteral("firmware")), QStringLiteral("fo"));
    QVERIFY(!ManifestRegistry::parseProfile(R"({"core_id":"x","library_basename":"x","system_ids":["nds"],"save":{"source":"core_file","extension":"../x"}})", &err));
    QVERIFY(!ManifestRegistry::parseProfile("not json", &err));
    QVERIFY(!ManifestRegistry::parseProfile(R"({"core_id":"x","system_ids":["nds"]})", &err));
    QVERIFY(err.contains(QStringLiteral("library_basename")));
    QVERIFY(!ManifestRegistry::parseProfile(R"({"core_id":"x","library_basename":"x"})", &err));
  }

  void directoryMatchesBuiltin() {
    ManifestRegistry reg;
    QString err;
    QVERIFY2(reg.loadDirectory(QStringLiteral(FB_MANIFEST_SRC_DIR), &err), qPrintable(err));
    QVERIFY(reg.find(QStringLiteral("nds")));
    QVERIFY(reg.profile(QStringLiteral("desmume")));
    // Duplicate system_id is rejected.
    QVERIFY(!reg.loadDirectory(QStringLiteral(FB_MANIFEST_SRC_DIR), &err));
  }

  void rejectsInvalid() {
    QString err;
    QVERIFY(!ManifestRegistry::parse("not json", &err));
    QVERIFY(!ManifestRegistry::parse("{}", &err));
    QVERIFY(err.contains(QStringLiteral("system_id")));
    QVERIFY(!ManifestRegistry::parse(
        R"({"system_id":"x","display_name":"X","extensions":[],"display":{"screens":[{"width":1,"height":1}]}})", &err));
  }

  void coreLocator() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const SystemManifest nds = *reg.resolve(QStringLiteral("nds"), QStringLiteral("melondsds"));
    const QString envName = CoreLocator::environmentVariableFor(nds.coreId);
    QCOMPARE(envName, QStringLiteral("FRAMEBEAM_MELONDSDS_CORE"));
    QCOMPARE(CoreLocator::environmentVariableFor(QStringLiteral("a-b")), QStringLiteral("FRAMEBEAM_A_B_CORE"));
    const QByteArray savedEnv = qgetenv(envName.toUtf8().constData());
    qunsetenv(envName.toUtf8().constData());
    const QByteArray savedLegacy = qgetenv("FRAMEBEAM_MELONDS_DS_CORE");  // alias variable of the same core
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("cores")));
    auto touch = [](const QString& p) {
      QFile f(p);
      return f.open(QIODevice::WriteOnly) && f.write("dummy") > 0;
    };

    CoreLocator loc(dir.path());
    CoreLocation r = loc.locate(nds);
    QVERIFY(!r.found());
    QVERIFY(!r.tried.isEmpty());

    const QString appCore = dir.filePath(QStringLiteral("cores/") + CoreLocator::libraryFileName(nds.coreLibraryBasename));
    QVERIFY(touch(appCore));
    r = loc.locate(nds);
    QVERIFY(r.found());
    QCOMPARE(r.source, QStringLiteral("app-dir"));

    const QString envCore = dir.filePath(QStringLiteral("env_core.bin"));
    QVERIFY(touch(envCore));
    qputenv(envName.toUtf8().constData(), envCore.toUtf8());
    r = loc.locate(nds);
    QCOMPARE(r.source, QStringLiteral("env"));
    QCOMPARE(r.path, QDir::cleanPath(envCore));

    const QString explicitCore = dir.filePath(QStringLiteral("explicit_core.bin"));
    QVERIFY(touch(explicitCore));
    loc.setExplicitPath(nds.coreId, explicitCore);
    r = loc.locate(nds);
    QCOMPARE(r.source, QStringLiteral("explicit"));

    // An explicitly set but missing path falls back to env.
    loc.setExplicitPath(nds.coreId, dir.filePath(QStringLiteral("missing.bin")));
    QCOMPARE(loc.locate(nds).source, QStringLiteral("env"));

    // The legacy variable of the alias works for the same core.
    loc.setExplicitPath(nds.coreId, QString());
    qunsetenv(envName.toUtf8().constData());
    qputenv("FRAMEBEAM_MELONDS_DS_CORE", envCore.toUtf8());
    QCOMPARE(loc.locate(nds).source, QStringLiteral("env"));
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");

    if (savedEnv.isEmpty()) qunsetenv(envName.toUtf8().constData());
    else qputenv(envName.toUtf8().constData(), savedEnv);
    if (!savedLegacy.isEmpty()) qputenv("FRAMEBEAM_MELONDS_DS_CORE", savedLegacy);
  }
};

QTEST_GUILESS_MAIN(ManifestTest)
#include "manifest_test.moc"
