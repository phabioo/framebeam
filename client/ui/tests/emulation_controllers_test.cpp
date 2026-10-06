// Phase 5: Emulation page (3e), settings hierarchy at launch, Controllers page (3f) with SDL virtual joysticks.
// Offscreen, FakeHub, dummy bytes only. The tests with the real core QSKIP without FRAMEBEAM_MELONDS_DS_CORE.
#include <SDL3/SDL.h>

#include <QSignalSpy>
#include <QtTest>

#include "controllerprofiles.h"
#include "core_options.h"
#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;
using S = HubConnection::State;
using L = EmulationSettings::Level;

namespace {
constexpr quint32 kA = 1u << 8, kB = 1u << 0, kUp = 1u << 4;

emu::CoreOption option(const QString& key, const QString& desc, const QString& cat, const QStringList& values, const QString& def,
                       bool visible = true) {
  emu::CoreOption o;
  o.key = key;
  o.description = desc;
  o.info = desc + QStringLiteral(" (info)");
  o.categoryKey = cat;
  for (const QString& v : values) o.values.append({v, QString()});
  o.defaultValue = def;
  o.currentValue = def;
  o.visible = visible;
  return o;
}

// What a core like melonDS DS reports (V2 with categories): enough to exercise the page without the real core.
emu::CoreProbe fakeProbe() {
  emu::CoreProbe p;
  p.ok = true;
  p.info.name = QStringLiteral("melonDS DS");
  p.info.version = QStringLiteral("1.4.0");
  p.categories = {{QStringLiteral("audio"), QStringLiteral("Audio"), QString()}, {QStringLiteral("system"), QStringLiteral("System"), QString()}};
  p.options = {
      option(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("Interpolation"), QStringLiteral("audio"),
             {QStringLiteral("disabled"), QStringLiteral("linear"), QStringLiteral("cosine")}, QStringLiteral("disabled")),
      option(QStringLiteral("melonds_boot_mode"), QStringLiteral("Boot Mode"), QStringLiteral("system"),
             {QStringLiteral("direct"), QStringLiteral("native")}, QStringLiteral("direct")),
      option(QStringLiteral("melonds_render_mode"), QStringLiteral("Render Mode"), QStringLiteral("system"),
             {QStringLiteral("software"), QStringLiteral("opengl")}, QStringLiteral("software")),
      option(QStringLiteral("melonds_screen_layout1"), QStringLiteral("Layout #1"), QStringLiteral("system"),
             {QStringLiteral("top-bottom"), QStringLiteral("left-right")}, QStringLiteral("top-bottom")),
      option(QStringLiteral("melonds_sysfile_mode"), QStringLiteral("BIOS/Firmware Mode"), QStringLiteral("system"),
             {QStringLiteral("native"), QStringLiteral("builtin")}, QStringLiteral("native")),
      option(QStringLiteral("melonds_hidden_by_core"), QStringLiteral("Hidden"), QStringLiteral("system"),
             {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("a"), false),
  };
  return p;
}

QJsonObject fwFile(const QString& id, const QString& name, bool required, const QByteArray& content) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("display_name"), name},
          {QStringLiteral("required"), required},
          {QStringLiteral("present"), !content.isEmpty()},
          {QStringLiteral("size"), content.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(content.size())},
          {QStringLiteral("sha256"), content.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(uitest::sha256Hex(content))}};
}
QJsonObject ndsSystem(const QString& mode, const QJsonArray& files) {
  return {{QStringLiteral("systems"),
           QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("nds")},
                                  {QStringLiteral("display_name"), QStringLiteral("Nintendo DS")},
                                  {QStringLiteral("preferred_core_id"), QStringLiteral("melonds_ds")},
                                  {QStringLiteral("expected_core_version"), QStringLiteral("1.4.0")},
                                  {QStringLiteral("firmware_mode"), mode},
                                  {QStringLiteral("firmware"), files}}}}};
}

