#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "plugin_manager.h"
#include "plugin_test_util.h"

using namespace framebeam::plugins;
using namespace framebeam::plugins::testutil;

class ManagerTest : public QObject {
  Q_OBJECT

  static QStringList ids(const GameResolution& r) {
    QStringList out;
    for (const PluginInfo& i : r.start) out << i.manifest.id;
    return out;
  }
  static QJsonArray ndsGames() {
    return QJsonDocument::fromJson(R"([{"system":"nds","codes":["ABC*"]}])").array();
  }

 private slots:
  void installEnableUninstall() {
    QTemporaryDir root, src;
    PluginManager m(root.path());
    PluginSpec s;
    s.id = "net.test.one";
    s.lua = "-- v1";
    s.files["i18n/en.json"] = "{}";
    const QString dir = writePlugin(src.path(), s);
    QSignalSpy changed(&m, &PluginManager::pluginsChanged);
    const InstallResult r = m.installFromFolder(dir);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.id, QString("net.test.one"));
    QVERIFY(changed.count() >= 1);
    QVERIFY(QFileInfo(root.path() + "/installed/net.test.one/i18n/en.json").isFile());
    QCOMPARE(m.list().size(), 1);
    const PluginInfo i = m.list().first();
    QVERIFY(!i.enabled);
    QVERIFY(!i.devLinked);
    QCOMPARE(i.origin, QString("local"));
    QCOMPARE(i.path, root.path() + "/installed/net.test.one");
    QVERIFY(i.problems.isEmpty());

    QVERIFY(m.setEnabled("net.test.one", true));
    QVERIFY(m.list().first().enabled);
    QVERIFY(!m.setEnabled("net.unknown.x", true));
    QVERIFY(QFileInfo(root.path() + "/state.json").isFile());
    { PluginManager again(root.path()); QVERIFY(again.list().first().enabled); }

    // reinstall replaces the local copy and keeps the enabled flag
    QFile f(dir + "/main.lua");
    f.open(QIODevice::WriteOnly);
    f.write("-- v2");
    f.close();
    QVERIFY(m.installFromFolder(dir).ok);
    QFile in(root.path() + "/installed/net.test.one/main.lua");
    in.open(QIODevice::ReadOnly);
    QCOMPARE(in.readAll(), QByteArray("-- v2"));
    QVERIFY(m.list().first().enabled);

