// Launch options: manifest defaults < user overrides < firmware options < manifest-locked values (no core needed).
#include <QtTest>

#include "core_options.h"
#include "system_manifest.h"

using namespace framebeam::emu;

class LaunchOptionsTest : public QObject {
  Q_OBJECT
  SystemManifest nds() {
    ManifestRegistry reg;
    reg.loadBuiltin();
    return *reg.find(QStringLiteral("nds"));
  }

 private slots:
  void lockedOptionsStayUnderFrameBeamControl() {
    const SystemManifest m = nds();
    for (const char* k : {"melonds_screen_layout1", "melonds_show_current_layout", "melonds_show_lid_state",
                          "melonds_sysfile_mode", "melonds_firmware_nds_path"}) {
      QVERIFY2(isLockedCoreOption(m, QString::fromLatin1(k)), k);
    }
    QVERIFY(!isLockedCoreOption(m, QStringLiteral("melonds_audio_interpolation")));
    QVERIFY(!isLockedCoreOption(m, QStringLiteral("melonds_render_mode")));  // user choice since 0.5 (default software)
    QVERIFY(m.alwaysShownCoreOptions.contains(QStringLiteral("melonds_opengl_resolution")));
    QVERIFY(!isLockedCoreOption(m, QStringLiteral("melonds_boot_mode")));  // manifest default, user may change it

    // Overrides of locked keys never reach the core; the firmware mode comes from the Hub.
    const QMap<QString, QString> user = {{QStringLiteral("melonds_render_mode"), QStringLiteral("opengl")},
                                         {QStringLiteral("melonds_screen_layout1"), QStringLiteral("left-right")},
                                         {QStringLiteral("melonds_show_lid_state"), QStringLiteral("enabled")},
                                         {QStringLiteral("melonds_sysfile_mode"), QStringLiteral("native")},
                                         {QStringLiteral("melonds_audio_interpolation"), QStringLiteral("cubic")},
                                         {QStringLiteral("melonds_boot_mode"), QStringLiteral("native")},
                                         {QStringLiteral("framebeam.fullscreen_on_start"), QStringLiteral("on")}};
    const QMap<QString, QString> fw = {{QStringLiteral("melonds_sysfile_mode"), QStringLiteral("builtin")}};
    const QMap<QString, QString> out = launchCoreOptions(m, user, fw);
    QCOMPARE(out.value(QStringLiteral("melonds_render_mode")), QStringLiteral("opengl"));  // user override applies
    QCOMPARE(out.value(QStringLiteral("melonds_screen_layout1")), QStringLiteral("top-bottom"));
    QCOMPARE(out.value(QStringLiteral("melonds_show_lid_state")), QStringLiteral("disabled"));
    QCOMPARE(out.value(QStringLiteral("melonds_sysfile_mode")), QStringLiteral("builtin"));
    QCOMPARE(out.value(QStringLiteral("melonds_audio_interpolation")), QStringLiteral("cubic"));
    QCOMPARE(out.value(QStringLiteral("melonds_boot_mode")), QStringLiteral("native"));  // user > manifest default
    QVERIFY(!out.contains(QStringLiteral("framebeam.fullscreen_on_start")));
  }

  void withoutOverridesTheManifestDefaultsApply() {
    const SystemManifest m = nds();
    QCOMPARE(launchCoreOptions(m, {}, {}), m.coreOptions);
  }
};

QTEST_APPLESS_MAIN(LaunchOptionsTest)
#include "launch_options_test.moc"