// SDL virtual gamepad (default mapping: buttons a b x y back guide start ls rs lb rb dpup dpdown dpleft dpright).
enum Btn { South = 0, East, West, North, Back, Guide, Start, LS, RS, LB, RB, DUp, DDown, DLeft, DRight };
struct VirtualPad {
  SDL_JoystickID id = 0;
  SDL_Joystick* js = nullptr;
  explicit VirtualPad(const char* name) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.name = name;
    desc.vendor_id = 0x1209;
    desc.product_id = 0x5100;
    id = SDL_AttachVirtualJoystick(&desc);
    if (id != 0) js = SDL_OpenJoystick(id);
  }
  ~VirtualPad() { detach(); }
  void detach() {
    if (js) SDL_CloseJoystick(js);
    js = nullptr;
    if (id != 0) SDL_DetachVirtualJoystick(id);
    id = 0;
  }
  void button(int b, bool down) { SDL_SetJoystickVirtualButton(js, b, down); }
};
}  // namespace

class EmulationControllersTest : public QObject {
  Q_OBJECT

  QByteArray savedCoreEnv_;

  static void pair(Harness& h, FakeHub& hub) {
    hub.decision = FakeHub::Decision::Approve;
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsPairing, 8000);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 8000);
  }
  static QString text(Harness& h, const char* name) {
    QQuickItem* it = h.item(name);
    return it ? it->property("text").toString() : QString();
  }
  static bool visible(Harness& h, const char* name) {
    QQuickItem* it = h.item(name);
    return it != nullptr && it->isVisible();
  }
  static QVariantMap deviceRow(Harness& h, const QString& kind) {
    for (const QVariant& v : h.controller->controllers()->devices()) {
      if (v.toMap().value(QStringLiteral("kind")).toString() == kind) return v.toMap();
    }
    return {};
  }
  static QVariantMap inputRow(Harness& h, const QString& input) {
    for (const QVariant& v : h.controller->controllers()->rows()) {
      if (v.toMap().value(QStringLiteral("input")).toString() == input) return v.toMap();
    }
    return {};
  }
  static void pick(Harness& h, const char* selectName, const QString& value) {
    QQuickItem* sel = h.item(selectName);
    QVERIFY(sel != nullptr);
    QVERIFY(QMetaObject::invokeMethod(sel, "picked", Q_ARG(QString, value)));
  }

 private slots:
  void initTestCase() {
    savedCoreEnv_ = qgetenv("FRAMEBEAM_MELONDS_DS_CORE");
    uitest::installWarningCounter();
  }
  void init() {
    uitest::warningCount() = 0;
    // "Found" core that is never loaded (the options are injected); the real-core tests set the real path themselves.
    qputenv("FRAMEBEAM_MELONDS_DS_CORE", QCoreApplication::applicationFilePath().toLocal8Bit());
  }
  void cleanup() {
    QCOMPARE(uitest::warningCount().load(), 0);
    if (savedCoreEnv_.isEmpty()) qunsetenv("FRAMEBEAM_MELONDS_DS_CORE"); else qputenv("FRAMEBEAM_MELONDS_DS_CORE", savedCoreEnv_);
  }

  // ------------------------------------------------------------------ Emulation (3e)

  void emulationPageListsOnlyReportedOptions() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), fakeProbe(), false);
    QVERIFY(h.click("navEmulation"));
    QCOMPARE(h.controller->screen(), QStringLiteral("emulation"));

    // System card: name, core + version, readiness, firmware (no firmware_v1 on this hub -> built-in BIOS).
    QCOMPARE(text(h, "systemCore_nds"), QStringLiteral("melonDS DS · 1.4.0"));
    QCOMPARE(text(h, "systemReady_nds"), QStringLiteral("Ready · included in the Player"));
    QCOMPARE(text(h, "systemFirmware_nds"), QStringLiteral("Built-in BIOS"));
    QCOMPARE(text(h, "emulationTitle"), QStringLiteral("Nintendo DS · melonDS DS"));
    QVERIFY(visible(h, "optionGroup_framebeam"));
    QVERIFY(visible(h, "optionGroup_core"));

    // FrameBeam group and the core's visible, non-locked options; nothing else.
    QVERIFY(h.item("optionRow_framebeam.fullscreen_on_start") != nullptr);
    QVERIFY(h.item("optionRow_framebeam.default_multiview") != nullptr);
    QVERIFY(h.item("optionRow_melonds_audio_interpolation") != nullptr);
    QVERIFY(h.item("optionRow_melonds_boot_mode") != nullptr);
    for (const char* hidden : {"optionRow_melonds_render_mode", "optionRow_melonds_screen_layout1", "optionRow_melonds_sysfile_mode",
                               "optionRow_melonds_hidden_by_core"}) {
      QVERIFY2(h.item(hidden) == nullptr, hidden);  // locked by FrameBeam / hidden by the core: not user-editable
    }
    QCOMPARE(h.controller->emulation()->lockedCount(), 3);
    QVERIFY(visible(h, "lockedHint"));
    // Level switch: Game Override is disabled ("later").
    QVERIFY(h.item("levelGame") != nullptr);
    QVERIFY(h.click("levelGame"));
    QCOMPARE(h.controller->emulation()->level(), QStringLiteral("system"));

    // Origins: inherited sources, no badge while no game runs.
    QCOMPARE(text(h, "optionOrigin_melonds_audio_interpolation"), QStringLiteral("inherited · core default"));
    QCOMPARE(text(h, "optionOrigin_melonds_boot_mode"), QStringLiteral("inherited · FrameBeam default"));  // manifest default
    QCOMPARE(text(h, "optionOrigin_framebeam.fullscreen_on_start"), QStringLiteral("inherited · default"));
    QVERIFY(!visible(h, "restartBadge_melonds_audio_interpolation"));
    uitest::saveShot(h.window, QStringLiteral("5-emulation"));

    // Set a value: stored as an explicit override, "● set here" + reset; reset removes the key again.
    pick(h, "optionSelect_melonds_audio_interpolation", QStringLiteral("cosine"));
    QCOMPARE(h.controller->emulation()->settings()->value(L::System, QStringLiteral("nds"), QStringLiteral("melonds_audio_interpolation")),
             QStringLiteral("cosine"));
    QCOMPARE(text(h, "optionOrigin_melonds_audio_interpolation"), QStringLiteral("● set here"));
    QVERIFY(visible(h, "optionReset_melonds_audio_interpolation"));
    QCOMPARE(h.controller->emulation()->launchOverrides(QStringLiteral("nds"), QStringLiteral("g1")).value(QStringLiteral("melonds_audio_interpolation")),
             QStringLiteral("cosine"));
    {
      QFile f(h.controller->emulation()->settings()->filePath());
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
      // Only the explicit override is stored (no complete copy of the configuration).
      QCOMPARE(o.value(QStringLiteral("systems")).toObject().value(QStringLiteral("nds")).toObject().value(QStringLiteral("options")).toObject().size(), 1);
      QVERIFY(!o.contains(QStringLiteral("global")));
    }
    // An invalid value and a locked key are refused.
    h.controller->emulation()->setOption(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("nonsense"));
    h.controller->emulation()->setOption(QStringLiteral("melonds_render_mode"), QStringLiteral("opengl"));
    QCOMPARE(h.controller->emulation()->settings()->values(L::System, QStringLiteral("nds")).size(), 1);
    QVERIFY(h.click("optionReset_melonds_audio_interpolation"));
    QVERIFY(!h.controller->emulation()->settings()->hasValue(L::System, QStringLiteral("nds"), QStringLiteral("melonds_audio_interpolation")));
    QCOMPARE(text(h, "optionOrigin_melonds_audio_interpolation"), QStringLiteral("inherited · core default"));

    // "Restart required" badge on core options only while a game runs (values apply at the next launch).
    h.controller->emulation()->setGameRunning(true);
    QVERIFY(visible(h, "restartBadge_melonds_audio_interpolation"));
    QVERIFY(!visible(h, "restartBadge_framebeam.fullscreen_on_start"));
    QVERIFY(visible(h, "gameRunningNote"));
    h.controller->emulation()->setGameRunning(false);
    QVERIFY(!visible(h, "restartBadge_melonds_audio_interpolation"));
  }

  void settingsHierarchyOnThePage() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), fakeProbe(), false);
    h.controller->showEmulation();
    EmulationController* emu = h.controller->emulation();

    // Global level: FrameBeam options only; core keys belong to System / Core.
    QVERIFY(h.click("levelGlobal"));
    QCOMPARE(emu->level(), QStringLiteral("global"));
    QVERIFY(h.item("optionGroup_core") == nullptr);
    QVERIFY(visible(h, "globalCoreHint"));
    QCOMPARE(text(h, "optionOrigin_framebeam.fullscreen_on_start"), QStringLiteral("default"));
    QVERIFY(!h.controller->fullscreenOnStart());
    pick(h, "optionSelect_framebeam.fullscreen_on_start", QStringLiteral("on"));
    QCOMPARE(text(h, "optionOrigin_framebeam.fullscreen_on_start"), QStringLiteral("● set here"));
    QVERIFY(h.controller->fullscreenOnStart());  // wired: the game view goes fullscreen on start

    // System level inherits it; an own value overrides it; reset falls back to Global.
    QVERIFY(h.click("levelSystem"));
    QCOMPARE(text(h, "optionOrigin_framebeam.fullscreen_on_start"), QStringLiteral("inherited · Global"));
    pick(h, "optionSelect_framebeam.fullscreen_on_start", QStringLiteral("off"));
    QVERIFY(!h.controller->fullscreenOnStart());
    QCOMPARE(emu->frameBeamValue(QStringLiteral("framebeam.fullscreen_on_start"), QStringLiteral("nds")), QStringLiteral("off"));
    QVERIFY(h.click("optionReset_framebeam.fullscreen_on_start"));
    QVERIFY(h.controller->fullscreenOnStart());
    QCOMPARE(text(h, "optionOrigin_framebeam.fullscreen_on_start"), QStringLiteral("inherited · Global"));

    // Default multiview is applied to the Session view.
    QCOMPARE(h.controller->sessions()->multiviewMode(), QStringLiteral("pip"));
    pick(h, "optionSelect_framebeam.default_multiview", QStringLiteral("side"));
    QCOMPARE(h.controller->sessions()->multiviewMode(), QStringLiteral("side"));

    // A core option set globally in the file (keys stay assigned to the core) is inherited at System level.
    emu->settings()->setValue(L::Global, QString(), QStringLiteral("melonds_boot_mode"), QStringLiteral("native"));
    emu->setLevel(QStringLiteral("global"));
    emu->setLevel(QStringLiteral("system"));
    QCOMPARE(text(h, "optionOrigin_melonds_boot_mode"), QStringLiteral("inherited · Global"));
    QCOMPARE(emu->launchOverrides(QStringLiteral("nds"), QString()).value(QStringLiteral("melonds_boot_mode")), QStringLiteral("native"));
  }

  void firmwareStateOnTheCard() {
    const QByteArray b7(16, '7'), b9(8, '9'), fw(32, 'f');
    struct Case { QString mode; QJsonArray files; QString expected; };
    const QList<Case> cases = {
        {QStringLiteral("builtin"), {}, QStringLiteral("Built-in BIOS")},
        {QStringLiteral("native"),
         QJsonArray{fwFile(QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), true, b7), fwFile(QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), true, {}),
                    fwFile(QStringLiteral("firmware"), QStringLiteral("DS Firmware"), true, fw)},
         QStringLiteral("Firmware required/missing")},
        {QStringLiteral("native"),
         QJsonArray{fwFile(QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), true, b7), fwFile(QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), true, b9),
                    fwFile(QStringLiteral("firmware"), QStringLiteral("DS Firmware"), true, fw)},
         QStringLiteral("Firmware from Hub · verified")},
    };
    for (const Case& c : cases) {
      FakeHub hub(QStringLiteral("a"));
      hub.features = {QStringLiteral("saves_v1"), QStringLiteral("firmware_v1")};
      hub.systems = ndsSystem(c.mode, c.files);
      QVERIFY(hub.start());
      Harness h;
      QVERIFY(h.start());
      pair(h, hub);
      QTRY_VERIFY_WITH_TIMEOUT(h.controller->hubSystems()->state() == HubSystems::State::Ready, 8000);
      h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), fakeProbe(), false);
      h.controller->showEmulation();
      QTRY_COMPARE(text(h, "systemFirmware_nds"), c.expected);
      if (c.expected.startsWith(QStringLiteral("Firmware required"))) {
        QCOMPARE(h.controller->emulation()->system().value(QStringLiteral("firmwareTone")).toString(), QStringLiteral("error"));
      }
    }
  }

  void coreMissingIsShown() {
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->showEmulation();
    QCOMPARE(text(h, "systemReady_nds"), QStringLiteral("Core missing"));
    // Without a core and without a cache the page still works and says why the core group is empty.
    QVERIFY(visible(h, "optionGroup_framebeam"));
    QVERIFY(!h.controller->emulation()->coreNote().isEmpty());
  }

  // Options are captured with the real core (loaded without a game) and cached for the next start.
  void realCoreOptionsAndCache() {
    const QString core = savedCoreEnv_;
    if (core.isEmpty()) QSKIP("FRAMEBEAM_MELONDS_DS_CORE not set");
    qputenv("FRAMEBEAM_MELONDS_DS_CORE", core.toLocal8Bit());
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start(true));  // probes the core like the handshake does
    pair(h, hub);
    h.controller->showEmulation();
    QVERIFY(h.controller->emulation()->hasCoreProbe(QStringLiteral("melonds_ds")));
    QCOMPARE(text(h, "systemCore_nds").left(10), QStringLiteral("melonDS DS"));
    QCOMPARE(text(h, "systemReady_nds"), QStringLiteral("Ready · included in the Player"));
    QVERIFY(h.item("optionRow_melonds_audio_interpolation") != nullptr);
    QVERIFY(h.item("optionRow_melonds_render_mode") == nullptr);
    QVERIFY(h.item("optionRow_melonds_sysfile_mode") == nullptr);
    QVERIFY(h.item("optionRow_melonds_firmware_nds_path") == nullptr);
    QVERIFY(h.item("optionRow_melonds_show_lid_state") == nullptr);
    const QString cache = QDir(h.controller->profileStore()->baseDir()).filePath(QStringLiteral("cache/core-options/melonds_ds.json"));
    QVERIFY(QFile::exists(cache));
    pick(h, "optionSelect_melonds_audio_interpolation", QStringLiteral("cubic"));
    QCOMPARE(text(h, "optionOrigin_melonds_audio_interpolation"), QStringLiteral("● set here"));

    // A fresh controller without core access reads the options from the cache.
    emu::ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    EmulationController cached(h.controller->profileStore()->baseDir(), &reg);
    cached.loadCoreCache(QStringLiteral("melonds_ds"));
    QVERIFY(cached.hasCoreProbe(QStringLiteral("melonds_ds")));
    QCOMPARE(cached.coreProbe(QStringLiteral("melonds_ds"))->options.size(), h.controller->emulation()->coreProbe(QStringLiteral("melonds_ds"))->options.size());
    QCOMPARE(cached.settings()->value(L::System, QStringLiteral("nds"), QStringLiteral("melonds_audio_interpolation")), QStringLiteral("cubic"));
  }

  // ------------------------------------------------------------------ Controllers (3f)

  void gamepadsDisabledGracefully() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;  // gamepads off (also what a failed SDL init looks like)
    QVERIFY(h.start());
    pair(h, hub);
    QVERIFY(!h.controller->controllers()->gamepadAvailable());
    QVERIFY(!hub.lastHandshakeBody.value(QStringLiteral("input")).toObject().value(QStringLiteral("gamepad")).toBool(true));
    QVERIFY(hub.lastHandshakeBody.value(QStringLiteral("input")).toObject().value(QStringLiteral("keyboard")).toBool());
    QVERIFY(h.click("navControllers"));
    QCOMPARE(h.controller->screen(), QStringLiteral("controllers"));
    QVERIFY(visible(h, "gamepadNote"));
    QVERIFY(!text(h, "gamepadNote").isEmpty());
    QCOMPARE(h.controller->controllers()->selectedDevice(), QStringLiteral("keyboard"));  // keyboard and mouse still work
    QCOMPARE(h.controller->controllers()->devices().size(), 2);
  }

  void controllersPageGamepadFlow() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    h.gamepads = true;
    QVERIFY(h.start());
    ControllersController* c = h.controller->controllers();
    QVERIFY2(c->gamepadAvailable(), qPrintable(c->gamepadNote()));
    pair(h, hub);
    QVERIFY(hub.lastHandshakeBody.value(QStringLiteral("input")).toObject().value(QStringLiteral("gamepad")).toBool());  // reflects availability

    QVERIFY(h.click("navControllers"));
    QCOMPARE(h.controller->screen(), QStringLiteral("controllers"));
    QCOMPARE(c->devices().size(), 2);  // Keyboard, Mouse
    QCOMPARE(c->selectedDevice(), QStringLiteral("keyboard"));
    QCOMPARE(text(h, "profilesLocalFooter"), QStringLiteral("Profiles stay local on this device and are not synchronized."));
    QCOMPARE(c->rows().size(), 12);
    QVERIFY(!c->supportsLid());  // no LID row: the nds profiles have no lid input
    QCOMPARE(inputRow(h, QStringLiteral("a")).value(QStringLiteral("binding")).toString(), QStringLiteral("X"));
    QCOMPARE(deviceRow(h, QStringLiteral("keyboard")).value(QStringLiteral("slot")).toString(), QStringLiteral("P1"));  // no pad yet

    // Hotplug: the first connected gamepad is P1, the keyboard gives up the slot.
    VirtualPad pad("FrameBeam Test Pad");
    QVERIFY(pad.js != nullptr);
    c->gamepads()->poll();
    const QVariantMap padRow = deviceRow(h, QStringLiteral("gamepad"));
    QCOMPARE(padRow.value(QStringLiteral("name")).toString(), QStringLiteral("FrameBeam Test Pad"));
    QCOMPARE(padRow.value(QStringLiteral("slot")).toString(), QStringLiteral("P1"));
    QCOMPARE(padRow.value(QStringLiteral("profile")).toString(), QStringLiteral("Standard Gamepad"));
    QCOMPARE(deviceRow(h, QStringLiteral("keyboard")).value(QStringLiteral("slot")).toString(), QStringLiteral("—"));
    QCOMPARE(deviceRow(h, QStringLiteral("mouse")).value(QStringLiteral("profile")).toString(), QStringLiteral("DS touch"));
    const QString padKey = padRow.value(QStringLiteral("key")).toString();
    QVERIFY(h.click(QByteArray("device_" + padKey.toUtf8()).constData()));
    QCOMPARE(c->selectedDevice(), padKey);
    QCOMPARE(text(h, "controllersTitle"), QStringLiteral("FrameBeam Test Pad"));
    QVERIFY(c->profileBuiltin());
    QVERIFY(visible(h, "builtinNote"));
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("B"));       // NDS A on the east button
    QCOMPARE(text(h, "mapText_up"), QStringLiteral("D-Pad ▲ / Left stick ▲"));
    // Built-in profiles are read-only.
    QVERIFY(h.click("mapField_a"));
    QVERIFY(c->listening().isEmpty());
    uitest::saveShot(h.window, QStringLiteral("5-controllers"));

    // Input test: live tiles and the merged joypad mask of the running game path.
    QVERIFY(h.item("testTile_a") != nullptr);
    QVERIFY(!h.item("testTile_a")->property("active").toBool());
    pad.button(East, true);
    c->gamepads()->poll();
    QVERIFY(h.item("testTile_a")->property("active").toBool());
    QVERIFY(!h.item("testTile_b")->property("active").toBool());
    QCOMPARE(h.controller->gameSession()->joypadMask(), kA);  // gamepad -> FrameBeam input -> joypad mask
    pad.button(DUp, true);
    c->gamepads()->poll();
    QVERIFY(h.item("testTile_up")->property("active").toBool());
    QCOMPARE(h.controller->gameSession()->joypadMask(), kA | kUp);
    // Keyboard and gamepad merge.
    QVERIFY(h.controller->gameSession()->keyEvent(Qt::Key_Z, true));
    QCOMPARE(h.controller->gameSession()->joypadMask(), kA | kUp | kB);
    h.controller->gameSession()->keyEvent(Qt::Key_Z, false);
    pad.button(East, false);
    pad.button(DUp, false);
    c->gamepads()->poll();
    QCOMPARE(h.controller->gameSession()->joypadMask(), 0u);

    // Duplicate -> own profile (assigned to this device), remap by "Press a button…".
    QVERIFY(h.click("duplicateProfileButton"));
    QVERIFY(!c->profileBuiltin());
    QCOMPARE(c->profileName(), QStringLiteral("Standard Gamepad copy"));
    QCOMPARE(deviceRow(h, QStringLiteral("gamepad")).value(QStringLiteral("profile")).toString(), QStringLiteral("Standard Gamepad copy"));
    QVERIFY(h.click("mapField_a"));
    QCOMPARE(c->listening(), QStringLiteral("a"));
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("Press a button…"));
    pad.button(LB, true);
    c->gamepads()->poll();
    QVERIFY(c->listening().isEmpty());
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("LB"));
    QCOMPARE(text(h, "mapText_l"), QStringLiteral("LT"));  // taken away from L (a token drives one input)
    pad.button(LB, false);
    c->gamepads()->poll();
    pad.button(LB, true);  // remapped profile drives the game
    c->gamepads()->poll();
    QCOMPARE(h.controller->gameSession()->joypadMask(), kA);
    pad.button(LB, false);
    pad.button(East, true);  // east was A, now it is nothing in this profile? (it stays on no input)
    c->gamepads()->poll();
    QCOMPARE(h.controller->gameSession()->joypadMask(), 0u);
    pad.button(East, false);
    c->gamepads()->poll();
    // Not mapped state, clear.
    QVERIFY(h.click("mapClear_start"));
    QCOMPARE(text(h, "mapText_start"), QStringLiteral("not mapped"));
    QVERIFY(!inputRow(h, QStringLiteral("start")).value(QStringLiteral("mapped")).toBool());
    // Escape cancels a capture on the keyboard only; for pads a second tap on the field cancels.
    QVERIFY(h.click("mapField_b"));
    QCOMPARE(c->listening(), QStringLiteral("b"));
    QVERIFY(h.click("mapField_b"));
    QVERIFY(c->listening().isEmpty());

    // Profile persisted locally in <data>/settings/controllers.json
    {
      ControllerProfiles reread(h.controller->profileStore()->baseDir());
      QCOMPARE(reread.all().size(), 3);
      QVERIFY(reread.filePath().endsWith(QStringLiteral("settings/controllers.json")));
    }
    // Reset to default restores the built-in bindings; rename; delete (two-step) falls back to the built-in profile.
    QVERIFY(h.click("resetProfileButton"));
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("B"));
    c->renameProfile(QStringLiteral("Couch pad"));
    QCOMPARE(c->profileName(), QStringLiteral("Couch pad"));
    QVERIFY(h.click("deleteProfileButton"));
    QCOMPARE(c->profileName(), QStringLiteral("Couch pad"));  // asked to confirm first
    QVERIFY(h.click("deleteProfileButton"));
    QCOMPARE(c->profileName(), QStringLiteral("Standard Gamepad"));
    QVERIFY(c->profileBuiltin());

    // Unplug: the device disappears and the selection falls back.
    pad.detach();
    c->gamepads()->poll();
    QVERIFY(deviceRow(h, QStringLiteral("gamepad")).isEmpty());
    QCOMPARE(c->selectedDevice(), QStringLiteral("keyboard"));
    QCOMPARE(deviceRow(h, QStringLiteral("keyboard")).value(QStringLiteral("slot")).toString(), QStringLiteral("P1"));
  }

  void controllersKeyboardProfileAndMouse() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ControllersController* c = h.controller->controllers();
    h.controller->showControllers();
    QCOMPARE(c->profileName(), QStringLiteral("Keyboard · Standard"));

    // Input test with the keyboard: X lights up A, the arrow lights up the D-pad.
    QTest::keyPress(h.window, Qt::Key_X);
    QVERIFY(h.item("testTile_a")->property("active").toBool());
    QTest::keyRelease(h.window, Qt::Key_X);
    QVERIFY(!h.item("testTile_a")->property("active").toBool());
    QTest::keyPress(h.window, Qt::Key_Left);
    QVERIFY(h.item("testTile_left")->property("active").toBool());
    QTest::keyRelease(h.window, Qt::Key_Left);

    // Duplicate, remap A to J (keyboard capture), Escape cancels a capture.
    QVERIFY(h.click("duplicateProfileButton"));
    QVERIFY(h.click("mapField_a"));
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("Press a key…"));
    QTest::keyClick(h.window, Qt::Key_Escape);
    QVERIFY(c->listening().isEmpty());
    QVERIFY(h.click("mapField_a"));
    QTest::keyClick(h.window, Qt::Key_J);
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("J"));
    // Taking X away from nothing else; X is now unmapped, J is A in the game and in the test panel.
    GameSession* s = h.controller->gameSession();
    QVERIFY(!s->keyEvent(Qt::Key_X, true));
    QVERIFY(s->keyEvent(Qt::Key_J, true));
    QCOMPARE(s->joypadMask(), kA);
    s->keyEvent(Qt::Key_J, false);
    QCOMPARE(s->joypadMask(), 0u);
    QVERIFY(s->keyEvent(Qt::Key_Up, true));  // untouched keys keep working
    QCOMPARE(s->joypadMask(), kUp);
    s->keyEvent(Qt::Key_Up, false);
    QTest::keyPress(h.window, Qt::Key_J);
    QVERIFY(h.item("testTile_a")->property("active").toBool());
    QTest::keyRelease(h.window, Qt::Key_J);
    // Built-in keyboard profile is untouched (read-only).
    c->selectProfile(QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("X"));
    QVERIFY(visible(h, "builtinNote"));
    QTest::keyClick(h.window, Qt::Key_Q);  // not capturing: only feeds the test

    // Mouse = DS touch: fixed, no profile, no mapping table.
    QVERIFY(h.click("device_mouse"));
    QCOMPARE(c->rows().size(), 0);
    QVERIFY(visible(h, "mouseNote"));
    QVERIFY(!visible(h, "profileSelect"));
    QVERIFY(c->activeInputs().isEmpty());
  }

  // Both pages in the light palette (tokens only, no hardcoded colors) load without QML warnings.
  void pagesInLightAndDark() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), fakeProbe(), false);
    for (const QString& mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      h.controller->setAppearance(mode);
      h.controller->showEmulation();
      QTest::qWait(50);
      uitest::saveShot(h.window, QStringLiteral("5-emulation-") + mode);
      h.controller->showControllers();
      QTest::qWait(50);
      uitest::saveShot(h.window, QStringLiteral("5-controllers-") + mode);
    }
  }
};

UITEST_MAIN(EmulationControllersTest)
#include "emulation_controllers_test.moc"
