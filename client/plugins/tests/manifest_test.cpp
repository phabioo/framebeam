#include <QTemporaryDir>
#include <QtTest>

#include "plugin_test_util.h"

using namespace framebeam::plugins;
using framebeam::update::SemVer;

class ManifestTest : public QObject {
  Q_OBJECT

  static QByteArray base(const QString& patch = {}) {
    QJsonObject o = QJsonDocument::fromJson(R"({"id":"net.example.tracker","name":"Tracker","version":"1.2.3","api":1,"entry":"main.lua"})").object();
    if (!patch.isEmpty()) {
      const QJsonObject p = QJsonDocument::fromJson(patch.toUtf8()).object();
      for (auto it = p.begin(); it != p.end(); ++it) {
        if (it.value().isNull()) o.remove(it.key());
        else o.insert(it.key(), it.value());
      }
    }
    return QJsonDocument(o).toJson();
  }
  static QString errOf(const QString& patch) {
    const ManifestResult r = parseManifest(base(patch));
    return r.ok ? QString() : r.error;
  }

 private slots:
  void validFull() {
    const ManifestResult r = parseManifest(R"({
      "id": "net.framebeam.example", "name": "Example", "version": "1.0.0", "api": 1, "type": "script",
      "requires": { "net.framebeam.example-core": "^1.0" }, "provides": ["example.team"],
      "games": [{ "system": "nds", "codes": ["ABC*"] }],
      "permissions": ["memory.read", "rom.read", "ui", "run.state"], "entry": "main.lua",
      "assets": ["icons/", "i18n/"], "settings": { "a": 1 } })");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.manifest.id, QString("net.framebeam.example"));
    QCOMPARE(r.manifest.dependencies.size(), 1);
    QCOMPARE(r.manifest.provides, QStringList{"example.team"});
    QCOMPARE(r.manifest.permissions.size(), 4);
    QVERIFY(r.manifest.matchesGame("nds", "ABCD"));
    QVERIFY(r.manifest.matchesGame("NDS", "abce"));
    QVERIFY(!r.manifest.matchesGame("nds", "XBCD"));
    QVERIFY(!r.manifest.matchesGame("gba", "ABCD"));
    QCOMPARE(r.manifest.settings.value("a").toInt(), 1);
  }
  void defaultsAllGames() {
    const ManifestResult r = parseManifest(base());
    QVERIFY(r.ok);
    QVERIFY(r.manifest.matchesGame("3ds", "anything"));
    QCOMPARE(r.manifest.type, QString("script"));
  }
  void apiVersion() {
    QCOMPARE(errOf(R"({"api":2})"), QString("Tracker needs plugin API 2; this Player supports API 1"));
    QVERIFY(errOf(R"({"api":null})").contains("'api' is required"));
  }
  void typeTool() {
    QCOMPARE(errOf(R"({"type":"tool"})"), QString("tool plugins are not supported yet"));
    QVERIFY(errOf(R"({"type":"weird"})").contains("unknown plugin type"));
  }
  void permissions() {
    QCOMPARE(errOf(R"({"permissions":["memory.write"]})"), QString("permission memory.write is not available yet"));
    QCOMPARE(errOf(R"({"permissions":["tool.exec"]})"), QString("permission tool.exec is not available yet"));
    QVERIFY(errOf(R"({"permissions":["network"]})").contains("unknown permission 'network'"));
  }
  void ids() {
    QVERIFY(errOf(R"({"id":"Tracker"})").contains("invalid plugin id"));
    QVERIFY(errOf(R"({"id":"tracker"})").contains("invalid plugin id"));
    QVERIFY(errOf(R"({"id":"net..x"})").contains("invalid plugin id"));
    QVERIFY(errOf(R"({"id":"net.example.a_b"})").contains("invalid plugin id"));
    QVERIFY(errOf(R"({"id":null})").contains("'id' is required"));
  }
  void versionAndName() {
    QVERIFY(errOf(R"({"version":"1.0"})").contains("invalid version"));
    QVERIFY(errOf(R"({"name":""})").contains("'name'"));
  }
  void entryRules() {
    QVERIFY(errOf(R"({"entry":"../x.lua"})").contains("invalid entry"));
    QVERIFY(errOf(R"({"entry":"/etc/x.lua"})").contains("invalid entry"));
    QVERIFY(errOf(R"({"entry":"main.txt"})").contains("invalid entry"));
    QVERIFY(errOf(R"({"entry":null})").contains("'entry' is required"));
    QTemporaryDir t;
    testutil::PluginSpec s;
    const QString dir = testutil::writePlugin(t.path(), s);
    QVERIFY(loadManifest(dir).ok);
    QFile::remove(dir + "/main.lua");
    QVERIFY(loadManifest(dir).error.contains("does not exist"));
    QVERIFY(!loadManifest(t.path() + "/nothing").ok);
  }
  void badJson() {
    QVERIFY(!parseManifest("{nope").ok);
    QVERIFY(!parseManifest("[]").ok);
  }
  void ranges() {
    auto m = [](const char* r, const char* v) {
      const auto range = VersionRange::parse(r);
      return range && range->matches(*SemVer::parse(v));
    };
    QVERIFY(m("^1.2", "1.2.0")); QVERIFY(m("^1.2", "1.9.9")); QVERIFY(!m("^1.2", "2.0.0")); QVERIFY(!m("^1.2", "1.1.9"));
    QVERIFY(m("^0.2", "0.2.7")); QVERIFY(!m("^0.2", "0.3.0"));
    QVERIFY(m("~1.2", "1.2.9")); QVERIFY(!m("~1.2", "1.3.0")); QVERIFY(m("~1", "1.9.0")); QVERIFY(!m("~1", "2.0.0"));
    QVERIFY(m(">=1.0", "1.0.0")); QVERIFY(m(">=1.0", "7.0.0")); QVERIFY(!m(">=1.0", "0.9.0"));
    QVERIFY(m("1.2.3", "1.2.3")); QVERIFY(!m("1.2.3", "1.2.4"));
    QVERIFY(m("*", "9.9.9"));
    QVERIFY(!m("^2.0", "2.0.0-beta.1"));
    QVERIFY(!VersionRange::parse("nonsense")); QVERIFY(!VersionRange::parse("^a.b")); QVERIFY(!VersionRange::parse(">=")); QVERIFY(!VersionRange::parse("1.2.3.4"));
    QVERIFY(errOf(R"({"requires":{"net.x.y":"huh"}})").contains("invalid version range"));
    QVERIFY(errOf(R"({"requires":{"net.example.tracker":"*"}})").contains("cannot require itself"));
  }
  void globs() {
    QVERIFY(globMatch("AB?D", "ABCD")); QVERIFY(!globMatch("AB?D", "ABD")); QVERIFY(globMatch("*", "")); QVERIFY(globMatch("A*D*", "ABCDE"));
    QVERIFY(!globMatch("A*D", "ABCDE")); QVERIFY(globMatch("a*", "ABC"));
    QVERIFY(errOf(R"({"games":[{"codes":["A"]}]})").contains("'system'"));
  }
  void providesAndAssets() {
    QVERIFY(errOf(R"({"provides":"x"})").contains("array"));
    QVERIFY(errOf(R"({"assets":["../x"]})").contains("invalid asset path"));
  }
};

QTEST_APPLESS_MAIN(ManifestTest)
#include "manifest_test.moc"
