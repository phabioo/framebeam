// Plugin UI runtime in the PluginsController (shell context, UI models, actions, toasts, host events) and the
// image://fbplugin provider safety rules. Homebrew plugin folders and generated PNGs only; no QML.
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "pluginimageprovider.h"
#include "pluginscontroller.h"

using namespace framebeam::ui;

namespace {

QString writePlugin(const QString& parent, const QString& id, const QJsonObject& extra, const QString& lua, const QStringList& perms = {"ui"}) {
  const QString dir = QDir(parent).filePath(id);
  QDir().mkpath(dir);
  QJsonObject m{{"id", id}, {"name", id.section('.', -1)}, {"version", "1.0.0"}, {"api", 1}, {"type", "script"}, {"entry", "main.lua"},
                {"permissions", QJsonArray::fromStringList(perms)}};
  for (auto it = extra.begin(); it != extra.end(); ++it) m[it.key()] = it.value();
  QFile mf(dir + "/manifest.json");
  if (!mf.open(QIODevice::WriteOnly)) return {};
  mf.write(QJsonDocument(m).toJson());
  QFile lf(dir + "/main.lua");
  if (!lf.open(QIODevice::WriteOnly)) return {};
  lf.write(lua.toUtf8());
  return dir;
}

const char* kShellLua = R"LUA(
  fb.ui.set('screen', {type='text', text='ready'})
  fb.ui.badge(3)
  fb.ui.set('hud', {type='text', text='hud'})
  fb.ui.on_action(function(a, v, slot) fb.ui.set('screen', {type='text', text=a .. ':' .. tostring(v) .. ':' .. slot}) end)
  fb.events.on('fb', 'detail.game', function(g)
    if g then fb.ui.set('detail', {type='text', text=g.title}) else fb.ui.clear('detail') end
  end)
  fb.events.on('fb', 'hud.visible', function(v) fb.ui.set('hud', {type='text', text='visible=' .. tostring(v)}) end)
  fb.events.on('fb', 'tiles', function(t) fb.ui.set('tile:' .. t[1].id, {type='text', text=t[1].player}) end)
  fb.ui.notify{ text='hello', actions={ {label='Go', action='go'} } }
)LUA";

}  // namespace

class PluginsRuntimeUiTest : public QObject {
  Q_OBJECT

  QTemporaryDir m_data, m_src;
  std::unique_ptr<PluginsController> m_pc;

  void install(const QString& id) {
    const auto r = m_pc->manager()->installFromFolder(m_src.path() + "/" + id);
    QVERIFY2(r.ok, qPrintable(r.error));
  }

 private slots:
  void init() {
    m_pc = std::make_unique<PluginsController>(nullptr, m_data.path(), nullptr);
    QDir(m_data.path() + "/plugins").removeRecursively();
    QDir(m_src.path()).removeRecursively();
    m_pc.reset();
    m_pc = std::make_unique<PluginsController>(nullptr, m_data.path(), nullptr);
  }
  void cleanup() { m_pc.reset(); }

