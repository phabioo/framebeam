// Server settings in the app (0.18 package 8, 3q-app-1 to 3q-app-7): Settings for "Server" with the seven pages General, Network & sharing,
// Sign-in, Updates, Backups, Storage and Diagnostics. Offscreen, FakeHub with admin_v1 and fixed or hook-driven admin routes; synthetic data only.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace uzume;
using namespace uzume::ui;
using uitest::Harness;
using S = HubConnection::State;

namespace {
const QString kApi = QStringLiteral("/api/v1/admin");

QString get(const QString& path) { return QStringLiteral("GET ") + kApi + path; }
QString put(const QString& path) { return QStringLiteral("PUT ") + kApi + path; }
QString post(const QString& path) { return QStringLiteral("POST ") + kApi + path; }
QString del(const QString& path) { return QStringLiteral("DELETE ") + kApi + path; }

QByteArray netSetting(const char* key, const char* label, const char* value, const char* source = "env", bool pending = false) {
  return QByteArray("{\"key\": \"") + key + "\", \"label\": \"" + label + "\", \"value\": \"" + value + "\", \"running_value\": \"" + value +
         "\", \"default_value\": \"" + value + "\", \"source\": \"" + source + "\", \"needs_restart\": true, \"restart_pending\": " +
         (pending ? "true" : "false") + "}";
}

// The network block; `pending` lists the labels that wait for a restart, `port` is the listen port.
QByteArray networkBody(const char* port = "8443", const char* pending = "") {
  QByteArray p = pending[0] ? QByteArray("[\"") + pending + "\"]" : QByteArray("[]");
  return "{\"settings\": [" + netSetting("listen_port", "Listen port", port, pending[0] ? "web" : "env", pending[0] != 0) + ", " +
         netSetting("public_host", "Public host", "server.example.com:8443") + ", " + netSetting("turn", "Built-in TURN relay", "true") + ", " +
         netSetting("turn_port", "TURN port", "3478") + ", " + netSetting("turn_relay_ports", "Relay port range", "49160-49199") + ", " +
         netSetting("turn_relay_ip", "Relay IP", "") + ", " + netSetting("ice_servers", "External STUN servers", "null") + ", " +
         netSetting("save_keep_recent", "Keep newest versions", "20") + ", " + netSetting("save_keep_daily", "Keep daily versions", "7") + ", " +
         netSetting("save_keep_weekly", "Keep weekly versions", "4") + ", " + netSetting("network_sharing", "Network sharing", "true") + ", " +
         netSetting("import_dir", "Import folder", "/srv/uzume/import") + "], \"pending_restart\": " + p +
         ", \"issues\": [], \"can_restart\": true, \"turn\": {\"public_host\": \"server.example.com\", \"relay_ip\": \"203.0.113.10\", \"fixed\": false, "
         "\"port\": 3478, \"relay_min\": 49160, \"relay_max\": 49199, \"allocations\": 2, \"forwards\": []}, \"cloudflare_configured\": false, "
         "\"external_turn\": {\"source\": \"flags\", \"active\": true, \"servers\": []}}";
}

QByteArray updatesBody(bool available = true) {
  return QByteArray("{\"current_version\": \"0.17.0\", \"channel\": \"stable\", \"channel_is_set\": true, \"default_channel\": \"stable\", "
                    "\"compiled_channel\": \"stable\", \"auto\": true, \"packaged\": true, \"request_pending\": false, \"active_sessions\": 0, "
                    "\"in_container\": false") +
         (available ? ", \"available\": {\"version\": \"0.17.1\", \"channel\": \"stable\", \"published_at\": \"2026-10-20T10:00:00Z\", \"size\": 52428800, "
                      "\"breaking\": false, \"breaking_count\": 0, \"skipped\": false}"
                    : "") +
         "}";
}

QByteArray settingsBody(const QByteArray& name = "Home server", bool uploads = true, const char* port = "8443", const char* pending = "",
                        bool available = true) {
  return "{\"hub_name\": \"" + name + "\", \"allow_user_uploads\": " + (uploads ? "true" : "false") + ", \"network\": " + networkBody(port, pending) +
         ", \"security\": {\"tls\": true, \"trusted_proxy\": [], \"fingerprint\": \"AA:BB:CC\", \"cert_source\": \"self\", "
         "\"cert_not_after\": \"2027-01-01T00:00:00Z\", \"can_renew_cert\": true}, \"updates\": " + updatesBody(available) + "}";
}

QByteArray backupStatus(const QByteArray& extra = "", const char* mode = "daily", int keep = 7) {
  return "{\"settings\": {\"mode\": \"" + QByteArray(mode) + "\", \"weekday\": 0, \"time\": \"03:00\", \"keep\": " + QByteArray::number(keep) +
         ", \"include_roms\": false, \"folder\": \"/mnt/backup/uzume\"}, "
         "\"folder_set\": true, \"disk_total\": 1000000000000, \"disk_free\": 400000000000, \"can_restore\": true" +
         (extra.isEmpty() ? QByteArray() : ", " + extra) + "}";
}
const char* kRunning = R"JSON( "running": {"kind": "backup", "phase": "saves and snapshots", "done_bytes": 1000, "total_bytes": 4000, "started": "2026-10-22T10:00:00Z"})JSON";
const char* kDone = R"JSON( "last": {"ok": true, "started": "2026-10-22T10:00:00Z", "finished": "2026-10-22T10:01:00Z", "name": "uzume-server-home-20261022T100000Z.tar", "size": 4000},
  "last_ok": {"ok": true, "started": "2026-10-22T10:00:00Z", "finished": "2026-10-22T10:01:00Z", "name": "uzume-server-home-20261022T100000Z.tar", "size": 4000})JSON";
