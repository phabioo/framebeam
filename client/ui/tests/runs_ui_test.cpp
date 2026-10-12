// Runs in the Player UI (docs/design/plugins.md 4j-2): the sidebar entry (Hub announces runs_v1 and an installed plugin has
// run.state), the invite card with the game picker, the run detail with participants, status actions and the event log,
// "Start a run", and "Play run" (Hub copies of the run's plugins, the run's own save slot). Synthetic plugins and a fake
// Hub only. With FRAMEBEAM_SCREENSHOT_DIR=<dir> the views are also saved as PNGs (dark and light) for the review.
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "fakehub.h"
#include "fbplugin_testutil.h"
#include "runtypes.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;
using namespace framebeam::plugins::testutil;

namespace {

const QString kPluginId = QStringLiteral("net.test.runs");
const QString kGameId = QStringLiteral("00000000-0000-4000-8000-0000000000a1");

void pair(Harness& h, FakeHub& hub) {
  h.controller->addHub(hub.address());
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation, 8000);
  h.controller->confirmTrust();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), HubConnection::State::NeedsPairing, 8000);
  h.controller->requestPairing();
  QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
}

}  // namespace

class RunsUiTest : public QObject {
  Q_OBJECT
  QString shotDir_;

  void shot(Harness& h, const QString& name) {
    if (shotDir_.isEmpty()) return;
    QQuickTest::qWaitForPolish(h.window);
    QTest::qWait(350);
    QDir().mkpath(shotDir_);
    const QImage img = h.window->grabWindow();
    QVERIFY2(!img.isNull() && !uitest::isAllBlack(img), qPrintable(name));
    QVERIFY(img.save(QDir(shotDir_).filePath(name + QStringLiteral(".png"))));
  }

 private slots:
  void initTestCase() {
    shotDir_ = qEnvironmentVariable("FRAMEBEAM_SCREENSHOT_DIR");
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    uitest::installWarningCounter();
  }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  static bool popupOpen(Harness& h, const char* name) {
    QObject* o = h.window->findChild<QObject*>(QString::fromLatin1(name));
    return o != nullptr && o->property("visible").toBool();
  }