    QDir().mkpath(root.path() + "/cache/net.test.one");
    QVERIFY(m.uninstall("net.test.one"));
    QVERIFY(m.list().isEmpty());
    QVERIFY(!QFileInfo(root.path() + "/installed/net.test.one").exists());
    QVERIFY(!QFileInfo(root.path() + "/cache/net.test.one").exists());
    QVERIFY(!m.uninstall("net.test.one"));
    QVERIFY(!m.uninstall("../x"));
  }

  void invalidInstallRejected() {
    QTemporaryDir root, src;
    PluginManager m(root.path());
    QVERIFY(!m.installFromFolder(src.path() + "/nothing").ok);
    PluginSpec s;
    s.permissions = {"memory.write"};
    const InstallResult r = m.installFromFolder(writePlugin(src.path(), s));
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QString("permission memory.write is not available yet"));
    QVERIFY(m.list().isEmpty());
  }

  void brokenFolderShowsProblem() {
    QTemporaryDir root;
    QDir().mkpath(root.path() + "/installed/net.broken.x");
    QFile f(root.path() + "/installed/net.broken.x/manifest.json");
    f.open(QIODevice::WriteOnly);
    f.write("{oops");
    f.close();
    PluginManager m(root.path());
    QCOMPARE(m.list().size(), 1);
    QVERIFY(!m.list().first().problems.isEmpty());
    QVERIFY(!m.setEnabled("net.broken.x", true));
  }

  void dependencyOrderAndGames() {
    QTemporaryDir root, src;
    PluginManager m(root.path());
    PluginSpec core;
    core.id = "net.test.core"; core.name = "Creature Core"; core.version = "1.2.0"; core.provides = {"x.api"};
    PluginSpec lib;  // required by "app", no games of its own
    lib.id = "net.test.lib"; lib.requires_ = QJsonObject{{"net.test.core", "^1.0"}};
    PluginSpec app;
    app.id = "net.test.app"; app.games = ndsGames(); app.requires_ = QJsonObject{{"net.test.lib", "*"}, {"net.test.core", "~1.2"}};
    PluginSpec other;
    other.id = "net.test.other"; other.games = QJsonDocument::fromJson(R"([{"system":"gba"}])").array();
    for (const PluginSpec* s : {&app, &core, &lib, &other}) QVERIFY(m.installFromFolder(writePlugin(src.path(), *s)).ok);
    QVERIFY(m.setEnabled("net.test.app", true));
    QVERIFY(m.setEnabled("net.test.other", true));
    m.setDeveloperMode(true);
    GameResolution r = m.resolveForGame("nds", "ABCD");
    QVERIFY(r.problems.isEmpty());
    QCOMPARE(ids(r), (QStringList{"net.test.core", "net.test.lib", "net.test.app"}));
    QVERIFY(!r.start.first().enabled);  // required plugins start even when not switched on themselves
    QCOMPARE(ids(m.resolveForGame("gba", "ZZZZ")), QStringList{"net.test.other"});
    QVERIFY(ids(m.resolveForGame("nds", "XXXX")).isEmpty());
    QVERIFY(ids(m.resolveForGame("3ds", "ABCD")).isEmpty());
  }

  void developerModeGate() {
    QTemporaryDir root, src;
    PluginManager m(root.path());
    PluginSpec s;
    s.id = "net.test.gate";
    QVERIFY(m.installFromFolder(writePlugin(src.path(), s)).ok);
    QVERIFY(m.setEnabled("net.test.gate", true));
    GameResolution r = m.resolveForGame("nds", "ABCD");
    QVERIFY(r.start.isEmpty());
    QVERIFY(r.problems.value("net.test.gate").join(" ").contains("Developer mode"));
    m.setDeveloperMode(true);
    QCOMPARE(ids(m.resolveForGame("nds", "ABCD")), QStringList{"net.test.gate"});
    m.setDeveloperMode(false);
    QVERIFY(m.resolveForGame("nds", "ABCD").start.isEmpty());
    QVERIFY(m.setEnabled("net.test.gate", false));
    QVERIFY(m.resolveForGame("nds", "ABCD").problems.isEmpty());
  }

  void missingAndMismatchAndCycle() {
    QTemporaryDir root, src;
    PluginManager m(root.path());
    m.setDeveloperMode(true);
    PluginSpec core;
    core.id = "net.test.core"; core.name = "Creature Core"; core.version = "1.2.0";
    PluginSpec needsNew;
    needsNew.id = "net.test.newer"; needsNew.requires_ = QJsonObject{{"net.test.core", "^2.0"}};
    PluginSpec needsGone;
    needsGone.id = "net.test.gone"; needsGone.requires_ = QJsonObject{{"net.test.absent", "^1.0"}};
    PluginSpec a, b, c;
    a.id = "net.test.a"; a.requires_ = QJsonObject{{"net.test.b", "*"}};
    b.id = "net.test.b"; b.requires_ = QJsonObject{{"net.test.a", "*"}};
    c.id = "net.test.c"; c.requires_ = QJsonObject{{"net.test.newer", "*"}};  // depends on a plugin with problems
    PluginSpec fine;
    fine.id = "net.test.fine";
    for (const PluginSpec* s : {&core, &needsNew, &needsGone, &a, &b, &c, &fine}) QVERIFY(m.installFromFolder(writePlugin(src.path(), *s)).ok);
    QVERIFY(!m.setEnabled("net.test.newer", true));  // cannot be turned on while it has problems
    QVERIFY(m.setEnabled("net.test.fine", true));

    auto problems = [&](const QString& id) { return m.info(id)->problems.join(" | "); };
    QCOMPARE(problems("net.test.newer"), QString("needs Creature Core ^2.0 · you have 1.2.0"));
    QCOMPARE(problems("net.test.gone"), QString("needs net.test.absent, not installed"));
    QVERIFY2(problems("net.test.a").contains("dependency cycle: net.test.a -> net.test.b -> net.test.a"), qPrintable(problems("net.test.a")));
    QVERIFY(problems("net.test.b").contains("dependency cycle: net.test.b -> net.test.a -> net.test.b"));
    QVERIFY(problems("net.test.c").contains("which has problems"));
    QVERIFY(problems("net.test.core").isEmpty());

    // Force-enable through state.json (as if the dependency broke after enabling) and check resolveForGame.
    QFile st(root.path() + "/state.json");
    QVERIFY(st.open(QIODevice::WriteOnly));
    st.write(R"({"enabled":{"net.test.newer":true,"net.test.gone":true,"net.test.a":true,"net.test.fine":true,"net.test.c":true}})");
    st.close();
    PluginManager m2(root.path());
    m2.setDeveloperMode(true);
    const GameResolution r = m2.resolveForGame("nds", "ABCD");
    QCOMPARE(ids(r), QStringList{"net.test.fine"});
    QVERIFY(r.problems.value("net.test.newer").join(" ").contains("you have 1.2.0"));
    QVERIFY(r.problems.value("net.test.gone").join(" ").contains("not installed"));
    QVERIFY(r.problems.value("net.test.a").join(" ").contains("cycle"));
    QVERIFY(r.problems.contains("net.test.c"));
    QVERIFY(!r.problems.contains("net.test.core"));
    QVERIFY(!r.problems.contains("net.test.fine"));
  }

  void linkedFolderLiveReload() {
    QTemporaryDir root, src;
    PluginManager m(root.path());
    PluginSpec s;
    s.id = "net.test.dev";
    s.lua = "-- v1";
    const QString dir = writePlugin(src.path(), s);
    const InstallResult r = m.linkDevFolder(dir);
    QVERIFY2(r.ok, qPrintable(r.error));
    PluginInfo i = m.list().first();
    QVERIFY(i.devLinked);
    QCOMPARE(QFileInfo(i.path).canonicalFilePath(), QFileInfo(dir).canonicalFilePath());
    QVERIFY(!QFileInfo(root.path() + "/installed/net.test.dev").exists());  // runs straight from the folder

    QSignalSpy spy(&m, &PluginManager::pluginChanged);
    QFile f(dir + "/main.lua");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("-- v2");
    f.close();
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    QCOMPARE(spy.first().first().toString(), QString("net.test.dev"));

    // a burst of writes is debounced into (about) one signal
    spy.clear();
    for (int k = 0; k < 5; ++k) {
      QFile g(dir + "/main.lua");
      g.open(QIODevice::WriteOnly);
      g.write(QByteArray("-- burst ") + QByteArray::number(k));
      g.close();
      QTest::qWait(30);
    }
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    QTest::qWait(600);
    QVERIFY(spy.count() <= 2);

    // changing the manifest is picked up too
    spy.clear();
    PluginSpec s2 = s;
    s2.version = "1.1.0";
    writePlugin(src.path(), s2);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    QCOMPARE(m.list().first().manifest.version.toString(), QString("1.1.0"));

    // link survives a restart; uninstall only removes the link, never the folder
    { PluginManager again(root.path()); QVERIFY(again.list().first().devLinked); }
    QVERIFY(m.uninstall("net.test.dev"));
    QVERIFY(m.list().isEmpty());
    QVERIFY(QFileInfo(dir + "/main.lua").isFile());
  }
};

QTEST_MAIN(ManagerTest)
#include "manager_test.moc"
