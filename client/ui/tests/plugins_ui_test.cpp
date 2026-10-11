// Plugins in the Player UI: Settings > Plugins (Developer mode, list, install, uninstall, empty state) and the Plugin
// console tab of the game panel (preview game, no core). With FRAMEBEAM_SCREENSHOT_DIR=<dir> the views are also saved as
// PNGs (dark and light, 1440x900) for the review; they are never checked in. Homebrew plugin folders only.
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

namespace {

void pair(Harness& h, FakeHub& hub) {
  h.controller->addHub(hub.address());
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation, 8000);
  h.controller->confirmTrust();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsPairing, 8000);
  h.controller->requestPairing();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
}

QString writePlugin(const QString& parent, const QString& id, const QString& name, const QJsonObject& requires_ = {}) {
  const QString dir = QDir(parent).filePath(id);
  QDir().mkpath(dir);
  QJsonObject m{{"id", id}, {"name", name}, {"version", "1.2.0"}, {"api", 1}, {"type", "script"}, {"entry", "main.lua"},
                {"permissions", QJsonArray{"memory.read"}}};
  if (!requires_.isEmpty()) m["requires"] = requires_;
  QFile mf(dir + "/manifest.json");
  if (!mf.open(QIODevice::WriteOnly)) return {};
  mf.write(QJsonDocument(m).toJson());
  QFile lf(dir + "/main.lua");
  if (!lf.open(QIODevice::WriteOnly)) return {};
  lf.write("fb.log.info('hello')\n");
  return dir;
}

QVariantMap consoleData(bool error, bool withRegions) {
  QVariantList plugins{QVariantMap{{"id", "creature-core"}, {"name", "Creature Core"}, {"status", error ? "error" : "running"}, {"statusText", error ? "Paused after an error" : "Running"}},
                       QVariantMap{{"id", "item-helper"}, {"name", "Item Helper"}, {"status", "running"}, {"statusText", "Running"}}};
  QVariantList watched{QVariantMap{{"address", "0x08001A10"}, {"value", "6"}, {"label", "party count"}},
                       QVariantMap{{"address", "0x08002B44"}, {"value", "0x00000142"}, {"label", "rng seed"}},
                       QVariantMap{{"address", "0x14000104"}, {"value", "-"}, {"label", "slot 1 (not in snapshot)"}}};
  QVariantList events{QVariantMap{{"time", "18:42:07.311"}, {"plugin", "creature-core"}, {"kind", "party.changed"}, {"text", "slot 1 species 1"}},
                      QVariantMap{{"time", "18:42:05.002"}, {"plugin", "creature-core"}, {"kind", "game.ready"}, {"text", "{code=\"00040000\"}"}}};
  QVariantList log{QVariantMap{{"time", "18:42:09.870"}, {"level", "error"}, {"text", "main.lua:41: attempt to index a nil value (field 'party')"}},
                   QVariantMap{{"time", "18:42:08.120"}, {"level", "warn"}, {"text", "party pointer is 0, retrying"}},
                   QVariantMap{{"time", "18:42:07.311"}, {"level", "info"}, {"text", "party changed: 6 members"}},
                   QVariantMap{{"time", "18:42:05.001"}, {"level", "debug"}, {"text", "watch party_ptr 0x08000010 4"}}};
  QVariantList regions;
  if (withRegions)
    regions = {QVariantMap{{"start", "0x08000000"}, {"length", "0x100000"}, {"addrspace", "heap"}},
               QVariantMap{{"start", "0x14000000"}, {"length", "0x100000"}, {"addrspace", "linear"}}};
  return QVariantMap{{"plugins", plugins},
                     {"selected", "creature-core"},
                     {"status", error ? "error" : "running"},
                     {"statusText", error ? "Paused after an error" : "Running"},
                     {"error", error ? "main.lua:41: attempt to index a nil value (field 'party')\nstack traceback:\n\tmain.lua:41: in function 'on_tick'" : ""},
                     {"watched", watched},
                     {"events", events},
                     {"log", log},
                     {"regions", regions},
                     {"regionsText", withRegions ? "" : "The core exposes no memory map"}};
}

}  // namespace

class PluginsUiTest : public QObject {
  Q_OBJECT
  QString shotDir_;
  QTemporaryDir src_;

