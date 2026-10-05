#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "hubhttp.h"
#include "profilestore.h"

using namespace framebeam;
using State = HubConnection::State;

#define WAIT_STATE(conn, st) QTRY_VERIFY_WITH_TIMEOUT((conn).state() == (st), 8000)

class HubFlowTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<HubConnection> conn_;

  void pairViaApproval(FakeHub& hub) {
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    conn_->confirmTrust();
    WAIT_STATE(*conn_, State::NeedsPairing);
    hub.decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    WAIT_STATE(*conn_, State::Connected);
  }

  static QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
  }
  void cleanup() {
    conn_.reset();
    profiles_.reset();
    creds_.reset();
    dir_.reset();
  }

  void fingerprintFormatMatchesHub() {
    QFile f(QStringLiteral(FB_TEST_DATA_DIR "/test-cert-a.pem"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QSslCertificate cert(&f, QSsl::Pem);
    QCOMPARE(HubHttp::fingerprint(cert),
             QStringLiteral("2F:E5:AD:C0:A7:7D:0E:CE:5E:05:AC:C5:CA:A2:C7:6C:E2:8E:63:A9:CB:22:A5:D8:3D:F2:09:85:79:E8:B2:D2"));
  }

  void firstContactReportsFingerprintThenPins() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    QSignalSpy states(conn_.get(), &HubConnection::stateChanged);
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    QCOMPARE(conn_->observedFingerprint(), hub.fingerprint());
    QCOMPARE(conn_->hubInfo().hubId, hub.hubId);
    QVERIFY(profiles_->profiles().isEmpty());  // noch nichts uebernommen
    QCOMPARE(hub.requests.size(), 1);          // nur der Info-Endpunkt
    conn_->confirmTrust();
    WAIT_STATE(*conn_, State::NeedsPairing);
    QCOMPARE(profiles_->profile(hub.hubId)->pinnedFingerprint, hub.fingerprint());
  }

  void rejectTrustDisconnects() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    conn_->rejectTrust();
    QVERIFY(conn_->state() == State::Disconnected);
    QVERIFY(profiles_->profiles().isEmpty());
  }

  void pinnedMatchingCertificateConnects() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    pairViaApproval(hub);
    conn_->setHandshakeInfo([] {
      HandshakeInfo h = HandshakeInfo::detect();
      h.cores.append({QStringLiteral("melonds_ds"), QStringLiteral("1.4.0")});
      return h;
    }());
    conn_->disconnectFromHub();
    // zweiter Lauf ueber das gespeicherte Profil: Pin + Credential vorhanden -> direkt verbunden
    conn_->connectToProfile(hub.hubId);
    WAIT_STATE(*conn_, State::Connected);
    QVERIFY(profiles_->profile(hub.hubId)->lastConnected.isValid());
    QCOMPARE(profiles_->lastHubId(), hub.hubId);
    FakeRequest hs;
    for (const FakeRequest& r : hub.requests) {
      if (r.path == QLatin1String("/api/v1/handshake")) {
        hs = r;
      }
    }
    const QJsonObject body = QJsonDocument::fromJson(hs.body).object();
    QCOMPARE(body.value(QStringLiteral("protocol_version")).toInt(), 1);
    QCOMPARE(body.value(QStringLiteral("min_protocol_version")).toInt(), 1);
    QCOMPARE(body.value(QStringLiteral("cores")).toArray().first().toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("melonds_ds"));
    QVERIFY(!body.value(QStringLiteral("player_version")).toString().isEmpty());
    QVERIFY(body.contains(QStringLiteral("video")) && body.contains(QStringLiteral("audio")) && body.contains(QStringLiteral("input")));
    QVERIFY(hs.headers.value(QStringLiteral("authorization")).startsWith("Bearer fba_"));
  }

  void changedCertificateBlocksWithoutAnyRequest() {
    FakeHub hubA(QStringLiteral("a"));
    QVERIFY(hubA.start());
    HubProfile p;
    p.hubId = hubA.hubId;
    p.name = QStringLiteral("x");
    p.address = hubA.address();
    p.deviceId = profiles_->deviceId();
    p.pinnedFingerprint = hubA.fingerprint();
    p.credentialRef = credentialTarget(p.hubId, p.deviceId);
    QVERIFY(profiles_->upsertProfile(p));
    creds_->write(p.credentialRef, QString::fromLatin1(FakeHub::kDeviceCredential));
    hubA.close();

    // Gleiche Adresse, anderes Zertifikat
    FakeHub hubB(QStringLiteral("b"));
    QVERIFY(hubB.start());
    p.address = hubB.address();
    QVERIFY(profiles_->upsertProfile(p));
    conn_->connectToProfile(p.hubId);
    WAIT_STATE(*conn_, State::CertificateChanged);
    QCOMPARE(hubB.requests.size(), 0);
    QCOMPARE(conn_->observedFingerprint(), hubB.fingerprint());
    QCOMPARE(conn_->expectedFingerprint(), hubA.fingerprint());
    QCOMPARE(profiles_->profile(p.hubId)->pinnedFingerprint, hubA.fingerprint());  // nicht uebernommen
    QCOMPARE(hubB.tokenRequests(), 0);
  }

  void protocolIncompatibility() {
    FakeHub hub(QStringLiteral("a"));
    hub.minProtocolVersion = 2;
    hub.protocolVersion = 2;
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::Incompatible);
    QVERIFY(conn_->incompatibleReason() == HubConnection::IncompatibleReason::PlayerTooOld);
    QCOMPARE(conn_->errorCode(), QStringLiteral("player_too_old"));

    HandshakeInfo h = HandshakeInfo::detect();
    h.minProtocolVersion = 3;  // Player verlangt mehr, als der Hub kann
    conn_->setHandshakeInfo(h);
    hub.minProtocolVersion = 1;
    hub.protocolVersion = 2;
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::Incompatible);
    QVERIFY(conn_->incompatibleReason() == HubConnection::IncompatibleReason::HubTooOld);
  }

  void pairingAllowStoresCredentialOutsideProfile() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    conn_->confirmTrust();
    WAIT_STATE(*conn_, State::NeedsPairing);
    conn_->requestPairing();
    WAIT_STATE(*conn_, State::AwaitingApproval);
    QTest::qWait(150);  // mehrere Polls mit pending
    QVERIFY(conn_->state() == State::AwaitingApproval);
    hub.decision = FakeHub::Decision::Approve;
    WAIT_STATE(*conn_, State::Connected);

    const auto p = profiles_->profile(hub.hubId);
    QVERIFY(p.has_value());
    QCOMPARE(p->hubUserId, QStringLiteral("u_test_1"));
    QCOMPARE(p->deviceId, profiles_->deviceId());
    QCOMPARE(p->credentialRef, credentialTarget(hub.hubId, profiles_->deviceId()));
    QCOMPARE(creds_->read(p->credentialRef).value(), QString::fromLatin1(FakeHub::kDeviceCredential));

    // Pairing-Anfrage enthaelt Geraeteangaben
    QJsonObject req;
    for (const FakeRequest& r : hub.requests) {
      if (r.path == QLatin1String("/api/v1/pairing/requests")) {
        req = QJsonDocument::fromJson(r.body).object();
      }
    }
    QCOMPARE(req.value(QStringLiteral("device_id")).toString(), profiles_->deviceId());
    QCOMPARE(req.value(QStringLiteral("device_name")).toString(), profiles_->deviceName());
    QVERIFY(!req.value(QStringLiteral("player_version")).toString().isEmpty());

    // Keine Secrets in Profil-/Device-Datei
    const QByteArray files = readAll(profiles_->profilesFilePath()) + readAll(dir_->filePath(QStringLiteral("device.json")));
    QVERIFY(!files.isEmpty());
    for (const QByteArray& secret : QList<QByteArray>{FakeHub::kDeviceCredential, FakeHub::kPollToken, "fba_", "fbd_", "fbp_"}) {
      QVERIFY2(!files.contains(secret), secret.constData());
    }
  }

  void pairingDenyExpireCancel() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    conn_->confirmTrust();
    WAIT_STATE(*conn_, State::NeedsPairing);

    hub.decision = FakeHub::Decision::Deny;
    conn_->requestPairing();
    WAIT_STATE(*conn_, State::Denied);
    QVERIFY(profiles_->profile(hub.hubId)->credentialRef.isEmpty());

    hub.decision = FakeHub::Decision::Expire;
    conn_->requestPairing();  // aus Denied heraus erneut moeglich
    WAIT_STATE(*conn_, State::Expired);

    hub.decision = FakeHub::Decision::Pending;
    conn_->requestPairing();
    WAIT_STATE(*conn_, State::AwaitingApproval);
    conn_->cancelPairing();
    QVERIFY(conn_->state() == State::NeedsPairing);
    const int polls = hub.count(QStringLiteral("/api/v1/pairing/requests/"));
    QTest::qWait(200);
    QCOMPARE(hub.count(QStringLiteral("/api/v1/pairing/requests/")), polls);  // Poll gestoppt
  }

  void pairingRateLimitKeepsState() {
    FakeHub hub(QStringLiteral("a"));
    hub.rateLimitPairing = true;
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    conn_->confirmTrust();
    WAIT_STATE(*conn_, State::NeedsPairing);
    QSignalSpy errors(conn_.get(), &HubConnection::errorOccurred);
    conn_->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 5000);
    QCOMPARE(errors.first().first().toString(), QStringLiteral("rate_limited"));
    QVERIFY(conn_->state() == State::NeedsPairing);
  }

  void accessTokenIsRenewedBeforeExpiry() {
    FakeHub hub(QStringLiteral("a"));
    hub.tokenLifetime = 1;  // Erneuerung nach ~0,8 s
    QVERIFY(hub.start());
    pairViaApproval(hub);
    QTRY_VERIFY_WITH_TIMEOUT(hub.tokenRequests() >= 3, 6000);
    QVERIFY(conn_->state() == State::Connected);
    QVERIFY(hub.issuedAccessTokens.at(0) != hub.issuedAccessTokens.at(1));
    // Erneuerung ohne weiteren Handshake
    QCOMPARE(hub.count(QStringLiteral("/api/v1/handshake")), 1);
  }

  void revokeBringsBackToPairing() {
    FakeHub hub(QStringLiteral("a"));
    hub.tokenLifetime = 1;
    QVERIFY(hub.start());
    pairViaApproval(hub);
    hub.revoked = true;  // Admin widerruft im Hub
    WAIT_STATE(*conn_, State::NeedsPairing);
    QCOMPARE(conn_->errorCode(), QStringLiteral("device_revoked"));
    QVERIFY(!creds_->read(credentialTarget(hub.hubId, profiles_->deviceId())).has_value());
    QVERIFY(profiles_->profile(hub.hubId)->credentialRef.isEmpty());
  }

  void revokeSelf() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    pairViaApproval(hub);
    conn_->revokeSelf();
    WAIT_STATE(*conn_, State::NeedsPairing);
    QVERIFY(hub.revoked);
    QVERIFY(!creds_->read(credentialTarget(hub.hubId, profiles_->deviceId())).has_value());
  }

  void invalidStoredCredentialGoesToPairing() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    pairViaApproval(hub);
    conn_->disconnectFromHub();
    creds_->write(credentialTarget(hub.hubId, profiles_->deviceId()), QStringLiteral("fbd_WRONG"));
    conn_->connectToProfile(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    QCOMPARE(conn_->errorCode(), QStringLiteral("invalid_credentials"));
  }

  void insecureHttpOnlyOnLoopbackOrDevFlag() {
    conn_->connectToAddress(QStringLiteral("http://192.0.2.1:9"));
    QVERIFY(conn_->state() == State::Unreachable);
    QCOMPARE(conn_->errorCode(), QStringLiteral("insecure_http"));
    QVERIFY(HubHttp::isSchemeAllowed(QUrl(QStringLiteral("http://localhost:1")), false));
    QVERIFY(HubHttp::isSchemeAllowed(QUrl(QStringLiteral("http://[::1]:1")), false));
    QVERIFY(!HubHttp::isSchemeAllowed(QUrl(QStringLiteral("http://192.0.2.1")), false));
    QVERIFY(HubHttp::isSchemeAllowed(QUrl(QStringLiteral("http://192.0.2.1")), true));

    FakeHub hub{QString()};  // HTTP auf 127.0.0.1
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsPairing);  // kein TLS -> keine Trust-Bestaetigung
  }

  void unreachableHub() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    const QString addr = hub.address();
    hub.close();
    conn_->connectToAddress(addr);
    WAIT_STATE(*conn_, State::Unreachable);
    QCOMPARE(conn_->errorCode(), QStringLiteral("unreachable"));
  }

  void switchingHubDisconnectsFirst() {
    FakeHub hub1(QStringLiteral("a"));
    FakeHub hub2(QStringLiteral("b"));
    hub2.hubId = QStringLiteral("hub-test-2");
    QVERIFY(hub1.start() && hub2.start());
    pairViaApproval(hub1);
    QList<State> seen;
    connect(conn_.get(), &HubConnection::stateChanged, this, [&seen](State s) { seen.append(s); });
    conn_->connectToAddress(hub2.address());
    QVERIFY(seen.size() >= 2);
    QVERIFY(seen.at(0) == State::Disconnected);
    QVERIFY(seen.at(1) == State::Identifying);
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    QCOMPARE(conn_->hubInfo().hubId, QStringLiteral("hub-test-2"));
  }
};

QTEST_GUILESS_MAIN(HubFlowTest)
#include "hub_flow_test.moc"
