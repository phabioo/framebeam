// Hub client features: invite redemption, user_disabled, ROM upload, systems registry and firmware provisioning.
// Dummy bytes only (no real ROM/BIOS/firmware).
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "firmwarecache.h"
#include "firmwareprovisioner.h"
#include "gameuploader.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "hubsystems.h"
#include "profilestore.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }

QJsonObject fwFile(const QString& id, const QString& name, bool required, const QByteArray& content) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("display_name"), name},
          {QStringLiteral("required"), required},
          {QStringLiteral("present"), !content.isEmpty()},
          {QStringLiteral("size"), content.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(content.size())},
          {QStringLiteral("sha256"), content.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(shaOf(content))}};
}
}  // namespace

class HubFeaturesTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;

  void connectToPairing() {
    conn_->connectToAddress(hub_->address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
  }
  void pairAndConnect() {
    connectToPairing();
    hub_->decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }
  QString writeTemp(const QString& name, const QByteArray& data) {
    const QString p = dir_->filePath(name);
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    return p;
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->filePath(QStringLiteral("data")));
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->features = {QStringLiteral("saves_v1"), QStringLiteral("users_v1"), QStringLiteral("uploads_v1"), QStringLiteral("firmware_v1")};
    QVERIFY(hub_->start());
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
  }
  void cleanup() {
    conn_.reset();
    hub_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  // ------------------------------------------------------------ invites

  void inviteDirectPairsImmediately() {
    connectToPairing();
    conn_->redeemInvite(QStringLiteral(" fb-test-code "), QStringLiteral("  Anna "));
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
    QCOMPARE(hub_->lastInviteBody.value(QStringLiteral("code")).toString(), QStringLiteral("FB-TEST-CODE"));
    QCOMPARE(hub_->lastInviteBody.value(QStringLiteral("display_name")).toString(), QStringLiteral("Anna"));
    QCOMPARE(hub_->lastInviteBody.value(QStringLiteral("device_id")).toString(), profiles_->deviceId());
    QVERIFY(!hub_->lastInviteBody.value(QStringLiteral("player_version")).toString().isEmpty());
    QCOMPARE(hub_->lastInviteBody.value(QStringLiteral("protocol_version")).toInt(), 1);
    // Stored like a normal pairing: credential in the store, profile references it, user id from the Hub.
    const auto p = profiles_->profile(hub_->hubId);
    QVERIFY(p.has_value());
    QCOMPARE(p->hubUserId, QStringLiteral("u_invited_1"));
    QVERIFY(!p->credentialRef.isEmpty());
    QVERIFY(creds_->read(p->credentialRef).has_value());
    QVERIFY(conn_->hubHasFeature(QStringLiteral("users_v1")));
  }

  void invitePendingUsesPollingFlow() {
    hub_->inviteDirect = false;
    connectToPairing();
    conn_->redeemInvite(QStringLiteral("FB-TEST-CODE"), QStringLiteral("Anna"));
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::AwaitingApproval, 8000);
    hub_->decision = FakeHub::Decision::Approve;
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
    QVERIFY(profiles_->profile(hub_->hubId)->hubUserId == QStringLiteral("u_test_1"));
  }

  void inviteErrorsKeepState() {
    hub_->takenNames = {QStringLiteral("Lena")};
    connectToPairing();
    QSignalSpy errors(conn_.get(), &HubConnection::errorOccurred);

    conn_->redeemInvite(QStringLiteral("FB-WRONG"), QStringLiteral("Anna"));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 5000);
    QCOMPARE(errors.last().at(0).toString(), QStringLiteral("invite_invalid"));
    QCOMPARE(conn_->state(), State::NeedsPairing);

    conn_->redeemInvite(QStringLiteral("FB-TEST-CODE"), QStringLiteral("lena"));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 2, 5000);
    QCOMPARE(errors.last().at(0).toString(), QStringLiteral("display_name_taken"));
    QCOMPARE(conn_->state(), State::NeedsPairing);

    hub_->rateLimitInvites = true;
    conn_->redeemInvite(QStringLiteral("FB-TEST-CODE"), QStringLiteral("Anna"));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 3, 5000);
    QCOMPARE(errors.last().at(0).toString(), QStringLiteral("rate_limited"));

    // Empty/too long names never reach the Hub.
    const int before = hub_->count(QStringLiteral("/api/v1/invites/"));
    conn_->redeemInvite(QStringLiteral("FB-TEST-CODE"), QStringLiteral("   "));
    conn_->redeemInvite(QStringLiteral("FB-TEST-CODE"), QString(33, QLatin1Char('x')));
    QCOMPARE(errors.count(), 5);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/invites/")), before);
    QVERIFY(!profiles_->profile(hub_->hubId)->credentialRef.size());
  }

  // ------------------------------------------------------------ user_disabled

  void userDisabledStopsWithoutRetryLoop() {
    pairAndConnect();
    const QString ref = profiles_->profile(hub_->hubId)->credentialRef;
    hub_->userDisabled = true;
    QSignalSpy states(conn_.get(), &HubConnection::stateChanged);
    conn_->noteUnauthorized();  // token renewal -> 401 user_disabled
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::UserDisabled, 8000);
    QCOMPARE(conn_->errorCode(), QStringLiteral("user_disabled"));
    const int tokens = hub_->tokenRequests();
    QTest::qWait(600);
    QCOMPARE(hub_->tokenRequests(), tokens);  // no automatic retry
    // The credential stays (an admin can enable the user again).
    QCOMPARE(profiles_->profile(hub_->hubId)->credentialRef, ref);
    QVERIFY(creds_->read(ref).has_value());
    QVERIFY(conn_->authorizedGet(QStringLiteral("/games")) == nullptr);

    hub_->userDisabled = false;
    conn_->retry();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }

  void userDisabledOnConnect() {
    pairAndConnect();
    conn_->disconnectFromHub();
    hub_->userDisabled = true;
    conn_->connectToProfile(hub_->hubId);
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::UserDisabled, 8000);
    QVERIFY(!profiles_->profile(hub_->hubId)->credentialRef.isEmpty());
  }

  // ------------------------------------------------------------ upload

  void uploadStreamsAndReports() {
    pairAndConnect();
    GameUploader up(conn_.get());
    QSignalSpy progress(&up, &GameUploader::progress);
    QSignalSpy finished(&up, &GameUploader::finished);
    const QByteArray rom = QByteArray(600 * 1024, 'r') + "FRAMEBEAM-DUMMY-ROM";
    const QString path = writeTemp(QStringLiteral("Demo ROM.nds"), rom);
    QVERIFY(up.upload(path, QStringLiteral("Mein Spiel")));
    QVERIFY(up.busy());
    QVERIFY(!up.upload(path));  // one at a time
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(progress.count() >= 1);
    const auto res = finished.first().first().value<UploadResult>();
    QCOMPARE(res.kind, UploadResult::Kind::Created);
    QCOMPARE(res.game.title, QStringLiteral("Mein Spiel"));
    QCOMPARE(res.game.romSha256, shaOf(rom));
    QCOMPARE(hub_->uploads.size(), 1);
    QCOMPARE(hub_->uploads.first().filename, QStringLiteral("Demo ROM.nds"));
    QCOMPARE(hub_->uploads.first().title, QStringLiteral("Mein Spiel"));
    QCOMPARE(hub_->uploads.first().size, qint64(rom.size()));
    QCOMPARE(hub_->uploads.first().sha256, shaOf(rom));
    QVERIFY(!up.busy());

    // Same ROM again: duplicate, names the existing game.
    QVERIFY(up.upload(path));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
    const auto dup = finished.last().first().value<UploadResult>();
    QCOMPARE(dup.kind, UploadResult::Kind::Duplicate);
    QCOMPARE(dup.existingGameId, QStringLiteral("up-1"));
    QCOMPARE(hub_->uploads.size(), 1);
  }

  void uploadForbiddenAndUnreadable() {
    pairAndConnect();
    GameUploader up(conn_.get());
    QSignalSpy finished(&up, &GameUploader::finished);
    hub_->uploadsAllowed = false;
    QVERIFY(up.upload(writeTemp(QStringLiteral("a.nds"), QByteArray(2000, 'a'))));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);
    const auto forbidden = finished.last().first().value<UploadResult>();
    QCOMPARE(forbidden.kind, UploadResult::Kind::Forbidden);
    QCOMPARE(forbidden.errorCode, QStringLiteral("uploads_disabled"));

    QVERIFY(up.upload(dir_->filePath(QStringLiteral("missing.nds"))));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 8000);
    const auto bad = finished.last().first().value<UploadResult>();
    QCOMPARE(bad.kind, UploadResult::Kind::Failed);
    QCOMPARE(bad.errorCode, QStringLiteral("file_unreadable"));
  }

  // ------------------------------------------------------------ systems / firmware

  void systemsRegistryRequiresFeature() {
    hub_->features = {QStringLiteral("saves_v1")};  // old Hub
    pairAndConnect();
    HubSystems systems(conn_.get());
    QVERIFY(!systems.supported());
    systems.reload();
    QTest::qWait(100);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/systems")), 0);
    QCOMPARE(systems.state(), HubSystems::State::Idle);
  }

  void provisionsValidatesAndCaches() {
    const QByteArray b7(16, '7'), b9(8, '9'), fw(32, 'f');
    hub_->systems = QJsonObject{{QStringLiteral("systems"),
                                 QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("nds")},
                                                        {QStringLiteral("display_name"), QStringLiteral("Nintendo DS")},
                                                        {QStringLiteral("preferred_core_id"), QStringLiteral("melonds_ds")},
                                                        {QStringLiteral("expected_core_version"), QStringLiteral("1.4.0")},
                                                        {QStringLiteral("firmware_mode"), QStringLiteral("native")},
                                                        {QStringLiteral("firmware"),
                                                         QJsonArray{fwFile(QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), true, b7),
                                                                    fwFile(QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), true, b9),
                                                                    fwFile(QStringLiteral("firmware"), QStringLiteral("DS Firmware"), true, fw)}}}}}};
    hub_->firmwareFiles = {{QStringLiteral("nds/bios7"), b7}, {QStringLiteral("nds/bios9"), b9}, {QStringLiteral("nds/firmware"), fw}};
    pairAndConnect();

    HubSystems systems(conn_.get());
    QVERIFY(systems.supported());
    QSignalSpy sysChanged(&systems, &HubSystems::stateChanged);
    systems.reload();
    QTRY_VERIFY_WITH_TIMEOUT(systems.state() == HubSystems::State::Ready, 8000);
    const auto nds = systems.system(QStringLiteral("nds"));
    QVERIFY(nds.has_value());
    QVERIFY(nds->nativeFirmware());
    QCOMPARE(nds->expectedCoreVersion, QStringLiteral("1.4.0"));
    QCOMPARE(nds->firmware.size(), 3);
    QVERIFY(nds->file(QStringLiteral("bios7"))->present);

    FirmwareCache cache(dir_->filePath(QStringLiteral("data/system/firmware")));
    FirmwareProvisioner prov(conn_.get(), &cache);
    QSignalSpy done(&prov, &FirmwareProvisioner::finished);
    const QStringList wanted{QStringLiteral("bios7"), QStringLiteral("bios9"), QStringLiteral("firmware")};
    prov.prepare(*nds, wanted);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 8000);
    auto res = done.last().first().value<FirmwareResult>();
    QVERIFY(res.ok);
    QCOMPARE(res.pathsById.size(), 3);
    QCOMPARE(hub_->firmwareDownloads, 3);
    QVERIFY(res.pathsById.value(QStringLiteral("bios7")).endsWith(QLatin1String("firmware/nds/") + shaOf(b7)));
    QVERIFY(cache.lookup(QStringLiteral("nds"), shaOf(fw), fw.size()));

    // Second start: everything comes from the cache.
    prov.prepare(*nds, wanted);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 8000);
    QVERIFY(done.last().first().value<FirmwareResult>().ok);
    QCOMPARE(hub_->firmwareDownloads, 3);
  }

  void provisionFailsOnHashMismatchAndMissing() {
    const QByteArray b7(16, '7'), b9(8, '9');
    SystemInfo nds;
    nds.id = QStringLiteral("nds");
    nds.firmwareMode = QStringLiteral("native");
    nds.firmware = {{QStringLiteral("bios7"), QStringLiteral("ARM7 BIOS"), true, true, b7.size(), shaOf(b7)},
                    {QStringLiteral("bios9"), QStringLiteral("ARM9 BIOS"), true, false, 0, QString()},
                    {QStringLiteral("firmware"), QStringLiteral("DS Firmware"), false, false, 0, QString()}};
    // The Hub delivers other bytes than it announced.
    hub_->firmwareFiles = {{QStringLiteral("nds/bios7"), QByteArray(16, 'X')}};
    pairAndConnect();
    FirmwareCache cache(dir_->filePath(QStringLiteral("fw")));
    FirmwareProvisioner prov(conn_.get(), &cache);
    QSignalSpy done(&prov, &FirmwareProvisioner::finished);
    const QStringList wanted{QStringLiteral("bios7"), QStringLiteral("bios9"), QStringLiteral("firmware")};

    const auto missing = FirmwareProvisioner::missingOnHub(nds, wanted);
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first().fileId, QStringLiteral("bios9"));

    prov.prepare(nds, wanted);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 8000);
    const auto res = done.last().first().value<FirmwareResult>();
    QVERIFY(!res.ok);
    QVERIFY(res.pathsById.isEmpty());
    QStringList reasons;
    for (const FirmwareProblem& p : res.problems) {
      reasons << p.fileId + QLatin1Char(':') + p.reason;
    }
    reasons.sort();
    QCOMPARE(reasons, (QStringList{QStringLiteral("bios7:invalid_hash"), QStringLiteral("bios9:missing_on_hub")}));
    QVERIFY(!cache.probe(QStringLiteral("nds"), shaOf(b7), b7.size()));  // never cached
  }
};

QTEST_GUILESS_MAIN(HubFeaturesTest)
#include "hub_features_test.moc"