  void shellContextAndModels() {
    writePlugin(m_src.path(), "net.test.dep", {{"provides", QJsonArray{"dep.api"}}}, "fb.log.info('dep')", {});
    writePlugin(m_src.path(), "net.test.shell",
                {{"requires", QJsonObject{{"net.test.dep", "*"}}},
                 {"ui", QJsonObject{{"screen", QJsonObject{{"title", "Runs"}}}, {"detail", true}, {"hud", true}, {"tile", true},
                                    {"hotkeys", QJsonArray{QJsonObject{{"id", "toggle-hud"}, {"label", "Toggle"}}}}}}},
                kShellLua);
    writePlugin(m_src.path(), "net.test.panel", {{"ui", QJsonObject{{"panel", QJsonObject{{"title", "Panel"}}}}}},
                "fb.ui.set('panel', {type='text', text='in game'}) fb.ui.dot('news')");
    install("net.test.dep");
    install("net.test.shell");
    install("net.test.panel");

    // Nothing runs while the plugin is off or Developer mode is off.
    QVERIFY(m_pc->shellPluginIds().isEmpty());
    m_pc->setPluginEnabled("net.test.shell", true);
    QVERIFY(!m_pc->shellRunning());
    m_pc->setDeveloperMode(true);
    QVERIFY(m_pc->shellRunning());
    QCOMPARE(m_pc->shellPluginIds(), (QStringList{"net.test.dep", "net.test.shell"}));  // required plugin first

    QTRY_COMPARE_WITH_TIMEOUT(m_pc->sidebarEntries().size(), 1, 10000);
    QTRY_COMPARE(m_pc->sidebarEntries().at(0).toMap().value("badge").toInt(), 3);
    QCOMPARE(m_pc->sidebarEntries().at(0).toMap().value("title").toString(), QStringLiteral("Runs"));
    QCOMPARE(m_pc->sidebarEntries().at(0).toMap().value("pluginId").toString(), QStringLiteral("net.test.shell"));
    QTRY_COMPARE(m_pc->slotTree("net.test.shell", "screen").value("text").toString(), QStringLiteral("ready"));
    QTRY_COMPARE(m_pc->hudWidgets().size(), 1);
    QCOMPARE(m_pc->hotkeys().size(), 1);
    QVERIFY(m_pc->panelTabs().isEmpty());  // net.test.panel is not part of the shell set

    // Toast of the plugin, with its name; dismiss and mute.
    QTRY_COMPARE(m_pc->toasts().size(), 1);
    const QVariantMap toast = m_pc->toasts().at(0).toMap();
    QCOMPARE(toast.value("pluginName").toString(), QStringLiteral("shell"));
    QCOMPARE(toast.value("text").toString(), QStringLiteral("hello"));
    m_pc->dismissToast(toast.value("toastId").toInt());
    QCOMPARE(m_pc->toasts().size(), 0);

    // Action round trip UI -> plugin thread -> on_action -> new tree.
    QSignalSpy slotSpy(m_pc.get(), &PluginsController::slotChanged);
    m_pc->sendAction("net.test.shell", "screen", "pick", 7);
    QTRY_COMPARE(m_pc->slotTree("net.test.shell", "screen").value("text").toString(), QStringLiteral("pick:7:screen"));
    QTRY_VERIFY(slotSpy.count() >= 1);
    m_pc->triggerHotkey("net.test.shell", "toggle-hud");
    QTRY_COMPARE(m_pc->slotTree("net.test.shell", "screen").value("text").toString(), QStringLiteral("toggle-hud:nil:hotkey"));
    m_pc->triggerHotkey("net.test.shell", "unknown");  // not declared: ignored

    // HUD hide event and corner.
    m_pc->hideHud("net.test.shell");
    QTRY_COMPARE(m_pc->slotTree("net.test.shell", "hud").value("text").toString(), QStringLiteral("visible=false"));
    QTRY_VERIFY(m_pc->hudWidgets().at(0).toMap().value("hidden").toBool());
    m_pc->showHud("net.test.shell");
    QTRY_COMPARE(m_pc->slotTree("net.test.shell", "hud").value("text").toString(), QStringLiteral("visible=true"));
    m_pc->setHudCorner("net.test.shell", "bottom-left");
    QCOMPARE(m_pc->hudWidgets().at(0).toMap().value("corner").toString(), QStringLiteral("bottom-left"));

    // Detail game: event in, section out; changing the game clears the section first.
    m_pc->setDetailGame("nds", "ABCD", "ff", "Homebrew");
    QTRY_COMPARE(m_pc->detailSections().size(), 1);
    QCOMPARE(m_pc->slotTree("net.test.shell", "detail").value("text").toString(), QStringLiteral("Homebrew"));
    m_pc->setDetailGame(QString(), QString(), QString(), QString());
    QTRY_COMPARE(m_pc->detailSections().size(), 0);

    // Multiview tiles.
    m_pc->setTiles({QVariantMap{{"id", "t1"}, {"player", "Ann"}, {"game", "G"}}});
    QTRY_COMPARE(m_pc->tileStrips("t1").size(), 1);
    QCOMPARE(m_pc->tileStrips("t1").at(0).toMap().value("tree").toMap().value("text").toString(), QStringLiteral("Ann"));
    QCOMPARE(m_pc->tileStrips("other").size(), 0);

    // Game start: the shell set stops, the game set runs; game end restarts the shell set.
    m_pc->setPluginEnabled("net.test.panel", true);
    m_pc->gameStarted("nds", m_src.path() + "/missing.nds", "abc", "Homebrew");
    QVERIFY(!m_pc->shellRunning());
    QVERIFY(m_pc->pluginsRunning());
    QTRY_COMPARE_WITH_TIMEOUT(m_pc->panelTabs().size(), 1, 10000);
    QCOMPARE(m_pc->panelTabs().at(0).toMap().value("title").toString(), QStringLiteral("Panel"));
    QTRY_COMPARE(m_pc->panelTabs().at(0).toMap().value("dot").toString(), QStringLiteral("news"));
    QTRY_COMPARE(m_pc->slotTree("net.test.panel", "panel").value("text").toString(), QStringLiteral("in game"));
    m_pc->gameStopped();
    QVERIFY(!m_pc->pluginsRunning());
    QVERIFY(m_pc->shellRunning());
    QCOMPARE(m_pc->shellPluginIds(), (QStringList{"net.test.dep", "net.test.shell"}));
    QTRY_COMPARE_WITH_TIMEOUT(m_pc->panelTabs().size(), 0, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_pc->sidebarEntries().size(), 1, 10000);

    // Toggling re-evaluates the shell set; so does Developer mode.
    m_pc->setPluginEnabled("net.test.shell", false);
    QVERIFY(!m_pc->shellRunning());
    QVERIFY(m_pc->shellPluginIds().isEmpty());
    QTRY_COMPARE(m_pc->sidebarEntries().size(), 0);
    m_pc->setPluginEnabled("net.test.shell", true);
    QVERIFY(m_pc->shellRunning());
    m_pc->setDeveloperMode(false);
    QVERIFY(!m_pc->shellRunning());
    QTRY_COMPARE(m_pc->sidebarEntries().size(), 0);
  }

