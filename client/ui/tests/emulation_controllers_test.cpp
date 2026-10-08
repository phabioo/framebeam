// Emulation page (3e), settings hierarchy at launch, Controllers page (3f) with SDL virtual joysticks.
// Offscreen, FakeHub, dummy bytes only. The tests with the real core QSKIP without FRAMEBEAM_MELONDS_DS_CORE.
#include <SDL3/SDL.h>

#include <QQmlContext>
#include <QQmlProperty>
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
      option(QStringLiteral("melonds_opengl_resolution"), QStringLiteral("Internal Resolution"), QStringLiteral("system"),
             {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")}, QStringLiteral("1"), false),
      option(QStringLiteral("melonds_screen_layout1"), QStringLiteral("Layout #1"), QStringLiteral("system"),
             {QStringLiteral("top-bottom"), QStringLiteral("left-right")}, QStringLiteral("top-bottom")),
      option(QStringLiteral("melonds_sysfile_mode"), QStringLiteral("BIOS/Firmware Mode"), QStringLiteral("system"),
             {QStringLiteral("native"), QStringLiteral("builtin")}, QStringLiteral("native")),
      option(QStringLiteral("melonds_hidden_by_core"), QStringLiteral("Hidden"), QStringLiteral("system"),
             {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("a"), false),
  };
  return p;
}

// A core with many categories and every control type: toggle, segment, select (6 values), description with an option list.
emu::CoreProbe richProbe() {
  emu::CoreProbe p;
  p.ok = true;
  p.info.name = QStringLiteral("melonDS DS");
  p.info.version = QStringLiteral("1.4.0");
  const QStringList cats = {QStringLiteral("System"), QStringLiteral("Video"), QStringLiteral("Audio"), QStringLiteral("Screen"),
                            QStringLiteral("Firmware"), QStringLiteral("Network"), QStringLiteral("Date/Time"), QStringLiteral("Input Devices")};
  for (const QString& c : cats) p.categories.append({QString(c.toLower()).replace(QLatin1Char('/'), QLatin1Char('_')), c, QString()});
  p.options = {
      option(QStringLiteral("rich_boot"), QStringLiteral("Boot Mode"), QStringLiteral("system"), {QStringLiteral("direct"), QStringLiteral("native")}, QStringLiteral("direct")),
      option(QStringLiteral("rich_threaded"), QStringLiteral("Threaded Software Renderer"), QStringLiteral("video"),
             {QStringLiteral("disabled"), QStringLiteral("enabled")}, QStringLiteral("enabled")),
      option(QStringLiteral("rich_res"), QStringLiteral("Internal Resolution"), QStringLiteral("video"),
             {QStringLiteral("1x native (256 x 192)"), QStringLiteral("2x native (512 x 384)"), QStringLiteral("3x native (768 x 576)"),
              QStringLiteral("4x native (1024 x 768)"), QStringLiteral("5x native (1280 x 960)"), QStringLiteral("6x native (1536 x 1152)")},
             QStringLiteral("1x native (256 x 192)")),
      option(QStringLiteral("rich_interp"), QStringLiteral("Interpolation"), QStringLiteral("audio"),
             {QStringLiteral("None"), QStringLiteral("Linear"), QStringLiteral("Cosine"), QStringLiteral("Cubic")}, QStringLiteral("None")),
      option(QStringLiteral("rich_mic"), QStringLiteral("Microphone Input Mode"), QStringLiteral("audio"),
             {QStringLiteral("silence"), QStringLiteral("noise"), QStringLiteral("host")}, QStringLiteral("silence")),
      option(QStringLiteral("rich_screen"), QStringLiteral("Screen Gap"), QStringLiteral("screen"), {QStringLiteral("0"), QStringLiteral("1")}, QStringLiteral("0")),
      option(QStringLiteral("rich_fw"), QStringLiteral("Firmware Source"), QStringLiteral("firmware"), {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("a")),
      option(QStringLiteral("rich_net"), QStringLiteral("Network Mode"), QStringLiteral("network"), {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("a")),
      option(QStringLiteral("rich_time"), QStringLiteral("Time Mode"), QStringLiteral("date_time"), {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("a")),
      option(QStringLiteral("rich_input"), QStringLiteral("Cursor Mode"), QStringLiteral("input devices"), {QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("a")),
  };
  p.options[4].info = QStringLiteral("Select the microphone input.\n- Silence: No input.\n- White noise: Random noise.\n- Host: Use the host microphone.");
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
  // One line per item for the CI log: class, objectName, width, implicitWidth, attached Layout.min/pref/max.
  static QString layoutDump(QQuickItem* it, int depth) {
    const auto lay = [it](const char* p) {
      QQmlContext* ctx = qmlContext(it);
      const QVariant v = ctx ? QQmlProperty(it, QString::fromLatin1(p), ctx).read() : QVariant();
      return v.isValid() ? QString::number(v.toDouble()) : QStringLiteral("?");
    };
    QString s = QStringLiteral("\n%1%2 '%3' w=%4 impl=%5 min=%6 pref=%7 max=%8")
                    .arg(QString(depth * 2, QLatin1Char(' ')), QString::fromLatin1(it->metaObject()->className()), it->objectName())
                    .arg(it->width()).arg(it->implicitWidth())
                    .arg(lay("Layout.minimumWidth"), lay("Layout.preferredWidth"), lay("Layout.maximumWidth"));
    return s;
  }
  static QString hotkeysLayoutDump(Harness& h) {
    QQuickItem* info = h.item("hotkeysInfo");
    if (!info || !info->parentItem()) return QStringLiteral("(no hotkeysInfo)");
    QQuickItem* table = info->parentItem();
    QString s = QStringLiteral("Hotkeys layout:");
    if (table->parentItem()) {
      s += layoutDump(table->parentItem(), 0);  // content
      for (QQuickItem* sibling : table->parentItem()->childItems()) {
        if (sibling != table && sibling->isVisible()) s += layoutDump(sibling, 1);
      }
    }
    s += layoutDump(table, 1);
    for (QQuickItem* child : table->childItems()) {
      s += layoutDump(child, 2);
      for (QQuickItem* rowBox : child->childItems()) {  // delegate column: row layout, conflict line, divider
        s += layoutDump(rowBox, 3);
        if (rowBox->objectName().isEmpty() && rowBox->inherits("QQuickRowLayout")) {
          for (QQuickItem* cell : rowBox->childItems()) {
            s += layoutDump(cell, 4);
            for (QQuickItem* inner : cell->childItems()) s += layoutDump(inner, 5);
          }
        }
      }
    }
    return s;
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
    EmulationController* emu = h.controller->emulation();

    // System item: name, core + version, readiness, firmware (no firmware_v1 on this hub -> built-in BIOS).
    QCOMPARE(text(h, "systemCore_nds"), QStringLiteral("melonDS DS · 1.4.0"));
    QCOMPARE(text(h, "systemReady_nds"), QStringLiteral("Ready · included in the Player"));
    QCOMPARE(text(h, "systemFirmware_nds"), QStringLiteral("Built-in BIOS"));
    QCOMPARE(text(h, "emulationTitle"), QStringLiteral("Nintendo DS · melonDS DS 1.4.0"));
    QVERIFY(!emu->defaultsSelected());

    // The system shows the core's visible, non-locked options; nothing else, and no FrameBeam options (those are Defaults).
    QVERIFY(visible(h, "optionGroup_core"));
    QVERIFY(h.item("optionSelect_framebeam.default_multiview") == nullptr);  // display options are global only (speed-up options are offered per system)
    QVERIFY(h.item("optionRow_melonds_audio_interpolation") != nullptr);
    QVERIFY(h.item("optionRow_melonds_boot_mode") != nullptr);
    for (const char* hidden : {"optionRow_melonds_screen_layout1", "optionRow_melonds_sysfile_mode",
                               "optionRow_melonds_hidden_by_core"}) {
      QVERIFY2(h.item(hidden) == nullptr, hidden);  // locked by FrameBeam / hidden by the core: not user-editable
    }
    QVERIFY(h.item("optionRow_melonds_render_mode") != nullptr);  // user choice since 0.5
    QVERIFY(h.item("optionRow_melonds_opengl_resolution") != nullptr);  // hidden by the core in software mode, the manifest shows it anyway
    QCOMPARE(emu->lockedCount(), 2);
    QVERIFY(visible(h, "lockedHint"));
    // Category chips: the categories of this scope.
    QCOMPARE(emu->categories(), (QStringList{QStringLiteral("Audio"), QStringLiteral("System")}));
    QVERIFY(visible(h, "category_Audio") && visible(h, "category_System"));
    // Nothing changed yet: "Default" in every row, no dot, no "Reset N changed".
    QCOMPARE(text(h, "optionOrigin_melonds_audio_interpolation"), QStringLiteral("Default"));
    QVERIFY(!visible(h, "changedDot_melonds_audio_interpolation"));
    QVERIFY(!visible(h, "resetAllChanged"));
    QVERIFY(!visible(h, "restartBadge_melonds_audio_interpolation"));
    uitest::saveShot(h.window, QStringLiteral("5-emulation"));

    // Category chip and search narrow the list.
    QVERIFY(h.click("category_Audio"));
    QVERIFY(h.item("optionRow_melonds_boot_mode") == nullptr);
    QVERIFY(h.item("optionRow_melonds_audio_interpolation") != nullptr);
    QVERIFY(h.click("categoryAll"));
    QVERIFY(h.item("optionRow_melonds_boot_mode") != nullptr);
    emu->setSearchText(QStringLiteral("boot"));
    QVERIFY(h.item("optionRow_melonds_audio_interpolation") == nullptr);
    QVERIFY(h.item("optionRow_melonds_boot_mode") != nullptr);
    emu->setSearchText(QString());

    // Set a value: stored as an explicit override with the changed dot, "N changed" and Reset; reset removes the key again.
    pick(h, "optionSelect_melonds_audio_interpolation", QStringLiteral("cosine"));
    QCOMPARE(emu->settings()->value(L::System, QStringLiteral("nds"), QStringLiteral("melonds_audio_interpolation")), QStringLiteral("cosine"));
    QVERIFY(visible(h, "changedDot_melonds_audio_interpolation"));
    QVERIFY(visible(h, "optionReset_melonds_audio_interpolation"));
    QCOMPARE(emu->changedCount(), 1);
    QCOMPARE(text(h, "resetAllChanged"), QStringLiteral("Reset 1 changed"));
    QCOMPARE(text(h, "systemChanged_nds"), QStringLiteral("1 changed"));
    QCOMPARE(emu->launchOverrides(QStringLiteral("nds"), QStringLiteral("g1")).value(QStringLiteral("melonds_audio_interpolation")),
             QStringLiteral("cosine"));
    {
      QFile f(emu->settings()->filePath());
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
      // Only the explicit override is stored (no complete copy of the configuration).
      QCOMPARE(o.value(QStringLiteral("systems")).toObject().value(QStringLiteral("nds")).toObject().value(QStringLiteral("options")).toObject().size(), 1);
      QVERIFY(!o.contains(QStringLiteral("global")));
    }
    // An invalid value and a locked key are refused.
    emu->setOption(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("nonsense"));
    emu->setOption(QStringLiteral("melonds_screen_layout1"), QStringLiteral("left-right"));
    QCOMPARE(emu->settings()->values(L::System, QStringLiteral("nds")).size(), 1);
    QVERIFY(h.click("optionReset_melonds_audio_interpolation"));
    QVERIFY(!emu->settings()->hasValue(L::System, QStringLiteral("nds"), QStringLiteral("melonds_audio_interpolation")));
    QCOMPARE(text(h, "optionOrigin_melonds_audio_interpolation"), QStringLiteral("Default"));
    // Picking the default again stores nothing.
    emu->setOption(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("cosine"));
    emu->setOption(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("disabled"));
    QVERIFY(!emu->settings()->hasValue(L::System, QStringLiteral("nds"), QStringLiteral("melonds_audio_interpolation")));
    // "Reset N changed" resets every explicit value of the scope.
    emu->setOption(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("linear"));
    emu->setOption(QStringLiteral("melonds_boot_mode"), QStringLiteral("native"));
    QCOMPARE(emu->changedCount(), 2);
    QVERIFY(h.click("resetAllChanged"));
    QCOMPARE(emu->changedCount(), 0);
    QCOMPARE(emu->settings()->values(L::System, QStringLiteral("nds")).size(), 0);

    // "applies on next start" badge on core options only while a game runs; the hint appears once such an option changed.
    emu->setGameRunning(true);
    QVERIFY(visible(h, "restartBadge_melonds_audio_interpolation"));
    QVERIFY(visible(h, "gameRunningNote"));
    QVERIFY(!visible(h, "restartHint"));
    emu->setOption(QStringLiteral("melonds_audio_interpolation"), QStringLiteral("linear"));
    QVERIFY(visible(h, "restartHint"));
    emu->setGameRunning(false);
    QVERIFY(!visible(h, "restartBadge_melonds_audio_interpolation"));
    QVERIFY(!visible(h, "restartHint"));
  }

  // ------------------------------------------------------------------ SettingsRow on the Emulation and Settings pages

  static QList<QQuickItem*> settingsRows(QQuickItem* root) {
    QList<QQuickItem*> out;
    for (QQuickItem* c : root->childItems()) {
      if (c->property("controlColumn").isValid() && c->isVisible()) out.append(c);
      out.append(settingsRows(c));
    }
    return out;
  }
  static QRectF sceneRect(QQuickItem* it) { return it->mapRectToScene(QRectF(0, 0, it->width(), it->height())); }
  static QQuickItem* rowItem(QQuickItem* row, const char* prop) { return row->property(prop).value<QQuickItem*>(); }
  // Every element of every row lies inside the visible column (Windows layout rule: nothing widens a column).
  static void verifyRowsInside(Harness& h, const char* scrollName, int windowWidth) {
    QQuickItem* flick = h.item(scrollName);
    QVERIFY(flick != nullptr);
    const QRectF col = sceneRect(flick);
    const QList<QQuickItem*> rows = settingsRows(flick->property("contentItem").value<QQuickItem*>());
    QVERIFY(!rows.isEmpty());
    for (QQuickItem* row : rows) {
      for (const char* prop : {"controlColumn", "resetColumn", "textColumn", "labelItem"}) {
        QQuickItem* it = rowItem(row, prop);
        QVERIFY(it != nullptr);
        const QRectF r = sceneRect(it);
        QVERIFY2(r.left() >= col.left() - 0.5 && r.right() <= col.right() + 0.5,
                 qPrintable(QStringLiteral("%1.%2 at %3 px: %4..%5 outside the column %6..%7")
                                .arg(row->objectName(), QString::fromLatin1(prop)).arg(windowWidth).arg(r.left()).arg(r.right()).arg(col.left()).arg(col.right())));
      }
      QQuickItem* ctl = rowItem(row, "controlLoader")->property("item").value<QQuickItem*>();
      if (ctl != nullptr) {
        const QRectF r = sceneRect(ctl);
        QVERIFY2(r.left() >= col.left() - 0.5 && r.right() <= col.right() + 0.5, qPrintable(row->objectName()));
      }
    }
  }
  static qreal controlRight(QQuickItem* row) { return sceneRect(rowItem(row, "controlColumn")).right(); }

  void emulationRowsAlignAndStayInsideTheColumn() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), richProbe(), false);
    h.controller->showEmulation();
    QTest::qWait(100);
    qreal emuRight = 0;
    for (const int w : {1280, 1440, 960}) {
      h.window->resize(w, 800);
      QTest::qWait(120);
      QQuickTest::qWaitForPolish(h.window);
      QQuickItem* boot = h.item("optionRow_rich_boot");        // segment
      QQuickItem* thr = h.item("optionRow_rich_threaded");     // toggle
      QQuickItem* res = h.item("optionRow_rich_res");          // select
      QQuickItem* interp = h.item("optionRow_rich_interp");    // segment, 4 options
      QVERIFY(boot && thr && res && interp);
      QCOMPARE(boot->property("kind").toString(), QStringLiteral("segment"));
      QCOMPARE(thr->property("kind").toString(), QStringLiteral("toggle"));
      QCOMPARE(res->property("kind").toString(), QStringLiteral("select"));
      verifyRowsInside(h, "emulationScroll", w);
      if (w >= 1280) {
        QCOMPARE(controlRight(thr), controlRight(boot));
        QCOMPARE(controlRight(res), controlRight(boot));
        QCOMPARE(controlRight(interp), controlRight(boot));
        QCOMPARE(sceneRect(h.item("optionSelect_rich_boot")).right(), controlRight(boot));
        QCOMPARE(sceneRect(h.item("optionSelect_rich_res")).left(), sceneRect(h.item("optionSelect_rich_interp")).left());
        QCOMPARE(sceneRect(h.item("optionSelect_rich_res")).width(), 280.0);
        QCOMPARE(sceneRect(h.item("optionSelect_rich_res")).height(), 32.0);
        QCOMPARE(sceneRect(h.item("optionToggle_rich_threaded")).right(), controlRight(boot));
        // The changed dot never moves the label.
        const qreal labelX = sceneRect(rowItem(boot, "labelItem")).left();
        QCOMPARE(sceneRect(rowItem(thr, "labelItem")).left(), labelX);
        const qreal thrRight = controlRight(thr);
        h.controller->emulation()->setOption(QStringLiteral("rich_boot"), QStringLiteral("native"));  // rebuilds the rows
        QTest::qWait(60);
        QQuickItem* boot2 = h.item("optionRow_rich_boot");
        QVERIFY(visible(h, "changedDot_rich_boot"));
        QCOMPARE(sceneRect(rowItem(boot2, "labelItem")).left(), labelX);
        QCOMPARE(controlRight(boot2), thrRight);
        h.controller->emulation()->resetOption(QStringLiteral("rich_boot"));
        QTest::qWait(60);
        if (w == 1280) emuRight = thrRight;
      }
    }
    h.window->resize(1280, 800);
    QTest::qWait(100);
    QCOMPARE(controlRight(h.item("optionRow_rich_threaded")), emuRight);
  }

  void emulationCategoryChipsCollapse() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), richProbe(), false);
    h.controller->showEmulation();
    h.window->resize(960, 800);
    QTest::qWait(150);
    QQuickTest::qWaitForPolish(h.window);
    QQuickItem* bar = h.item("categoryChips");
    QVERIFY(bar != nullptr);
    QVERIFY(visible(h, "categoryMore"));
    QVERIFY(text(h, "categoryMore").startsWith(QLatin1Char('+')) && text(h, "categoryMore").contains(QStringLiteral("more")));
    QQuickItem* flick = h.item("emulationScroll");
    QVERIFY(sceneRect(h.item("categoryMore")).right() <= sceneRect(bar).right() + 0.5);
    QVERIFY(sceneRect(bar).right() <= sceneRect(flick).right() + 0.5);
    QVERIFY(visible(h, "categoryAll"));
    // The hidden categories are in the menu; picking one filters and keeps the selection reachable.
    QVERIFY(h.click("categoryMore"));
    QTest::qWait(80);
    QQuickItem* last = h.item("categoryOverflow_Input Devices");
    QVERIFY(last != nullptr && last->isVisible());
    QVERIFY(h.click("categoryOverflow_Input Devices"));
    QTest::qWait(80);
    QCOMPARE(h.controller->emulation()->categoryFilter(), QStringLiteral("Input Devices"));
    QVERIFY(h.item("optionRow_rich_input") != nullptr);
    QVERIFY(h.item("optionRow_rich_boot") == nullptr);
    QVERIFY(h.click("categoryAll"));
    // Wide enough: all chips are shown and there is no "+N more".
    h.window->resize(1920, 800);
    QTest::qWait(150);
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(!visible(h, "categoryMore"));
    QVERIFY(visible(h, "category_Input Devices"));
  }

  void emulationDescriptionOptionList() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), richProbe(), false);
    h.controller->showEmulation();
    QTest::qWait(120);
    QQuickItem* row = h.item("optionRow_rich_mic");
    QVERIFY(row != nullptr);
    QCOMPARE(row->property("moreLabel").toString(), QStringLiteral("More · 3 options"));
    QVERIFY(!visible(h, "optionList_rich_mic"));
    QVERIFY(h.click("descMore_rich_mic"));
    QTest::qWait(60);
    QVERIFY(visible(h, "optionList_rich_mic"));
    QVERIFY(h.click("descMore_rich_mic"));
    QVERIFY(!visible(h, "optionList_rich_mic"));
  }

  void settingsRowsAlignWithEmulation() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->emulation()->setCoreProbe(QStringLiteral("melonds_ds"), richProbe(), false);
    for (const int w : {1280, 1440, 960}) {
      h.window->resize(w, 800);
      h.controller->showEmulation();
      QTest::qWait(150);
      QQuickTest::qWaitForPolish(h.window);
      qreal emuRight = 0;
      if (w >= 1280) {
        QQuickItem* thr = h.item("optionRow_rich_threaded");
        QVERIFY(thr != nullptr);
        emuRight = controlRight(thr);
      }
      h.controller->showSettings();
      QTest::qWait(150);
      QQuickTest::qWaitForPolish(h.window);
      QCOMPARE(h.controller->screen(), QStringLiteral("settings"));
      verifyRowsInside(h, "settingsScroll", w);
      // The inline meta ("Channel: beta") keeps its natural width (never clipped to "Channel: o").
      for (QQuickItem* it : h.items("updateChannelRow")) {
        if (!it->isVisible()) continue;
        QList<QQuickItem*> st{it};
        int found = 0;
        while (!st.isEmpty()) {
          QQuickItem* c = st.takeLast();
          st.append(c->childItems());
          if (c->isVisible() && c->property("text").toString().startsWith(QStringLiteral("Channel:"))) {
            QVERIFY(c->parentItem()->width() + 0.5 >= c->implicitWidth());
            ++found;
          }
        }
        QCOMPARE(found, 1);
      }
      QQuickItem* flick = h.item("settingsScroll");
      const QRectF col = sceneRect(flick);
      // Hub card and update card are indented 14 to the label edge and stay inside the column.
      QQuickItem* card = h.item("updateCard");
      QVERIFY(card != nullptr);
      QVERIFY(sceneRect(card).left() >= col.left() + 36 + 14 - 0.5);
      QVERIFY(sceneRect(card).right() <= col.right() + 0.5);
      QQuickItem* autoRow = h.item("autoConnectRow");
      QQuickItem* themeRow = h.item("themeRow");
      QQuickItem* channelRow = h.item("updateChannelRow");
      QQuickItem* versionRow = h.item("versionRow");
      QQuickItem* logRow = h.item("logFileRow");
      QQuickItem* autoInstall = h.item("updateAutoInstallRow");
      QVERIFY(autoRow && themeRow && channelRow && versionRow && logRow && autoInstall);
      QCOMPARE(autoRow->property("kind").toString(), QStringLiteral("toggle"));
      QCOMPARE(themeRow->property("kind").toString(), QStringLiteral("segment"));
      QCOMPARE(versionRow->property("kind").toString(), QStringLiteral("value"));
      QVERIFY(sceneRect(rowItem(autoRow, "controlColumn")).left() >= col.left());
      // The reset column stays empty on Settings.
      QVERIFY(!rowItem(themeRow, "resetColumn")->isVisible());
      if (w >= 1280) {
        for (QQuickItem* r : {autoRow, themeRow, channelRow, versionRow, logRow, autoInstall}) QCOMPARE(controlRight(r), emuRight);
        QCOMPARE(sceneRect(h.item("settingsAutoConnectToggle")).right(), emuRight);
        QCOMPARE(sceneRect(h.item("appearanceSegment")).right(), emuRight);
        QCOMPARE(sceneRect(h.item("appearanceSegment")).width(), 280.0);
        QCOMPARE(sceneRect(h.item("updatesVersion")).right(), emuRight);
        QCOMPARE(sceneRect(h.item("updateChannelSegment")).right(), emuRight);
        QCOMPARE(sceneRect(h.item("updateAutoInstallToggle")).right(), emuRight);
      }
    }
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

    // Defaults: FrameBeam options only; core keys belong to the systems.
    QVERIFY(h.click("defaultsCard"));
    QCOMPARE(emu->level(), QStringLiteral("global"));
    QVERIFY(emu->defaultsSelected());
    QCOMPARE(text(h, "emulationTitle"), QStringLiteral("Defaults for all systems"));
    QVERIFY(h.item("optionGroup_core") == nullptr);
    QVERIFY(visible(h, "globalCoreHint"));
    QVERIFY(visible(h, "optionGroup_framebeam"));
    QVERIFY(h.item("optionToggle_framebeam.fullscreen_on_start") != nullptr);  // off/on = toggle
    QCOMPARE(text(h, "optionOrigin_framebeam.fullscreen_on_start"), QStringLiteral("Default"));
    QVERIFY(!h.controller->fullscreenOnStart());
    QVERIFY(h.click("optionToggle_framebeam.fullscreen_on_start"));
    QVERIFY(visible(h, "changedDot_framebeam.fullscreen_on_start"));
    QVERIFY(h.controller->fullscreenOnStart());  // wired: the game view goes fullscreen on start
    QCOMPARE(emu->defaultsChangedCount(), 1);
    QCOMPARE(text(h, "defaultsChanged"), QStringLiteral("1 changed"));

    // The system inherits it (stored only once, at Global); resetting Defaults restores the default.
    QVERIFY(h.click("systemCard_nds"));
    QCOMPARE(emu->level(), QStringLiteral("system"));
    QVERIFY(h.controller->fullscreenOnStart());
    QVERIFY(h.click("defaultsCard"));
    QVERIFY(h.click("optionReset_framebeam.fullscreen_on_start"));
    QVERIFY(!h.controller->fullscreenOnStart());

    // Default Multiview offers Picture-in-Picture, Side-by-Side and Grid 2x2; it is applied to the Session view.
    QCOMPARE(h.controller->sessions()->multiviewMode(), QStringLiteral("pip"));
    QVERIFY(h.item("optionSelect_framebeam.default_multiview") != nullptr);
    QStringList values;
    for (const QVariant& g : emu->groups()) {
      for (const QVariant& o : g.toMap().value(QStringLiteral("options")).toList()) {
        if (o.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("framebeam.default_multiview")) {
          for (const QVariant& v : o.toMap().value(QStringLiteral("values")).toList()) values.append(v.toMap().value(QStringLiteral("value")).toString());
        }
      }
    }
    QCOMPARE(values, (QStringList{QStringLiteral("pip"), QStringLiteral("side"), QStringLiteral("grid")}));
    pick(h, "optionSelect_framebeam.default_multiview", QStringLiteral("side"));
    QCOMPARE(h.controller->sessions()->multiviewMode(), QStringLiteral("side"));
    pick(h, "optionSelect_framebeam.default_multiview", QStringLiteral("grid"));
    QCOMPARE(emu->frameBeamValue(QStringLiteral("framebeam.default_multiview"), QStringLiteral("nds")), QStringLiteral("grid"));

    // A core option set globally in the file (keys stay assigned to the core) is still inherited by the system.
    emu->settings()->setValue(L::Global, QString(), QStringLiteral("melonds_boot_mode"), QStringLiteral("native"));
    emu->setLevel(QStringLiteral("system"));
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
    QVERIFY(visible(h, "coreNoteHint"));
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
    QVERIFY(h.item("optionRow_melonds_render_mode") != nullptr);
    QVERIFY(h.item("optionRow_melonds_sysfile_mode") == nullptr);
    QVERIFY(h.item("optionRow_melonds_firmware_nds_path") == nullptr);
    QVERIFY(h.item("optionRow_melonds_show_lid_state") == nullptr);
    const QString cache = QDir(h.controller->profileStore()->baseDir()).filePath(QStringLiteral("cache/core-options/melonds_ds.json"));
    QVERIFY(QFile::exists(cache));
    pick(h, "optionSelect_melonds_audio_interpolation", QStringLiteral("cubic"));
    QVERIFY(visible(h, "changedDot_melonds_audio_interpolation"));

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
    QCOMPARE(text(h, "profilesLocalFooter"), QStringLiteral("Saved on this device only"));
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
    c->beginCapture(QStringLiteral("a"));
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

  void controllersHotkeysTab() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ControllersController* c = h.controller->controllers();
    GameSession* s = h.controller->gameSession();
    h.controller->showControllers();
    h.window->resize(940, 800);  // narrow main column (408 px, input test hidden), like Windows with wide fallback fonts
    QTest::qWait(100);
    QQuickTest::qWaitForPolish(h.window);
    QVERIFY(!visible(h, "hotkeysInfo"));
    QVERIFY(h.click("tabHotkeys"));
    QVERIFY(visible(h, "hotkeysInfo"));
    QCOMPARE(text(h, "hotkeysInfo"), QStringLiteral("Player hotkeys work on the keyboard and are never sent to the game."));
    QCOMPARE(c->hotkeyRows().size(), 5);  // four actions + the fixed Esc row
    QCOMPARE(text(h, "hotkeyText_fullscreen"), QStringLiteral("F11"));
    QCOMPARE(text(h, "hotkeyText_diagnostics"), QStringLiteral("F3"));
    QCOMPARE(text(h, "hotkeyText_snapshot"), QStringLiteral("F5"));
    QCOMPARE(text(h, "hotkeyText_escape"), QStringLiteral("Esc"));
    QVERIFY(!visible(h, "resetChanged"));
    // The fixed Esc row cannot be captured.
    QVERIFY(h.click("hotkeyField_escape"));
    QVERIFY(c->hotkeyListening().isEmpty());

    // Capture: Escape cancels, a key used by another action is rejected (binding stays, note), a free key is taken.
    QVERIFY(h.click("hotkeyField_fullscreen"));
    QCOMPARE(text(h, "hotkeyText_fullscreen"), QStringLiteral("Press a key…"));
    QTest::keyClick(h.window, Qt::Key_Escape);
    QVERIFY(c->hotkeyListening().isEmpty());
    QCOMPARE(text(h, "hotkeyText_fullscreen"), QStringLiteral("F11"));
    QVERIFY(h.click("hotkeyField_fullscreen"));
    QTest::keyClick(h.window, Qt::Key_F3);
    QCOMPARE(c->hotkeyNote(), QStringLiteral("Already used by Diagnostics overlay"));
    QVERIFY(visible(h, "hotkeyNote"));
    QCOMPARE(c->hotkeyListening(), QStringLiteral("fullscreen"));
    QCOMPARE(c->hotkeyAction(Qt::Key_F11), QStringLiteral("fullscreen"));
    QTest::keyClick(h.window, Qt::Key_F10);
    QVERIFY(c->hotkeyListening().isEmpty());
    QVERIFY(c->hotkeyNote().isEmpty());
    QCOMPARE(text(h, "hotkeyText_fullscreen"), QStringLiteral("F10"));
    QVERIFY(visible(h, "hotkeyChangedDot_fullscreen"));
    QCOMPARE(c->hotkeysChangedCount(), 1);
    QVERIFY(visible(h, "resetChanged"));
    QCOMPARE(c->hotkeyLabels().value(QStringLiteral("fullscreen")).toString(), QStringLiteral("F10"));
    // The Player resolves keys through the configured actions.
    QCOMPARE(c->hotkeyAction(Qt::Key_F10), QStringLiteral("fullscreen"));
    QCOMPARE(c->hotkeyAction(Qt::Key_F11), QString());
    QVERIFY(s->isReservedKey(Qt::Key_F10));
    QVERIFY(!s->isReservedKey(Qt::Key_F11));
    QVERIFY(s->isReservedKey(Qt::Key_Escape));

    // A hotkey wins over the keyboard profile: Z is B in the standard keyboard profile.
    QVERIFY(s->keyEvent(Qt::Key_Z, true));
    s->keyEvent(Qt::Key_Z, false);
    QVERIFY(h.click("hotkeyField_snapshot"));
    QTest::keyClick(h.window, Qt::Key_Z);
    QCOMPARE(text(h, "hotkeyText_snapshot"), QStringLiteral("Z"));
    QCOMPARE(c->hotkeyAction(Qt::Key_Z), QStringLiteral("snapshot"));
    QCOMPARE(c->hotkeyRows().at(2).toMap().value(QStringLiteral("conflictInput")).toString(), QStringLiteral("B"));
    QVERIFY(visible(h, "hotkeyConflict_snapshot"));
    QQuickTest::qWaitForPolish(h.window);
    {
      // Nothing of the Hotkeys table lies outside the main column (the flickable).
      QQuickItem* flick = h.item("hotkeysInfo");
      while (flick && !flick->inherits("QQuickFlickable")) flick = flick->parentItem();
      QVERIFY(flick != nullptr);
      const QRectF column = flick->mapRectToScene(QRectF(0, 0, flick->width(), flick->height()));
      QVERIFY2(h.item("hotkeysInfo")->width() <= column.width() - 56 + 0.5,
               qPrintable(QStringLiteral("hotkeysInfo wider than the content column (%1)\n%2").arg(column.width() - 56).arg(hotkeysLayoutDump(h))));
      for (const char* name : {"hotkeyField_fullscreen", "hotkeyField_snapshot", "hotkeyField_escape", "hotkeyReset_fullscreen",
                               "hotkeyReset_snapshot", "hotkeyConflict_snapshot", "hotkeyText_snapshot"}) {
        QQuickItem* it = h.item(name);
        QVERIFY2(it != nullptr && it->isVisible(), name);
        const QRectF r = it->mapRectToScene(QRectF(0, 0, it->width(), it->height()));
        QVERIFY2(r.left() >= column.left() - 0.5 && r.right() <= column.right() + 0.5,
                 qPrintable(QStringLiteral("%1 outside the column: %2..%3 vs %4..%5\n%6").arg(QLatin1String(name)).arg(r.left()).arg(r.right()).arg(column.left()).arg(column.right()).arg(hotkeysLayoutDump(h))));
      }
    }
    QVERIFY(!c->keyboardMap().contains(Qt::Key_Z));
    QVERIFY(!s->keyEvent(Qt::Key_Z, true));
    QCOMPARE(s->joypadMask(), 0u);
    // Unassigned: no key, no hint.
    QVERIFY(h.click("hotkeyClear_diagnostics"));
    QCOMPARE(text(h, "hotkeyText_diagnostics"), QStringLiteral("not set"));
    QCOMPARE(c->hotkeyLabels().value(QStringLiteral("diagnostics")).toString(), QString());
    QVERIFY(!s->isReservedKey(Qt::Key_F3));

    // Persisted, then reset one / all.
    {
      ControllerProfiles reread(h.controller->profileStore()->baseDir());
      QCOMPARE(reread.hotkey(QStringLiteral("fullscreen")), int(Qt::Key_F10));
      QCOMPARE(reread.hotkey(QStringLiteral("diagnostics")), 0);
    }
    QCOMPARE(c->hotkeysChangedCount(), 3);
    QVERIFY(h.click("hotkeyReset_fullscreen"));
    QCOMPARE(text(h, "hotkeyText_fullscreen"), QStringLiteral("F11"));
    QCOMPARE(c->hotkeysChangedCount(), 2);
    QVERIFY(h.click("resetChanged"));
    QCOMPARE(c->hotkeysChangedCount(), 0);
    QCOMPARE(text(h, "hotkeyText_snapshot"), QStringLiteral("F5"));
    QVERIFY(s->isReservedKey(Qt::Key_F3));
    QVERIFY(c->keyboardMap().contains(Qt::Key_Z));  // the profile key is back
    // Back on the Buttons tab the mapping table is shown again.
    QVERIFY(h.click("tabButtons"));
    QVERIFY(!visible(h, "hotkeysInfo"));
    QVERIFY(visible(h, "mapField_a"));
  }

  void controllersDuplicateToEdit() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    ControllersController* c = h.controller->controllers();
    h.controller->showControllers();
    QVERIFY(c->profileBuiltin());
    QVERIFY(visible(h, "duplicateToEditButton"));
    QVERIFY(h.click("duplicateToEditButton"));
    QVERIFY(!c->profileBuiltin());
    QCOMPARE(c->profileName(), QStringLiteral("Keyboard · Standard copy"));
    QVERIFY(!visible(h, "duplicateToEditButton"));
    QVERIFY(!visible(h, "builtinNote"));
    // Clicking a field of a built-in profile duplicates and starts the capture on the copy.
    c->selectProfile(QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
    QVERIFY(c->profileBuiltin());
    QVERIFY(h.click("mapField_a"));
    QVERIFY(!c->profileBuiltin());
    QCOMPARE(c->profileName(), QStringLiteral("Keyboard · Standard copy 2"));
    QCOMPARE(c->listening(), QStringLiteral("a"));
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("Press a key…"));
    QTest::keyClick(h.window, Qt::Key_J);
    QCOMPARE(text(h, "mapText_a"), QStringLiteral("J"));
    // The built-in profile stays untouched and read-only.
    const auto builtin = c->profileStore()->find(QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
    QVERIFY(builtin && builtin->builtin);
    QCOMPARE(builtin->bindings.value(QStringLiteral("a")), QStringList{keyToken(Qt::Key_X)});
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
