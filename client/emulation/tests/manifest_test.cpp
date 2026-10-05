// Manifest-Parsing und CoreLocator (ohne Core).
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
    QCOMPARE(nds->coreId, QStringLiteral("melonds_ds"));
    QCOMPARE(nds->coreLibraryBasename, QStringLiteral("melondsds_libretro"));
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
    QCOMPARE(nds->coreOptions.value(QStringLiteral("melonds_render_mode")), QStringLiteral("software"));
  }

  void directoryMatchesBuiltin() {
    ManifestRegistry reg;
    QString err;
    QVERIFY2(reg.loadDirectory(QStringLiteral(FB_MANIFEST_SRC_DIR), &err), qPrintable(err));
    QVERIFY(reg.find(QStringLiteral("nds")));
    // Doppelte system_id wird abgelehnt.
    QVERIFY(!reg.loadDirectory(QStringLiteral(FB_MANIFEST_SRC_DIR), &err));
  }

  void rejectsInvalid() {
    QString err;
    QVERIFY(!ManifestRegistry::parse("not json", &err));
    QVERIFY(!ManifestRegistry::parse("{}", &err));
    QVERIFY(err.contains(QStringLiteral("system_id")));
    QVERIFY(!ManifestRegistry::parse(
        R"({"system_id":"x","display_name":"X","core_id":"c","core_library_basename":"c","extensions":[],"display":{"screens":[{"width":1,"height":1}]}})",
        &err));
  }

  void coreLocator() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const SystemManifest& nds = *reg.find(QStringLiteral("nds"));
    const QString envName = CoreLocator::environmentVariableFor(nds.coreId);
    QCOMPARE(envName, QStringLiteral("FRAMEBEAM_MELONDS_DS_CORE"));
    const QByteArray savedEnv = qgetenv(envName.toUtf8().constData());
    qunsetenv(envName.toUtf8().constData());

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

    // Explizit gesetzter, aber fehlender Pfad faellt auf Env zurueck.
    loc.setExplicitPath(nds.coreId, dir.filePath(QStringLiteral("missing.bin")));
    QCOMPARE(loc.locate(nds).source, QStringLiteral("env"));

    if (savedEnv.isEmpty()) qunsetenv(envName.toUtf8().constData());
    else qputenv(envName.toUtf8().constData(), savedEnv);
  }
};

QTEST_GUILESS_MAIN(ManifestTest)
#include "manifest_test.moc"