  void toastMuting() {
    writePlugin(m_src.path(), "net.test.shell", {{"ui", QJsonObject{{"screen", QJsonObject{{"title", "Runs"}}}}}}, "fb.ui.notify{ text='a' }");
    install("net.test.shell");
    m_pc->setDeveloperMode(true);
    m_pc->muteToasts("net.test.shell", "always");
    m_pc->setPluginEnabled("net.test.shell", true);
    QTRY_VERIFY(m_pc->shellRunning());
    QTest::qWait(300);
    QCOMPARE(m_pc->toasts().size(), 0);  // muted
    m_pc->muteToasts("net.test.shell", "off");
    m_pc->setPluginEnabled("net.test.shell", false);
    m_pc->setPluginEnabled("net.test.shell", true);
    QTRY_COMPARE_WITH_TIMEOUT(m_pc->toasts().size(), 1, 10000);
  }

  // ---------------------------------------------------------------- image provider

  void imageProviderRules() {
    QTemporaryDir root;
    const QString cache = root.path() + "/cache";
    const QString plug = root.path() + "/plugin";
    QDir().mkpath(cache + "/icons");
    QDir().mkpath(plug + "/assets");
    QImage ok(8, 6, QImage::Format_ARGB32);
    ok.fill(Qt::red);
    QVERIFY(ok.save(cache + "/icons/25.png", "PNG"));
    QVERIFY(ok.save(plug + "/assets/a.png", "PNG"));
    QVERIFY(ok.save(plug + "/secret.png", "PNG"));
    QVERIFY(ok.save(root.path() + "/outside.png", "PNG"));
    QImage wide(5000, 1, QImage::Format_ARGB32);
    wide.fill(Qt::blue);
    QVERIFY(wide.save(cache + "/wide.png", "PNG"));
    {
      QFile big(cache + "/big.png");
      QVERIFY(big.open(QIODevice::WriteOnly));
      big.write(QByteArray(9 * 1024 * 1024, 'x'));
    }
    const bool linked = QFile::link(root.path() + "/outside.png", cache + "/link.png");

    PluginImageSource src{cache, plug, {"assets"}};
    QString err;
    QVERIFY(!PluginImageProvider::resolve(src, "p/cache/icons/25.png", &err).isEmpty());
    QVERIFY(!PluginImageProvider::resolve(src, "p/asset/assets/a.png", &err).isEmpty());
    QVERIFY(PluginImageProvider::resolve(src, "p/cache/../outside.png", &err).isEmpty());
    QVERIFY2(err.contains("invalid path"), qPrintable(err));
    QVERIFY(PluginImageProvider::resolve(src, "p/cache/icons/../../outside.png", &err).isEmpty());
    QVERIFY(PluginImageProvider::resolve(src, "p/cache//icons/25.png", &err).isEmpty());
    QVERIFY(PluginImageProvider::resolve(src, "p/cache/%2e%2e/outside.png", &err).isEmpty());
    QVERIFY(PluginImageProvider::resolve(src, "p/asset/secret.png", &err).isEmpty());  // not declared
    QVERIFY2(err.contains("not declared"), qPrintable(err));
    QVERIFY(PluginImageProvider::resolve(src, "p/cache/icons/25.jpg", &err).isEmpty());
    QVERIFY(PluginImageProvider::resolve(src, "p/other/icons/25.png", &err).isEmpty());
    QVERIFY(PluginImageProvider::resolve(src, "p/cache/missing.png", &err).isEmpty());
#ifndef Q_OS_WIN  // QFile::link makes a .lnk shortcut on Windows, not a symlink; the provider never follows .lnk files
    if (linked) {
      QVERIFY(PluginImageProvider::resolve(src, "p/cache/link.png", &err).isEmpty());
      QVERIFY2(err.contains("leaves the plugin folder"), qPrintable(err));
    }
#else
    // A .lnk shortcut is just a file of non-PNG bytes: it is never followed and loading it fails as "not a PNG".
    if (linked) {
      const QString lnk = cache + "/link.png.lnk";
      QVERIFY(QFileInfo::exists(lnk));
      QVERIFY(PluginImageProvider::loadPng(lnk, &err).isNull());
      QVERIFY(!err.isEmpty());
    }
#endif

    const QImage img = PluginImageProvider::loadPng(cache + "/icons/25.png", &err);
    QCOMPARE(img.size(), QSize(8, 6));
    QVERIFY(PluginImageProvider::loadPng(cache + "/big.png", &err).isNull());
    QVERIFY2(err.contains("8 MiB"), qPrintable(err));
    QVERIFY(PluginImageProvider::loadPng(cache + "/wide.png", &err).isNull());
    QVERIFY2(err.contains("4096"), qPrintable(err));

    auto sources = std::make_shared<PluginImageSources>();
    sources->set({{"p", src}});
    PluginImageProvider provider(sources);
    QSize size;
    QCOMPARE(provider.requestImage("p/cache/icons/25.png", &size, QSize()).size(), QSize(8, 6));
    QCOMPARE(size, QSize(8, 6));
    QVERIFY(provider.requestImage("q/cache/icons/25.png", &size, QSize()).isNull());  // unknown plugin
    QVERIFY(provider.requestImage("p/cache/../outside.png", &size, QSize()).isNull());
  }
};

QTEST_MAIN(PluginsRuntimeUiTest)
#include "plugins_runtime_ui_test.moc"