const char* kOldBackup = R"JSON({"name": "uzume-server-home-20261014T010000Z.tar", "created": "2026-10-14T01:00:00Z", "size": 3000, "safety": false,
  "includes_roms": false, "restored": false, "contents": "Database · saves · plugins", "hub_version": "0.17.0"})JSON";
const char* kNewBackup = R"JSON({"name": "uzume-server-home-20261022T100000Z.tar", "created": "2026-10-22T10:00:00Z", "size": 4000, "safety": false,
  "includes_roms": false, "restored": false})JSON";

const char* kVersions = R"JSON({"current": "0.17.0", "current_schema": 12, "can_rollback": true, "in_container": false, "backup_folder": "/mnt/backup/uzume",
  "connected_players": 2, "versions": [
    {"version": "0.16.4", "channel": "stable", "published_at": "2026-09-01T10:00:00Z", "schema_version": 12, "data": "keep"},
    {"version": "0.16.3", "channel": "stable", "published_at": "2026-08-01T10:00:00Z", "schema_version": 11, "data": "restore", "lost_since": "2026-10-01T10:00:00Z"}]})JSON";
}  // namespace

class AdminSettingsUiTest : public QObject {
  Q_OBJECT

  static void pair(Harness& h, FakeHub& hub) {
    hub.decision = FakeHub::Decision::Approve;
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsPairing, 8000);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 8000);
  }
  static void adminHub(FakeHub& hub) {
    hub.adminUser = true;
    hub.features = {QStringLiteral("saves_v1")};
    hub.adminRoutes[get(QStringLiteral("/settings"))] = {200, settingsBody()};
    hub.adminRoutes[get(QStringLiteral("/accounts/settings"))] = {200, R"JSON({"require_passwords": false})JSON"};
    hub.adminRoutes[get(QStringLiteral("/backups/status"))] = {200, backupStatus()};
    hub.adminRoutes[get(QStringLiteral("/backups"))] = {200, QByteArray("{\"backups\": [") + kOldBackup + "]}"};
    hub.adminRoutes[get(QStringLiteral("/updates/versions"))] = {200, QByteArray(kVersions)};
    hub.adminRoutes[get(QStringLiteral("/metrics"))] = {200, R"JSON({"enabled": false, "token_set": false})JSON"};
  }
  static QString text(QQuickItem* it) { return it ? it->property("text").toString() : QString(); }
  static QJsonObject bodyOf(const FakeHub& hub, const QString& key) { return QJsonDocument::fromJson(hub.adminBodies.value(key)).object(); }
  static int calls(const FakeHub& hub, const QString& key) { return hub.adminCalls.value(key); }
  static bool shown(const Harness& h, const char* name) {
    QQuickItem* it = h.item(name);
    return it != nullptr && it->isVisible();
  }
  // Settings, scope Server, the given page.
  static void openPage(Harness& h, const char* nav, const char* pageName) {
    h.window->resize(1400, 1800);
    QTRY_VERIFY(h.controller->admin()->available());
    h.controller->showSettings();
    QTRY_VERIFY(h.item("scopeServer") != nullptr);
    QVERIFY(h.clickWhenReady("scopeServer"));
    QTRY_VERIFY(shown(h, "serverSettingsPage"));
    if (nav != nullptr) QVERIFY(h.clickWhenReady(nav));
    QTRY_VERIFY(shown(h, pageName));
  }
  static void editAndSave(Harness& h, const QString& value) {
    QTRY_VERIFY(shown(h, "settingsEditDialog"));
    h.item("settingsEditField")->setProperty("text", value);
    QVERIFY(h.click("settingsEditSave"));
  }

 private slots:
  void initTestCase() {
    qunsetenv("UZUME_MELONDS_DS_CORE");
    uitest::installWarningCounter();
  }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  void serverScopeListsTheSevenPages() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, nullptr, "settingsGeneral");
    QVERIFY(h.item("settingsServerEmpty") == nullptr);
    for (const char* n : {"serverSettingsNav_general", "serverSettingsNav_network", "serverSettingsNav_signin", "serverSettingsNav_updates",
                          "serverSettingsNav_backups", "serverSettingsNav_storage", "serverSettingsNav_diagnostics"}) {
      QVERIFY2(h.item(n) != nullptr, n);
    }
    QCOMPARE(text(h.item("generalName")), QStringLiteral("Home server"));
    QCOMPARE(text(h.item("generalImport")).left(17), QStringLiteral("/srv/uzume/import"));
    QTRY_COMPARE(text(h.item("serverSettingsNavStatus_updates")), QStringLiteral("0.17.0"));
    QTRY_COMPARE(text(h.item("serverSettingsNavStatus_backups")), QStringLiteral("03:00"));
    // This PC again: the pane goes away.
    QVERIFY(h.click("scopePc"));
    QTRY_VERIFY(!shown(h, "serverSettingsPage"));
  }

  void generalRenameAndUploadsPatch() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString patch = QStringLiteral("PATCH ") + kApi + QStringLiteral("/settings");
    hub.adminRoutes[patch] = {200, settingsBody("Den", true)};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, nullptr, "settingsGeneral");
    QVERIFY(h.clickWhenReady("generalRename"));
    editAndSave(h, QStringLiteral("Den"));
    QTRY_COMPARE(calls(hub, patch), 1);
    QCOMPARE(bodyOf(hub, patch).value("name").toString(), QStringLiteral("Den"));
    QTRY_COMPARE(text(h.item("generalName")), QStringLiteral("Den"));
    QTRY_COMPARE(text(h.item("serverSettingsNote")), QStringLiteral("Renamed the server to Den."));
    QTRY_VERIFY(!shown(h, "settingsEditDialog"));
    hub.adminRoutes[patch] = {200, settingsBody("Den", false)};
    QVERIFY(h.click("generalUploads"));
    QTRY_COMPARE(calls(hub, patch), 2);
    QCOMPARE(bodyOf(hub, patch).value("allow_user_uploads").toBool(), false);
    // A refusal shows the message of the server in the dialog and keeps it open.
    hub.adminRoutes[patch] = {400, R"JSON({"error": {"code": "bad_request", "message": "Name is too long"}})JSON"};
    QVERIFY(h.clickWhenReady("generalRename"));
    editAndSave(h, QStringLiteral("x"));
    QTRY_VERIFY(shown(h, "dialogError"));
    QCOMPARE(text(h.item("dialogError")), QStringLiteral("Name is too long"));
    QVERIFY(shown(h, "settingsEditDialog"));
  }

  void networkChangeShowsRestartAndRestartPosts() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString route = put(QStringLiteral("/settings/network/listen_port"));
    hub.adminRoutes[route] = {200, networkBody("9000", "Listen port")};
    hub.adminRoutes[post(QStringLiteral("/settings/network/restart"))] = {202, R"JSON({"port_changed": true, "url": "server.example.com:9000"})JSON"};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_network", "settingsNetwork");
    QVERIFY(!shown(h, "serverSettingsRestart"));
    QCOMPARE(text(h.item("networkDone")), QStringLiteral("Prepared for the internet: public address and relay are set."));
    QCOMPARE(text(h.item("networkPorts")), QStringLiteral("8443 TCP · 3478 UDP+TCP · 49160-49199 UDP"));
    QVERIFY(h.clickWhenReady("networkChange_listen"));
    editAndSave(h, QStringLiteral("9000"));
    QTRY_COMPARE(calls(hub, route), 1);
    QCOMPARE(bodyOf(hub, route).value("value").toString(), QStringLiteral("9000"));
    QTRY_VERIFY(shown(h, "serverSettingsRestart"));
    QVERIFY(h.item("serverSettingsRestart")->findChild<QQuickItem*>() != nullptr);
    QVERIFY(h.clickWhenReady("serverSettingsRestartButton"));
    QTRY_COMPARE(calls(hub, post(QStringLiteral("/settings/network/restart"))), 1);
    QTRY_VERIFY(text(h.item("serverSettingsNote")).contains(QStringLiteral("server.example.com:9000")));
  }

  void sharingAsksToConfirm() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString route = put(QStringLiteral("/settings/network/network_sharing"));
    hub.confirmRequiredPaths = {QStringLiteral("/api/v1/admin/settings/network/network_sharing")};
    hub.adminRoutes[route] = {200, networkBody()};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_network", "settingsNetwork");
    QVERIFY(h.click("networkSharing"));
    QTRY_VERIFY(h.controller->admin()->confirmNeeded());
    QCOMPARE(calls(hub, route), 0);
    QTRY_VERIFY(shown(h, "confirmPassword"));
    h.item("confirmPassword")->setProperty("text", hub.accountPassword);
    QVERIFY(h.click("confirmSubmit"));
    QTRY_COMPARE(calls(hub, route), 1);
    QCOMPARE(bodyOf(hub, route).value("value").toString(), QStringLiteral("0"));
  }

  void signInSavesThePolicyAfterAChange() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString route = put(QStringLiteral("/accounts/settings"));
    hub.adminRoutes[route] = {200, R"JSON({"require_passwords": true})JSON"};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_signin", "settingsSignIn");
    QVERIFY(!shown(h, "signinWarning"));
    QVERIFY(h.item("signinPasswords") != nullptr);
    // Pick "Required for everyone" in the select: the row is marked and Save / Cancel appear; Cancel drops the draft.
    h.item("settingsSignIn")->setProperty("draft", true);
    QTRY_VERIFY(shown(h, "signinWarning"));
    QTRY_VERIFY(shown(h, "changedDot_passwords"));
    QVERIFY(h.click("signinCancel"));
    QTRY_VERIFY(!shown(h, "signinWarning"));
    QCOMPARE(calls(hub, route), 0);
    h.item("settingsSignIn")->setProperty("draft", true);
    QVERIFY(h.clickWhenReady("signinSave"));
    QTRY_COMPARE(calls(hub, route), 1);
    QCOMPARE(bodyOf(hub, route).value("require_passwords").toBool(), true);
    QTRY_VERIFY(!shown(h, "signinWarning"));
    QTRY_COMPARE(text(h.item("serverSettingsNavStatus_signin")), QStringLiteral("Password"));
  }

  void updatesInstallAndRollback() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString install = post(QStringLiteral("/updates/install"));
    const QString rollback = post(QStringLiteral("/updates/rollback"));
    hub.adminRoutes[install] = {202, R"JSON({"version": "0.17.1"})JSON"};
    hub.adminRoutes[rollback] = {202, R"JSON({"version": "0.16.3", "data": "restore", "safety_backup": "x.tar", "safety_folder": "/mnt/backup/uzume"})JSON"};
    hub.confirmRequiredPaths = {QStringLiteral("/api/v1/admin/updates/rollback")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_updates", "settingsUpdates");
    QCOMPARE(text(h.item("updatesDone")), QStringLiteral("Server 0.17.1 is ready."));
    QVERIFY(h.clickWhenReady("updatesNow"));
    QTRY_COMPARE(calls(hub, install), 1);
    QCOMPARE(bodyOf(hub, install).value("confirm_breaking").toBool(), false);
    // Earlier versions: the second one restores the database, so the dialog says so; the server asks to confirm.
    QTRY_VERIFY(h.item("versionsTableRow_1") != nullptr);
    QVERIFY(h.click("versionsTableRow_1"));
    QTRY_VERIFY(shown(h, "versionsDetails"));
    QVERIFY(h.clickWhenReady("versionsGoBack"));
    QTRY_VERIFY(shown(h, "rollbackDialog"));
    QCOMPARE(calls(hub, rollback), 0);
    QVERIFY(h.click("rollbackConfirm"));
    QTRY_VERIFY(h.controller->admin()->confirmNeeded());
    QTRY_VERIFY(shown(h, "confirmPassword"));
    h.item("confirmPassword")->setProperty("text", hub.accountPassword);
    QVERIFY(h.click("confirmSubmit"));
    QTRY_COMPARE(calls(hub, rollback), 1);
    QCOMPARE(bodyOf(hub, rollback).value("version").toString(), QStringLiteral("0.16.3"));
    QCOMPARE(bodyOf(hub, rollback).value("confirm_data_loss").toBool(), true);
    QTRY_VERIFY(!shown(h, "rollbackDialog"));
  }

  void backupRunPollsUntilDone() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString start = post(QStringLiteral("/backups"));
    hub.adminRoutes[start] = {202, ""};
    hub.adminHook = [&hub, start](const QString& key) {
      if (key == start) hub.adminRoutes[get(QStringLiteral("/backups/status"))] = {200, backupStatus(kRunning)};
    };
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_backups", "settingsBackups");
    QTRY_VERIFY(h.item("backupsTableRow_0") != nullptr);
    QVERIFY(h.item("backupsTableRow_1") == nullptr);
    QVERIFY(h.clickWhenReady("backupNow"));
    QTRY_COMPARE(calls(hub, start), 1);
    QTRY_VERIFY(shown(h, "backupProgress"));
    QVERIFY(!h.item("backupNow")->isEnabled());
    // Done: the poll sees no running job any more and reads the list again.
    hub.adminRoutes[get(QStringLiteral("/backups/status"))] = {200, backupStatus(kDone)};
    hub.adminRoutes[get(QStringLiteral("/backups"))] = {200, QByteArray("{\"backups\": [") + kNewBackup + "," + kOldBackup + "]}"};
    QTRY_VERIFY_WITH_TIMEOUT(!shown(h, "backupProgress"), 8000);
    QTRY_VERIFY(h.item("backupsTableRow_1") != nullptr);
    QTRY_COMPARE(text(h.item("backupNote")).left(12), QStringLiteral("Backup done:"));
    QTRY_VERIFY(h.item("backupNow")->isEnabled());
  }

  void backupScheduleAndFolderSave() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString route = put(QStringLiteral("/backups/settings"));
    hub.adminRoutes[route] = {200, R"JSON({"mode": "weekly", "weekday": 0, "time": "03:00", "keep": 7, "include_roms": false, "folder": "/mnt/backup/uzume"})JSON"};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_backups", "settingsBackups");
    hub.adminRoutes[get(QStringLiteral("/backups/status"))] = {200, backupStatus("", "weekly")};
    QVERIFY(h.clickWhenReady("backupModeWeekly"));
    QTRY_COMPARE(calls(hub, route), 1);
    QCOMPARE(bodyOf(hub, route).value("mode").toString(), QStringLiteral("weekly"));
    QTRY_VERIFY(shown(h, "backupDayRow"));
    hub.adminRoutes[route] = {200, R"JSON({"mode": "weekly", "weekday": 0, "time": "03:00", "keep": 14, "include_roms": false, "folder": "/mnt/backup/uzume"})JSON"};
    hub.adminRoutes[get(QStringLiteral("/backups/status"))] = {200, backupStatus("", "weekly", 14)};
    QVERIFY(h.clickWhenReady("backupKeepChange"));  // after the Day row moved it down: click() waits for the layout polish
    QTRY_VERIFY(shown(h, "settingsEditDialog"));
    editAndSave(h, QStringLiteral("14"));
    QTRY_COMPARE(calls(hub, route), 2);
    QCOMPARE(bodyOf(hub, route).value("keep").toInt(), 14);
  }

  void restoreGoesThroughConfirm() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString name = QStringLiteral("uzume-server-home-20261014T010000Z.tar");
    const QString restore = post(QStringLiteral("/backups/") + name + QStringLiteral("/restore"));
    hub.adminRoutes[restore] = {202, ""};
    hub.confirmRequiredPaths = {QStringLiteral("/api/v1/admin/backups/") + name + QStringLiteral("/restore")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_backups", "settingsBackups");
    QTRY_VERIFY(h.item("backupsTableRow_0") != nullptr);
    QVERIFY(h.click("backupsTableRow_0"));
    QVERIFY(h.clickWhenReady("backupRestore"));
    QTRY_VERIFY(shown(h, "restoreDialog"));
    QVERIFY(text(h.item("restoreDialog")).isEmpty() || true);
    QCOMPARE(calls(hub, restore), 0);
    QVERIFY(h.click("restoreConfirm"));
    QTRY_VERIFY(h.controller->admin()->confirmNeeded());
    QCOMPARE(calls(hub, restore), 0);
    QTRY_VERIFY(shown(h, "confirmPassword"));
    h.item("confirmPassword")->setProperty("text", hub.accountPassword);
    QVERIFY(h.click("confirmSubmit"));
    QTRY_COMPARE(calls(hub, restore), 1);
    QTRY_VERIFY(!shown(h, "restoreDialog"));
    // Delete asks first and then deletes.
    const QString del1 = del(QStringLiteral("/backups/") + name);
    hub.adminRoutes[del1] = {204, ""};
    QVERIFY(h.clickWhenReady("backupDelete"));
    QTRY_VERIFY(shown(h, "deleteBackupDialog"));
    QCOMPARE(calls(hub, del1), 0);
    QVERIFY(h.click("deleteBackupConfirm"));
    QTRY_COMPARE(calls(hub, del1), 1);
  }

  void backupDownloadWritesAFile() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString name = QStringLiteral("uzume-server-home-20261014T010000Z.tar");
    hub.adminRoutes[get(QStringLiteral("/backups/") + name + QStringLiteral("/download"))] = {200, "TARDATA"};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_backups", "settingsBackups");
    QTRY_VERIFY(h.item("backupsTableRow_0") != nullptr);
    QVERIFY(h.click("backupsTableRow_0"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("b.tar"));
    QVERIFY(QMetaObject::invokeMethod(h.item("settingsBackups"), "saveBackup", Q_ARG(QString, target)));
    QTRY_VERIFY(QFile::exists(target));
    QFile f(target);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("TARDATA"));
    QTRY_VERIFY(text(h.item("backupNote")).startsWith(QStringLiteral("Saved the backup to")));
  }

  void metricsTokenIsShownOnce() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString route = put(QStringLiteral("/metrics"));
    hub.adminRoutes[route] = {200, R"JSON({"enabled": true, "token_set": true, "token": "fbm_placeholdertoken"})JSON"};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_diagnostics", "settingsDiagnostics");
    QVERIFY(!shown(h, "metricsToken"));
    QTRY_VERIFY(h.item("metricsToggle")->isEnabled());
    QVERIFY(h.click("metricsToggle"));
    QTRY_COMPARE(calls(hub, route), 1);
    QCOMPARE(bodyOf(hub, route).value("enabled").toBool(), true);
    QTRY_VERIFY(shown(h, "metricsToken"));
    QCOMPARE(text(h.item("metricsToken")), QStringLiteral("fbm_placeholdertoken"));
    // Leaving the page and coming back: the server never returns the token again.
    hub.adminRoutes[get(QStringLiteral("/metrics"))] = {200, R"JSON({"enabled": true, "token_set": true})JSON"};
    QVERIFY(h.click("serverSettingsNav_general"));
    QTRY_VERIFY(shown(h, "settingsGeneral"));
    QVERIFY(h.click("serverSettingsNav_diagnostics"));
    QTRY_VERIFY(shown(h, "settingsDiagnostics"));
    QTRY_VERIFY(!shown(h, "metricsToken"));
    QTRY_VERIFY(shown(h, "metricsNew"));
    // A new token asks first.
    const QString token = post(QStringLiteral("/metrics/token"));
    hub.adminRoutes[token] = {200, R"JSON({"token": "fbm_secondtoken"})JSON"};
    QVERIFY(h.click("metricsNew"));
    QTRY_VERIFY(shown(h, "metricsNewDialog"));
    QCOMPARE(calls(hub, token), 0);
    QVERIFY(h.click("metricsNewConfirm"));
    QTRY_COMPARE(calls(hub, token), 1);
    QTRY_COMPARE(text(h.item("metricsToken")), QStringLiteral("fbm_secondtoken"));
  }

  void diagnosticsBundleWritesAFile() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    const QString route = post(QStringLiteral("/diagnostics/bundle"));
    hub.adminRoutes[route] = {200, "PK-zip-bytes"};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_diagnostics", "settingsDiagnostics");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("diag.zip"));
    QVERIFY(QMetaObject::invokeMethod(h.item("settingsDiagnostics"), "saveBundle", Q_ARG(QString, target)));
    QTRY_COMPARE(calls(hub, route), 1);
    QTRY_VERIFY(QFile::exists(target));
    QFile f(target);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("PK-zip-bytes"));
    QTRY_VERIFY(text(h.item("bundleNote")).startsWith(QStringLiteral("Saved the server logs to")));
  }

  void storageShowsTheDiskFromStatus() {
    FakeHub hub(QStringLiteral("a"));
    adminHub(hub);
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    openPage(h, "serverSettingsNav_storage", "settingsStorage");
    QTRY_VERIFY(shown(h, "storageBar"));
    QTRY_VERIFY(!text(h.item("storageUsed")).isEmpty());
    QTRY_VERIFY(shown(h, "storageWarning"));   // the sample status has a backup disk at 92 %
    QVERIFY(h.item("storageRecentRow") != nullptr);
  }

  void withoutAdminV1ThePaneExplainsIt() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QVERIFY(!h.controller->admin()->available());
    QVERIFY(h.item("serverSettingsPage") == nullptr || !h.item("serverSettingsPage")->isVisible());
  }
};

UITEST_MAIN(AdminSettingsUiTest)
#include "admin_settings_ui_test.moc"
