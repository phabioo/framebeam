// UI tests phase 5 (offscreen, FakeHub): invite redemption, user_disabled, upload, firmware state, core warnings,
// Settings/appearance. Dummy bytes only (no real ROM/BIOS/firmware); no real core needed.
#include <QSignalSpy>
#include <QtTest>

#include "fakehub.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;
using S = HubConnection::State;

namespace {
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
}  // namespace

class Phase5UiTest : public QObject {
  Q_OBJECT

  static QVariantMap game(Harness& h) { return h.controller->selectedGame(); }

  // Pairs directly (approve) and waits for the library.
  static void pair(Harness& h, FakeHub& hub) {
    hub.decision = FakeHub::Decision::Approve;
    h.controller->addHub(hub.address());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsTrustConfirmation, 8000);
    h.controller->confirmTrust();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->connection()->state(), S::NeedsPairing, 8000);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->libraryState(), QStringLiteral("ready"), 8000);
  }

  static void setGames(FakeHub& hub, const QByteArray& rom) {
    hub.roms.insert(uitest::sha256Hex(rom), rom);
    hub.games = QJsonObject{{QStringLiteral("games"), QJsonArray{uitest::gameJson(QStringLiteral("g1"), QStringLiteral("Lumen Drift"),
                                                                                    uitest::sha256Hex(rom), rom.size())}}};
  }

 private slots:
  void initTestCase() {
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    uitest::installWarningCounter();
  }
  void init() { uitest::warningCount() = 0; }
  void cleanup() { QCOMPARE(uitest::warningCount().load(), 0); }

  // 3b: "I have an invite code" -> code + display name -> paired immediately (200), errors are clear.
  void inviteRedemption() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("users_v1")};
    hub.takenNames = {QStringLiteral("Lena")};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    h.controller->addHub(hub.address());
    QTRY_COMPARE(h.controller->connection()->state(), S::NeedsTrustConfirmation);
    QVERIFY(h.click("trustButton"));
    QTRY_COMPARE(h.controller->connection()->state(), S::NeedsPairing);

    QVERIFY(h.click("inviteSegment"));
    QVERIFY(h.item("inviteCodeField") != nullptr);
    QVERIFY(h.item("inviteCodeField")->isVisible());
    QVERIFY(h.item("redeemButton")->isVisible());
    QVERIFY(!h.item("requestButton")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("5-invite-form"));

    // Wrong code
    h.item("inviteCodeField")->setProperty("text", QStringLiteral("FB-WRONG-CODE"));
    h.item("inviteNameField")->setProperty("text", QStringLiteral("Anna"));
    QVERIFY(h.click("redeemButton"));
    QTRY_VERIFY(pairingError(h).contains(QStringLiteral("not valid")));
    QCOMPARE(h.controller->screen(), QStringLiteral("pairing"));
    QVERIFY(!h.controller->pairing().value(QStringLiteral("inviteBusy")).toBool());
    uitest::saveShot(h.window, QStringLiteral("5-invite-invalid"));

    // Taken display name
    h.item("inviteCodeField")->setProperty("text", QStringLiteral("fb-test-code"));
    h.item("inviteNameField")->setProperty("text", QStringLiteral("lena"));
    QVERIFY(h.click("redeemButton"));
    QTRY_VERIFY(pairingError(h).contains(QStringLiteral("already taken")));

    // Rate limit
    hub.rateLimitInvites = true;
    QVERIFY(h.click("redeemButton"));
    QTRY_VERIFY(pairingError(h).contains(QStringLiteral("Too many attempts")));
    hub.rateLimitInvites = false;

    // Missing input is explained locally.
    h.item("inviteNameField")->setProperty("text", QString());
    QVERIFY(h.click("redeemButton"));
    QTRY_VERIFY(pairingError(h).contains(QStringLiteral("display name")));

    // Success: paired immediately, Library shown.
    h.item("inviteNameField")->setProperty("text", QStringLiteral("Anna"));
    QVERIFY(h.click("redeemButton"));
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
    QCOMPARE(h.controller->profileStore()->profile(hub.hubId)->hubUserId, QStringLiteral("u_invited_1"));
    QCOMPARE(hub.lastInviteBody.value(QStringLiteral("display_name")).toString(), QStringLiteral("Anna"));
  }

  // 202: the invite leads into the existing waiting/poll flow.
  void invitePendingWaitsForApproval() {
    FakeHub hub(QStringLiteral("a"));
    hub.inviteDirect = false;
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    h.controller->addHub(hub.address());
    QTRY_COMPARE(h.controller->connection()->state(), S::NeedsTrustConfirmation);
    h.controller->confirmTrust();
    QTRY_COMPARE(h.controller->connection()->state(), S::NeedsPairing);
    h.controller->redeemInvite(QStringLiteral("FB-TEST-CODE"), QStringLiteral("Anna"));
    QTRY_COMPARE(h.controller->connection()->state(), S::AwaitingApproval);
    QCOMPARE(h.controller->pairing().value(QStringLiteral("phase")).toString(), QStringLiteral("awaiting"));
    hub.decision = FakeHub::Decision::Approve;
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
  }

  // user_disabled: clear state on the connection screen, no retry loop.
  void userDisabledState() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    h.controller->switchHub();
    hub.userDisabled = true;
    h.controller->connectProfile(hub.hubId);
    QTRY_COMPARE(h.controller->connection()->state(), S::UserDisabled);
    QCOMPARE(h.controller->screen(), QStringLiteral("connection"));
    QVariantMap card;
    for (const QVariant& v : h.controller->hubs()) {
      if (v.toMap().value(QStringLiteral("hubId")).toString() == hub.hubId) card = v.toMap();
    }
    QCOMPARE(card.value(QStringLiteral("status")).toString(), QStringLiteral("userDisabled"));
    QVERIFY(card.value(QStringLiteral("message")).toString().contains(QStringLiteral("This user is disabled on the Hub")));
    QCOMPARE(card.value(QStringLiteral("tone")).toString(), QStringLiteral("error"));
    const int tokens = hub.tokenRequests();
    QTest::qWait(500);
    QCOMPARE(hub.tokenRequests(), tokens);
    uitest::saveShot(h.window, QStringLiteral("5-user-disabled"));
    hub.userDisabled = false;
    h.controller->retryConnection();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
  }

  // Upload: button only with uploads_v1; 201 selects the new game, 409 says "Already in the library" and selects it.
  void uploadFlow() {
    const QByteArray rom = "homebrew-dummy-rom-one";
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("uploads_v1")};
    setGames(hub, rom);
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QVERIFY(h.controller->canUpload());
    QVERIFY(h.item("uploadButton") != nullptr && h.item("uploadButton")->isVisible());

    QTemporaryDir files;
    QFile f(files.filePath(QStringLiteral("New Game.nds")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(200000, 'n') + "DUMMY");
    f.close();
    h.controller->uploadRom(QUrl::fromLocalFile(f.fileName()).toString());
    QVERIFY(h.controller->upload().value(QStringLiteral("active")).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->upload().value(QStringLiteral("active")).toBool(), 8000);
    QVERIFY(!h.controller->upload().value(QStringLiteral("isError")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->selectedGameId(), QStringLiteral("up-1"), 8000);
    QCOMPARE(h.controller->library()->totalCount(), 2);
    QCOMPARE(hub.uploads.first().filename, QStringLiteral("New Game.nds"));
    uitest::saveShot(h.window, QStringLiteral("5-upload-done"));

    // Same file again: duplicate, the existing game is selected.
    h.controller->selectGame(QStringLiteral("g1"));
    h.controller->uploadRom(f.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->upload().value(QStringLiteral("message")).toString() == QLatin1String("Already in the library."), 8000);
    QCOMPARE(h.controller->selectedGameId(), QStringLiteral("up-1"));
    QVERIFY(!h.controller->upload().value(QStringLiteral("isError")).toBool());

    // Forbidden
    hub.uploadsAllowed = false;
    QFile g(files.filePath(QStringLiteral("Other.nds")));
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write("other-dummy");
    g.close();
    h.controller->uploadRom(g.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->upload().value(QStringLiteral("isError")).toBool(), 8000);
    QVERIFY(h.controller->upload().value(QStringLiteral("message")).toString().contains(QStringLiteral("not allowed")));
  }

  void noUploadWithoutFeature() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1")};
    setGames(hub, "homebrew-dummy-rom-one");
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QVERIFY(!h.controller->canUpload());
    QVERIFY(h.item("uploadButton") == nullptr || !h.item("uploadButton")->isVisible());
    h.controller->uploadRom(QStringLiteral("/nonexistent.nds"));  // ignored
    QVERIFY(!h.controller->upload().value(QStringLiteral("active")).toBool());
  }

  // Firmware mode native with a required file missing on the Hub: "Firmware required/missing", launch blocked.
  void firmwareMissingBlocksLaunch() {
    const QByteArray rom = "homebrew-dummy-rom-one";
    const QByteArray b7(16, '7');
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1"), QStringLiteral("firmware_v1")};
    setGames(hub, rom);
    hub.systems = ndsSystem(QStringLiteral("native"), QJsonArray{fwFile(QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), true, b7),
                                                                 fwFile(QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), true, {}),
                                                                 fwFile(QStringLiteral("firmware"), QStringLiteral("DS Firmware"), true, {})});
    hub.firmwareFiles = {{QStringLiteral("nds/bios7"), b7}};
    QVERIFY(hub.start());
    qputenv("FRAMEBEAM_MELONDS_DS_CORE", QCoreApplication::applicationFilePath().toLocal8Bit());  // "found" core, never loaded
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->hubSystems()->state() == HubSystems::State::Ready, 8000);
    QTRY_COMPARE(game(h).value(QStringLiteral("firmwareText")).toString(), QStringLiteral("Firmware required/missing"));
    const QString hint = game(h).value(QStringLiteral("firmwareHint")).toString();
    QVERIFY2(hint.contains(QStringLiteral("ARM9 BIOS")) && hint.contains(QStringLiteral("DS Firmware")) && !hint.contains(QStringLiteral("ARM7")), qPrintable(hint));
    QCOMPARE(game(h).value(QStringLiteral("firmwareTone")).toString(), QStringLiteral("error"));
    QVERIFY(!game(h).value(QStringLiteral("canPlay")).toBool());
    QVERIFY(h.item("firmwareRecheck") != nullptr && h.item("firmwareRecheck")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("5-firmware-missing"));

    h.controller->playSelected();
    QTest::qWait(300);
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
    QCOMPARE(hub.firmwareDownloads, 0);
    QCOMPARE(hub.count(QStringLiteral("/api/v1/roms/")), 0);  // blocked before anything is downloaded

    // The admin provides the files; "Check again" releases the launch.
    const QByteArray b9(8, '9'), fw(32, 'f');
    hub.systems = ndsSystem(QStringLiteral("native"), QJsonArray{fwFile(QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), true, b7),
                                                                 fwFile(QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), true, b9),
                                                                 fwFile(QStringLiteral("firmware"), QStringLiteral("DS Firmware"), true, fw)});
    h.controller->recheckFirmware();
    QTRY_VERIFY_WITH_TIMEOUT(game(h).value(QStringLiteral("canPlay")).toBool(), 8000);
    QVERIFY(game(h).value(QStringLiteral("firmwareText")).toString().startsWith(QStringLiteral("From Hub")));
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
  }

  // Native: files are downloaded, validated and materialized under the names the core reads; builtin: nothing.
  void firmwareNativeMaterializesBuiltinDoesNot() {
    const QByteArray rom = "homebrew-dummy-rom-one";
    const QByteArray b7(16, '7'), b9(8, '9'), fw(32, 'f');
    for (const bool native : {false, true}) {
      FakeHub hub(QStringLiteral("a"));
      hub.features = {QStringLiteral("saves_v1"), QStringLiteral("firmware_v1")};
      setGames(hub, rom);
      hub.systems = ndsSystem(native ? QStringLiteral("native") : QStringLiteral("builtin"),
                              QJsonArray{fwFile(QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), native, b7),
                                         fwFile(QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), native, b9),
                                         fwFile(QStringLiteral("firmware"), QStringLiteral("DS Firmware"), native, fw)});
      hub.firmwareFiles = {{QStringLiteral("nds/bios7"), b7}, {QStringLiteral("nds/bios9"), b9}, {QStringLiteral("nds/firmware"), fw}};
      QVERIFY(hub.start());
      qputenv("FRAMEBEAM_MELONDS_DS_CORE", QCoreApplication::applicationFilePath().toLocal8Bit());  // not a core: the start fails later
      Harness h;
      QVERIFY(h.start());
      pair(h, hub);
      QTRY_VERIFY_WITH_TIMEOUT(h.controller->hubSystems()->state() == HubSystems::State::Ready, 8000);
      QTRY_VERIFY_WITH_TIMEOUT(game(h).value(QStringLiteral("canPlay")).toBool(), 8000);
      h.controller->playSelected();
      const QString sys = QDir(h.controller->profileStore()->baseDir()).filePath(QStringLiteral("system"));
      if (native) {
        QTRY_COMPARE_WITH_TIMEOUT(hub.firmwareDownloads, 3, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(sys + QStringLiteral("/firmware.bin")), 8000);
        QFile f(sys + QStringLiteral("/bios7.bin"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), b7);
        QVERIFY(QFile::exists(sys + QStringLiteral("/bios9.bin")));
        QVERIFY(QFile::exists(sys + QStringLiteral("/firmware/nds/") + uitest::sha256Hex(fw)));  // cache, separate from ROMs
      } else {
        QTest::qWait(500);
        QCOMPARE(hub.firmwareDownloads, 0);
        QVERIFY(!QFile::exists(sys + QStringLiteral("/bios7.bin")));
      }
      QTRY_VERIFY_WITH_TIMEOUT(hub.count(QStringLiteral("/api/v1/roms/")) >= 1, 8000);  // ROM only after firmware
      QTest::qWait(300);
      qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    }
  }

  // Old Hub without firmware_v1 behaves as builtin: no /systems request, no firmware row problem.
  void noFirmwareFeatureIsBuiltin() {
    FakeHub hub(QStringLiteral("a"));
    hub.features = {QStringLiteral("saves_v1")};
    setGames(hub, "homebrew-dummy-rom-one");
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QTest::qWait(150);
    QCOMPARE(hub.count(QStringLiteral("/api/v1/systems")), 0);
    QCOMPARE(game(h).value(QStringLiteral("firmwareText")).toString(), QStringLiteral("Not required"));
    QVERIFY(!game(h).value(QStringLiteral("firmwareBlocked")).toBool());
  }

  // Handshake problems about the core: non-blocking warning in the library.
  void coreWarningsAreShown() {
    FakeHub hub(QStringLiteral("a"));
    hub.handshakeExtra = QJsonObject{{QStringLiteral("problems"),
                                      QJsonArray{QJsonObject{{QStringLiteral("code"), QStringLiteral("core_version_mismatch")},
                                                             {QStringLiteral("detail"), QStringLiteral("Core melonds_ds 1.3.0, expected 1.4.0")},
                                                             {QStringLiteral("core_id"), QStringLiteral("melonds_ds")}}}}};
    QVERIFY(hub.start());
    Harness h;
    QVERIFY(h.start());
    pair(h, hub);
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));  // not blocked
    const QVariantList warnings = h.controller->coreWarnings();
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.first().toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("melonDS DS")));
    QTRY_VERIFY(h.item("coreWarning") != nullptr && h.item("coreWarning")->isVisible());
    uitest::saveShot(h.window, QStringLiteral("5-core-warning"));
  }

  // The handshake reports the core id of the registry ("melonds_ds"), never "melonds".
  void handshakeCoreId() {
    qputenv("FRAMEBEAM_MELONDS_DS_CORE", QCoreApplication::applicationFilePath().toLocal8Bit());
    Harness h;
    QVERIFY(h.start(/*probeCores=*/true));
    qunsetenv("FRAMEBEAM_MELONDS_DS_CORE");
    QCOMPARE(h.controller->handshakeCores().size(), 1);
    QCOMPARE(h.controller->handshakeCores().first().id, QStringLiteral("melonds_ds"));
  }

  // Settings page: Appearance Dark (default) | Light | System, persisted; the game view stays dark.
  void settingsAppearance() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    {
      Harness h;
      QVERIFY(h.start());
      const QString dataDir = h.dir.path();
      QCOMPARE(h.controller->appearance(), QStringLiteral("dark"));
      QVERIFY(h.controller->darkMode());
      pair(h, hub);
      QTest::qWait(150);  // the library was just shown: let the layout settle
      QVERIFY(h.click("navSettings"));
      QTest::qWait(100);
      QCOMPARE(h.controller->screen(), QStringLiteral("settings"));
      QVERIFY(h.item("appearanceSegment") != nullptr);
      QVERIFY(h.click("appearanceLight"));
      QCOMPARE(h.controller->appearance(), QStringLiteral("light"));
      QVERIFY(!h.controller->darkMode());
      QTest::qWait(100);
      uitest::saveShot(h.window, QStringLiteral("5-settings-light"));
      QVERIFY(h.click("appearanceSystem"));
      QCOMPARE(h.controller->appearance(), QStringLiteral("system"));
      QVERIFY(h.click("appearanceLight"));
      QVERIFY(h.click("navLibrary"));
      QCOMPARE(h.controller->screen(), QStringLiteral("library"));
      QTest::qWait(100);
      uitest::saveShot(h.window, QStringLiteral("5-library-light"));
      h.controller->setAppearance(QStringLiteral("nonsense"));  // ignored
      QCOMPARE(h.controller->appearance(), QStringLiteral("light"));
      QVERIFY(QFile::exists(h.controller->playerSettings()->filePath()));
      PlayerSettings reread(dataDir);  // a new start reads the persisted value
      QCOMPARE(reread.appearance(), PlayerSettings::Appearance::Light);
    }
  }

 private:
  static QString pairingError(Harness& h) { return h.controller->pairing().value(QStringLiteral("error")).toString(); }
};

UITEST_MAIN(Phase5UiTest)
#include "phase5_ui_test.moc"