  void runsFlow() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-runs");
    hub.name = QStringLiteral("Home");
    hub.decision = FakeHub::Decision::Approve;
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("plugins_v1"), QStringLiteral("runs_v1"), QStringLiteral("sessions_v1")};
    hub.games = QJsonObject{
        {"games", QJsonArray{QJsonObject{{"id", kGameId},
                                         {"title", "Homebrew Quest"},
                                         {"system", "nds"},
                                         {"rom", QJsonObject{{"sha256", QString(64, QLatin1Char('b'))}, {"size", 4096}, {"filename", "quest.nds"}}},
                                         {"added_at", "2026-01-01T12:00:00Z"}}}}};
    hub.fakeUsers = QJsonArray{QJsonObject{{"id", "u_test_1"}, {"display_name", "Tester"}, {"online", true}},
                               QJsonObject{{"id", "u_lena"}, {"display_name", "Lena"}, {"online", true}}};

    FbSpec spec;
    spec.id = kPluginId;
    spec.name = "Run Notes";
    spec.permissions = {"run.state"};
    const QByteArray file = makeFbPlugin(spec);
    const QString sha = sha256Hex(file);
    hub.pluginList.append(QJsonObject{{"plugin_id", spec.id}, {"version", spec.version}, {"name", spec.name}, {"description", "Notes for runs"}, {"api", 1},
                                      {"origin", "unverified"}, {"sha256", sha}, {"size", file.size()}, {"permissions", QJsonArray{"run.state"}},
                                      {"games", QJsonArray{QJsonObject{{"system", "nds"}}}}, {"created_at", "2026-10-12T10:00:00Z"}});
    hub.pluginFiles.insert(spec.id + "/" + spec.version, file);

    // A run Lena created and invited me to.
    const QJsonArray refs{QJsonObject{{"plugin_id", spec.id}, {"version", spec.version}, {"sha256", sha}}};
    QJsonObject run = hub.makeRun(QStringLiteral("Tide Hardcore"), {QStringLiteral("u_lena"), hub.runsMe}, QStringLiteral("u_lena"), refs);
    const QString runId = run.value("run_id").toString();
    QJsonArray ps = run.value("participants").toArray();
    QJsonObject lena = ps.at(0).toObject();
    lena.insert("user_name", "Lena");
    ps.replace(0, lena);
    run.insert("participants", ps);
    hub.runs.insert(runId, run);
    QVERIFY(hub.start());

    Harness h;
    QVERIFY(h.start());
    h.window->resize(1440, 900);
    pair(h, hub);
    RunsController* runs = h.controller->runs();
    PluginsController* pc = h.controller->plugins();
    QVERIFY(runs != nullptr);

    // No plugin with run.state is installed, but there is an invite: the Runs entry shows (runs_v1) with the invite count.
    QTRY_VERIFY_WITH_TIMEOUT(runs->loaded(), 8000);
    QVERIFY(!pc->hasRunPlugin());
    QTRY_COMPARE_WITH_TIMEOUT(pc->hubPlugins().size(), 1, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(runs->available(), 8000);
    QTRY_VERIFY_WITH_TIMEOUT(h.item("navRuns") != nullptr && h.item("navRuns")->isVisible(), 8000);
    QCOMPARE(runs->pendingInvites(), 1);
    QTRY_VERIFY(h.item("navRunsBadge") != nullptr && h.item("navRunsBadge")->isVisible());

    QVERIFY(h.click("navRuns"));
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("runs"), 8000);
    QTRY_VERIFY(h.item(("runInvite_" + runId).toUtf8().constData()) != nullptr);
    QCOMPARE(h.item("runInviteHeadline")->property("text").toString(), QStringLiteral("Lena invites you to \"Tide Hardcore\""));
    shot(h, QStringLiteral("4j-runs-invite"));
    h.controller->setAppearance(QStringLiteral("light"));
    shot(h, QStringLiteral("4j-runs-invite-light"));
    h.controller->setAppearance(QStringLiteral("dark"));

    // Join with the game picker (only games that fit the plugin's systems are offered).
    QVERIFY(h.click("runInviteJoin"));
    QTRY_VERIFY(popupOpen(h, "runJoinDialog"));
    QTRY_COMPARE_WITH_TIMEOUT(runs->games()->rowCount(), 1, 8000);
    // Every plugin of the run is listed with name, version, origin pill, permissions and the unverified warning.
    QCOMPARE(runs->joinPlugins()->rowCount(), 1);
    QTRY_VERIFY(h.item(("runJoinPlugin_" + spec.id).toUtf8().constData()) != nullptr);
    QCOMPARE(h.item("runJoinPluginOrigin")->property("text").toString(), QStringLiteral("! Hub upload · unverified"));
    QVERIFY(runs->joinUnverified());
    QTRY_VERIFY(h.item("runJoinUnverified") != nullptr && h.item("runJoinUnverified")->isVisible());
    QCOMPARE(h.item("runJoinUnverified")->property("text").toString(), runs->unverifiedWarning());
    QVERIFY(runs->unverifiedWarning().startsWith(QStringLiteral("FrameBeam has not checked this plugin.")));
    shot(h, QStringLiteral("4j-runs-join"));
    QVERIFY(!h.item("runJoinConfirm")->isEnabled() || runs->games()->rowCount() == 1);
    QVERIFY(h.click(("runGame_" + kGameId).toUtf8().constData()));
    QVERIFY(h.click("runJoinConfirm"));
    QTRY_VERIFY_WITH_TIMEOUT(runs->openRunId() == runId, 8000);
    QCOMPARE(hub.lastRunBody.value("game_id").toString(), kGameId);
    QTRY_COMPARE_WITH_TIMEOUT(runs->pendingInvites(), 0, 8000);
    // Joining is the consent: the Hub copy of the plugin is fetched without a further question.
    QTRY_VERIFY_WITH_TIMEOUT(!pc->manager()->hubCopyDir(spec.id, spec.version, sha).isEmpty(), 8000);
    QTRY_VERIFY_WITH_TIMEOUT(!runs->busy(), 8000);
    QTRY_VERIFY(h.item("runPlay") != nullptr && h.item("runPlay")->isVisible());
    QCOMPARE(h.item("runDetailName")->property("text").toString(), QStringLiteral("Tide Hardcore"));
    QTRY_COMPARE_WITH_TIMEOUT(runs->participants()->rowCount(), 2, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(runs->events()->rowCount() >= 2, 8000);  // run.created + run.joined, written by the Hub
    QVERIFY(h.item("runParticipant_u_lena") != nullptr);
    shot(h, QStringLiteral("4j-runs-detail"));

    // Status: the creator starts the run; any member can pause and resume it.
    QVERIFY(h.item("runStart") == nullptr || !h.item("runStart")->isVisible());  // not the creator
    {
      QJsonObject r = hub.runs.value(runId);
      r.insert("status", "active");
      hub.runs.insert(runId, r);
      hub.pushRun(runId);
    }
    QTRY_VERIFY(h.item("runPause") != nullptr && h.item("runPause")->isVisible());
    QTRY_VERIFY(h.item("runPause")->isEnabled());
    QVERIFY(h.click("runPause"));
    QTRY_COMPARE_WITH_TIMEOUT(hub.runs.value(runId).value("status").toString(), QStringLiteral("paused"), 8000);
    QTRY_VERIFY(h.item("runResume") != nullptr && h.item("runResume")->isVisible());
    QVERIFY(h.item("runDetailLine")->property("text").toString().startsWith(QStringLiteral("Paused by")));
    shot(h, QStringLiteral("4j-runs-detail-paused"));
    QTRY_VERIFY(h.item("runResume") != nullptr && h.item("runResume")->isEnabled());
    QVERIFY(h.click("runResume"));
    QTRY_COMPARE_WITH_TIMEOUT(hub.runs.value(runId).value("status").toString(), QStringLiteral("active"), 8000);

    // Play run: the Hub copy of every plugin is prepared, then the game starts with the run's save slot.
    QSignalSpy launch(runs, &RunsController::launchRequested);
    QTRY_VERIFY(h.item("runPlay") != nullptr && h.item("runPlay")->isEnabled());
    QVERIFY(h.click("runPlay"));
    QTRY_COMPARE_WITH_TIMEOUT(launch.size(), 1, 8000);
    QCOMPARE(launch.first().at(0).toString(), runId);
    QCOMPARE(launch.first().at(1).toString(), kGameId);
    QVERIFY(!pc->manager()->hubCopyDir(spec.id, spec.version, sha).isEmpty());
    QCOMPARE(h.controller->saveSync()->slotFor(kGameId), runSaveSlot(runId));
    QVERIFY(runSaveSlot(runId).startsWith(QStringLiteral("run-")) && runSaveSlot(runId).size() == 12);
    const RunLaunch ctx = runs->launchFor(runId);
    QVERIFY(ctx.bridge != nullptr);
    QCOMPARE(ctx.pins.size(), 1);
    QCOMPARE(ctx.pins.first().sha256, sha);
    // The game did not get going (no core in this test): a normal start selects the game's own slot again.
    h.controller->playSelected();
    QCOMPARE(h.controller->saveSync()->slotFor(kGameId), QStringLiteral("default"));

    // Back to the list: the run sits in the Active group.
    runs->closeRun();
    QTRY_VERIFY(h.item(("runRow_" + runId).toUtf8().constData()) != nullptr);
    QCOMPARE(runs->activeTitle(), QStringLiteral("ACTIVE · 1"));
    shot(h, QStringLiteral("4j-runs-list"));

    // Start a run: name, plugin, a player and the own game.
    QVERIFY(h.click("runsStartButton"));
    QTRY_VERIFY(popupOpen(h, "runStartDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(!runs->startUsersLoading(), 8000);
    QTRY_COMPARE_WITH_TIMEOUT(runs->startPlugins()->rowCount(), 1, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(runs->startUsers()->rowCount(), 1, 8000);  // not myself
    h.item("runNameField")->setProperty("text", QStringLiteral("Summer Duo"));
    QVERIFY(h.item("runStartUnverified") == nullptr || !h.item("runStartUnverified")->isVisible());  // nothing picked yet
    shot(h, QStringLiteral("4j-runs-start"));
    QVERIFY(h.click("runStartConfirm"));  // nothing picked yet
    QTRY_VERIFY(!runs->startError().isEmpty());
    QVERIFY(h.click(("runPlugin_" + kPluginId).toUtf8().constData()));
    QVERIFY(runs->startUnverified());
    QTRY_VERIFY(h.item("runStartUnverified") != nullptr && h.item("runStartUnverified")->isVisible());
    QCOMPARE(h.item("runPluginOrigin")->property("text").toString(), QStringLiteral("! Hub upload · unverified"));
    QVERIFY(h.click("runUser_u_lena"));
    QVERIFY(h.click(("runGame_" + kGameId).toUtf8().constData()));
    QVERIFY(h.click("runStartConfirm"));
    QTRY_VERIFY_WITH_TIMEOUT(hub.runs.size() == 2, 8000);
    QCOMPARE(hub.lastRunBody.value("name").toString(), QStringLiteral("Summer Duo"));
    QCOMPARE(hub.lastRunBody.value("invite_user_ids").toArray().size(), 1);
    QCOMPARE(hub.lastRunBody.value("game_id").toString(), kGameId);
    QTRY_VERIFY_WITH_TIMEOUT(!runs->openRunId().isEmpty() && runs->openRunId() != runId, 8000);  // the new run opens
    QTRY_VERIFY(h.item("runStart") != nullptr && h.item("runStart")->isVisible());               // creator of a starting run
    QTRY_VERIFY(h.item("runStart") != nullptr && h.item("runStart")->isEnabled());
    QVERIFY(h.click("runStart"));
    QTRY_VERIFY_WITH_TIMEOUT(hub.runs.value(runs->openRunId()).value("status").toString() == QLatin1String("active"), 8000);

    // Finishing asks first and ends the run for everyone.
    QTRY_VERIFY(h.item("runFinish") != nullptr && h.item("runFinish")->isEnabled());
    QVERIFY(h.click("runFinish"));
    QTRY_VERIFY(popupOpen(h, "runConfirmDialog"));
    QCOMPARE(h.item("runConfirmTitle")->property("text").toString(), QStringLiteral("Finish this run?"));
    QVERIFY(h.click("runConfirmOk"));
    QTRY_VERIFY_WITH_TIMEOUT(hub.runs.value(runs->openRunId()).value("status").toString() == QLatin1String("finished"), 8000);
    QTRY_VERIFY(h.item("runPause") == nullptr || !h.item("runPause")->isVisible());
    QCOMPARE(runs->detail().value("over").toBool(), true);
    runs->closeRun();
    QTRY_COMPARE_WITH_TIMEOUT(runs->finishedCount(), 1, 8000);
  }

  void entryHiddenWithoutRunsAndPlugin() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-empty");
    hub.decision = FakeHub::Decision::Approve;
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("plugins_v1"), QStringLiteral("runs_v1"), QStringLiteral("sessions_v1")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->runs()->loaded(), 8000);
    QVERIFY(!h.controller->runs()->available());  // runs_v1, but no run, no invite and no run plugin
    QVERIFY(h.item("navRuns") == nullptr || !h.item("navRuns")->isVisible());
  }

  void entryStaysHiddenWithoutHubSupport() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-noruns");
    hub.decision = FakeHub::Decision::Approve;
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("plugins_v1")};  // no runs_v1
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QVERIFY(!h.controller->runs()->available());
    h.controller->showRuns();
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
    QVERIFY(h.item("navRuns") == nullptr || !h.item("navRuns")->isVisible());
  }
  // The game picker keeps what every plugin of the run accepts: system and, when the Hub knows it, the game code (globs,
  // case-insensitive). Unknown codes stay after the matching games with a hint; a known code that matches nothing is dropped.
  void gamePickerFiltersByGameCode() {
    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-codes");
    hub.decision = FakeHub::Decision::Approve;
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("plugins_v1"), QStringLiteral("runs_v1"), QStringLiteral("sessions_v1")};
    const auto game = [](const char* id, const char* title, const char* system, const char* code) {
      return QJsonObject{{"id", id},
                         {"title", title},
                         {"system", system},
                         {"game_code", code},
                         {"game_code_kind", QString::fromLatin1(code).isEmpty() ? "" : "nds-gamecode"},
                         {"rom", QJsonObject{{"sha256", QString(64, QLatin1Char('b'))}, {"size", 4096}, {"filename", "x.nds"}}},
                         {"added_at", "2026-01-01T12:00:00Z"}};
    };
    hub.games = QJsonObject{{"games", QJsonArray{game("g-alpha", "Alpha", "nds", "ABCE"), game("g-zeta", "Zeta", "nds", "BXYZ"),
                                                 game("g-unknown", "Unknown", "nds", ""), game("g-aaa", "Aaa unreadable", "nds", ""),
                                                 game("g-other", "Other", "nds", "CCCC"), game("g-beta", "Beta", "nds", "aqqe"),
                                                 game("g-gba", "Advance", "gba", "")}}};
    hub.fakeUsers = QJsonArray{QJsonObject{{"id", "u_test_1"}, {"display_name", "Tester"}, {"online", true}},
                               QJsonObject{{"id", "u_lena"}, {"display_name", "Lena"}, {"online", true}}};
    const auto offer = [&](const QString& id, const QJsonArray& games) {
      hub.pluginList.append(QJsonObject{{"plugin_id", id}, {"version", "1.0.0"}, {"name", id}, {"description", ""}, {"api", 1},
                                        {"origin", "unverified"}, {"sha256", QString(64, QLatin1Char('c'))}, {"size", 10},
                                        {"permissions", QJsonArray{"run.state"}}, {"games", games}, {"created_at", "2026-10-12T10:00:00Z"}});
    };
    offer(QStringLiteral("net.test.one"), QJsonArray{QJsonObject{{"system", "nds"}, {"codes", QJsonArray{"A??E", "BXYZ"}}}});
    offer(QStringLiteral("net.test.two"), QJsonArray{QJsonObject{{"system", "NDS"}, {"codes", QJsonArray{"A*"}}}});
    offer(QStringLiteral("net.test.any"), QJsonArray{QJsonObject{{"system", "nds"}}});
    const auto ref = [](const char* id) {
      return QJsonObject{{"plugin_id", id}, {"version", "1.0.0"}, {"sha256", QString(64, QLatin1Char('c'))}};
    };
    const auto addRun = [&](const QJsonArray& refs) {
      QJsonObject run = hub.makeRun(QStringLiteral("Run"), {QStringLiteral("u_lena"), hub.runsMe}, QStringLiteral("u_lena"), refs);
      hub.runs.insert(run.value("run_id").toString(), run);
      return run.value("run_id").toString();
    };
    const QString one = addRun({ref("net.test.one")});
    const QString both = addRun({ref("net.test.one"), ref("net.test.two")});
    const QString any = addRun({ref("net.test.any")});
    QVERIFY(hub.start());

    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    RunsController* runs = h.controller->runs();
    QTRY_VERIFY_WITH_TIMEOUT(runs->loaded(), 8000);
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->plugins()->hubPlugins().size(), 3, 8000);

    const auto rowsOf = [&]() {
      QList<QVariantMap> out;
      QAbstractItemModel* m = runs->games();
      const QHash<int, QByteArray> names = m->roleNames();
      for (int r = 0; r < m->rowCount(); ++r) {
        QVariantMap row;
        for (auto it = names.begin(); it != names.end(); ++it) row.insert(QString::fromUtf8(it.value()), m->data(m->index(r, 0), it.key()));
        out.append(row);
      }
      return out;
    };
    const auto titles = [&](QStringList* hints = nullptr) {
      QStringList out;
      for (const QVariantMap& r : rowsOf()) {
        out << r.value("title").toString();
        if (hints != nullptr) *hints << r.value("hint").toString();
      }
      return out;
    };
    // One plugin: glob A??E matches ABCE and AQQE (any case), BXYZ is listed literally; CCCC and the GBA game are dropped.
    // Games whose code the Hub does not know stay, after the matching ones, with a hint.
    QStringList hints;
    QTRY_COMPARE_WITH_TIMEOUT((runs->prepareGames(one), titles(&(hints = QStringList()))),
                              (QStringList{"Alpha", "Beta", "Zeta", "Aaa unreadable", "Unknown"}), 8000);
    QCOMPARE(hints, (QStringList{"", "", "", "Game not recognized", "Game not recognized"}));
    // Two plugins: the intersection (A* drops Zeta).
    runs->prepareGames(both);
    QCOMPARE(titles(), (QStringList{"Alpha", "Beta", "Aaa unreadable", "Unknown"}));
    // A plugin whose pattern has no codes accepts every game of the system, known or not.
    runs->prepareGames(any);
    QCOMPARE(titles(&(hints = QStringList())), (QStringList{"Aaa unreadable", "Alpha", "Beta", "Other", "Unknown", "Zeta"}));
    QCOMPARE(hints, QStringList(6));

    // The Start dialog shows the same filtered list for the ticked plugins.
    runs->beginStart();
    QTRY_COMPARE_WITH_TIMEOUT(runs->startPlugins()->rowCount(), 3, 8000);
    runs->toggleStartPlugin(QStringLiteral("net.test.one"));
    QCOMPARE(titles(), (QStringList{"Alpha", "Beta", "Zeta", "Aaa unreadable", "Unknown"}));
    runs->toggleStartPlugin(QStringLiteral("net.test.two"));
    QCOMPARE(titles(), (QStringList{"Alpha", "Beta", "Aaa unreadable", "Unknown"}));
    // A game that no longer fits cannot be started with.
    runs->startRun(QStringLiteral("Mismatch"), QStringLiteral("g-zeta"));
    QVERIFY(!runs->startError().isEmpty());
    QCOMPARE(hub.runs.size(), 3);
  }

};

QTEST_MAIN(RunsUiTest)
#include "runs_ui_test.moc"