  void shot(Harness& h, const QString& name) {
    if (shotDir_.isEmpty()) return;
    QQuickTest::qWaitForPolish(h.window);
    QTest::qWait(350);
    QDir().mkpath(shotDir_);
    const QImage img = h.window->grabWindow();
    QVERIFY2(!img.isNull() && !uitest::isAllBlack(img), qPrintable(name));
    QVERIFY(img.save(QDir(shotDir_).filePath(name + QStringLiteral(".png"))));
  }
  static void gotoPlugins(Harness& h) {
    h.window->resize(1440, 900);
    h.controller->showSettings();
    QTest::qWait(150);
    QVERIFY(h.click("section_plugins"));
    QTest::qWait(300);
  }
  static void startPreviewGame(Harness& h) {
    GameSession* gs = h.controller->gameSession();
    QImage frame(256, 384, QImage::Format_RGB32);
    frame.fill(QColor(40, 90, 140));
    gs->setPreview(QStringLiteral("Framebeam Test"), frame, emu::DisplayProfile(), EmulationDiagnostics());
    emit gs->started();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 8000);
    h.controller->sessions()->gameStarted(QStringLiteral("t1"), QStringLiteral("Framebeam Test"));
    h.controller->sessions()->setTab(QStringLiteral("session"));
    QQuickTest::qWaitForPolish(h.window);
  }

 private slots:
  void initTestCase() {
    shotDir_ = qEnvironmentVariable("FRAMEBEAM_SCREENSHOT_DIR");
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    uitest::installWarningCounter();
  }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  void settingsSection() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-pl");
    hub.name = QStringLiteral("Home");
    hub.decision = FakeHub::Decision::Approve;
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    PluginsController* pc = h.controller->plugins();
    QVERIFY(pc != nullptr);
    QVERIFY(!pc->developerMode());
    QVERIFY(!pc->consoleAvailable());
    QCOMPARE(pc->manager()->rootDir(), QDir::cleanPath(h.controller->profileStore()->baseDir() + "/plugins"));

    // Empty state (dark and light).
    gotoPlugins(h);
    QVERIFY(h.item("pluginsSection") != nullptr && h.item("pluginsSection")->isVisible());
    QCOMPARE(h.item("sectionStatus_plugins")->property("text").toString(), QStringLiteral("None installed"));
    QVERIFY(h.item("pluginsEmpty")->isVisible());
    QVERIFY(h.item("pluginInstallButton")->isVisible());
    QVERIFY(!h.item("pluginLinkButton")->isVisible());  // Developer mode only
    QVERIFY(h.item("pluginDeveloperModeHint")->property("text").toString().startsWith(
        QStringLiteral("Allows unsigned local plugins, adds the Plugin console tab in game")));
    shot(h, QStringLiteral("4a-plugins-empty"));
    h.controller->setAppearance(QStringLiteral("light"));
    shot(h, QStringLiteral("4a-plugins-empty-light"));
    h.controller->setAppearance(QStringLiteral("dark"));

    // Developer mode persists and shows the link button.
    QVERIFY(h.click("pluginDeveloperModeToggle"));
    QTRY_VERIFY(pc->developerMode());
    QVERIFY(h.controller->playerSettings()->developerMode());
    QVERIFY(pc->manager()->developerMode());
    QVERIFY(PlayerSettings(h.controller->profileStore()->baseDir()).developerMode());
    QTRY_VERIFY(h.item("pluginLinkButton")->isVisible());

    // Install two plugins from folders (copy); one has an unmet dependency.
    QVERIFY(!writePlugin(src_.path(), "net.example.creature-core", "Creature Core").isEmpty());
    QVERIFY(!writePlugin(src_.path(), "net.example.item-helper", "Item Helper", QJsonObject{{"net.example.missing-core", "^2.0"}}).isEmpty());
    pc->installFromFolder(QUrl::fromLocalFile(src_.filePath("net.example.creature-core")).toString());
    QVERIFY2(pc->messageIsError() == false, qPrintable(pc->message()));
    pc->installFromFolder(src_.filePath("net.example.item-helper"));
    QVERIFY(QFileInfo(h.controller->profileStore()->baseDir() + "/plugins/installed/net.example.creature-core/manifest.json").exists());
    QTRY_VERIFY(h.item("pluginRow_net.example.creature-core") != nullptr);
    QVERIFY(h.item("pluginRow_net.example.item-helper") != nullptr);
    QCOMPARE(h.item("pluginsInstalledTitle")->property("text").toString(), QStringLiteral("Installed · 2"));
    QVERIFY(!h.item("pluginsEmpty")->isVisible());
    QCOMPARE(pc->plugins().size(), 2);
    const QVariantMap healthy = pc->plugins().value(0).toMap();
    const QVariantMap blocked = pc->plugins().value(1).toMap();
    QCOMPARE(healthy.value("id").toString(), QStringLiteral("net.example.creature-core"));
    QCOMPARE(healthy.value("originText").toString(), QStringLiteral("Local file"));
    QVERIFY(!healthy.value("enabled").toBool());
    QVERIFY(healthy.value("canToggle").toBool());
    QVERIFY(!blocked.value("canToggle").toBool());
    QVERIFY(!blocked.value("problemsText").toString().isEmpty());

    // Toggle on; the blocked plugin cannot be switched on.
    pc->setPluginEnabled(QStringLiteral("net.example.creature-core"), true);
    QVERIFY(pc->plugins().value(0).toMap().value("enabled").toBool());
    pc->setPluginEnabled(QStringLiteral("net.example.item-helper"), true);
    QVERIFY(!pc->plugins().value(1).toMap().value("enabled").toBool());
    QVERIFY(pc->messageIsError());
    pc->dismissMessage();
    QCOMPARE(h.item("sectionStatus_plugins")->property("text").toString(), QStringLiteral("1 on"));
    QTest::qWait(200);
    shot(h, QStringLiteral("4a-plugins-list"));
    h.controller->setAppearance(QStringLiteral("light"));
    shot(h, QStringLiteral("4a-plugins-list-light"));
    h.controller->setAppearance(QStringLiteral("dark"));

    // Uninstall asks first.
    QVERIFY(h.click("pluginUninstall"));
    QTRY_VERIFY(h.item("pluginUninstallConfirm") != nullptr && h.item("pluginUninstallConfirm")->isVisible());
    QVERIFY(h.click("pluginUninstallConfirm"));
    QTRY_COMPARE(pc->plugins().size(), 1);
    QVERIFY(!QFileInfo(h.controller->profileStore()->baseDir() + "/plugins/installed/net.example.creature-core").exists());

    // Developer mode off again: console never offered, link button gone.
    QVERIFY(h.click("pluginDeveloperModeToggle"));
    QTRY_VERIFY(!pc->developerMode());
    QVERIFY(!PlayerSettings(h.controller->profileStore()->baseDir()).developerMode());
    QTRY_VERIFY(!h.item("pluginLinkButton")->isVisible());
  }

  void consoleTab() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-pc");
    hub.name = QStringLiteral("Home");
    hub.decision = FakeHub::Decision::Approve;
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.window->resize(1440, 900);
    startPreviewGame(h);
    PluginsController* pc = h.controller->plugins();

    // Without plugins/Developer mode there is no tab bar.
    QVERIFY(h.item("panelTabBar") == nullptr || !h.item("panelTabBar")->isVisible());

    pc->setPreviewConsole(consoleData(false, true));
    QVERIFY(pc->consoleAvailable());
    QTRY_VERIFY(h.item("panelTabBar") != nullptr && h.item("panelTabBar")->isVisible());
    QVERIFY(h.item("pluginConsole") == nullptr || !h.item("pluginConsole")->isVisible());
    QVERIFY(h.click("panelTab_console"));
    QTRY_VERIFY(h.item("pluginConsole") != nullptr && h.item("pluginConsole")->isVisible());
    QVERIFY(!h.item("consoleError")->isVisible());
    QCOMPARE(h.items("consoleWatched").size(), 3);
    QCOMPARE(h.items("consoleEvent").size(), 2);
    QCOMPARE(h.items("consoleLog_warn").size(), 1);
    QCOMPARE(h.items("consoleLog_error").size(), 1);
    QCOMPARE(h.items("consoleRegion").size(), 2);
    QVERIFY(!h.item("consoleNoRegions")->isVisible());
    QVERIFY(h.item("consoleReload") != nullptr);
    QCOMPARE(h.item("consoleReload")->property("tip").toString(), QStringLiteral("Reload plugin · Ctrl+Shift+R"));
    shot(h, QStringLiteral("4d-console"));

    // Clear empties the log (preview).
    QVERIFY(h.click("consoleClear"));
    QTRY_COMPARE(h.items("consoleLog_warn").size(), 0);

    // Error state with "Restart plugin"; no memory map.
    pc->setPreviewConsole(consoleData(true, false));
    QTRY_VERIFY(h.item("consoleError") != nullptr && h.item("consoleError")->isVisible());
    QVERIFY(h.item("consoleRestart")->isVisible());
    QVERIFY(h.item("consoleErrorText")->property("text").toString().contains(QStringLiteral("attempt to index a nil value")));
    QVERIFY(h.item("consoleNoRegions")->isVisible());
    QCOMPARE(h.item("consoleNoRegions")->property("text").toString(), QStringLiteral("The core exposes no memory map"));
    shot(h, QStringLiteral("4d-console-error"));

    // Selecting another plugin goes through the controller.
    QVERIFY(h.click("consoleSelect"));
    QTRY_VERIFY(h.item("consolePlugin_item-helper") != nullptr && h.item("consolePlugin_item-helper")->isVisible());
    QVERIFY(h.click("consolePlugin_item-helper"));
    QTRY_COMPARE(pc->console().value("selected").toString(), QStringLiteral("item-helper"));

    // The console goes away with Developer mode / the plugins: back on the Game tab, no tab bar.
    pc->setPreviewConsole({});
    QTRY_VERIFY(h.item("panelTabBar") == nullptr || !h.item("panelTabBar")->isVisible());
    QVERIFY(h.item("sharingBlock") != nullptr && h.item("sharingBlock")->isVisible());
  }
};

UITEST_MAIN(PluginsUiTest)
#include "plugins_ui_test.moc"
