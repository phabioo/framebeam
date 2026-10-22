// Accounts (0.18 package 7): sign-in with a password, set-up with a code, invites with a password, the security code of an
// invite link, several people on one PC, removing a person, and the account API. Against the in-process fake server.
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "accountapi.h"
#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "localpin.h"
#include "profilestore.h"
#include "savestore.h"
#include "securitycode.h"

using namespace uzume;
using State = HubConnection::State;

#define WAIT_STATE(conn, st) QTRY_VERIFY_WITH_TIMEOUT((conn).state() == (st), 8000)

namespace {
const QString kPassword = QStringLiteral("correct horse battery");
}

class AccountsTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<HubConnection> conn_;

  // First contact: trust the certificate; ends in NeedsPairing.
  void trust(FakeHub& hub) {
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    conn_->confirmTrust();
    WAIT_STATE(*conn_, State::NeedsPairing);
  }
  void signInAs(FakeHub& hub, const QString& name) {
    conn_->signIn(name, kPassword);
    WAIT_STATE(*conn_, State::Connected);
    QVERIFY(!hub.requests.isEmpty());
  }
  static QJsonObject lastBodyOf(const FakeHub& hub, const QString& path) {
    QJsonObject body;
    for (const FakeRequest& r : hub.requests) {
      if (r.path == path) body = QJsonDocument::fromJson(r.body).object();
    }
    return body;
  }
  // Waits for the error of an action that keeps the state.
  struct ErrorSpy {
    QSignalSpy spy;
    explicit ErrorSpy(HubConnection* c) : spy(c, &HubConnection::errorOccurred) {}
    bool waitFor(const QString& code) {
      if (spy.isEmpty() && !spy.wait(8000)) return false;
      return spy.last().at(0).toString() == code;
    }
  };

  AccountApiResult call(const std::function<void(AccountApi::Callback)>& start) {
    AccountApiResult out;
    bool done = false;
    start([&](const AccountApiResult& r) {
      out = r;
      done = true;
    });
    for (int i = 0; i < 160 && !done; ++i) QTest::qWait(50);
    if (!done) out.errorCode = QStringLiteral("test_timeout");
    return out;
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

  // ---- identification ----

  void setupRequiredAndSecurityCode() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    QVERIFY(!conn_->setupRequired());  // a server that sends nothing is set up
    const QString code = conn_->securityCode();
    QCOMPARE(code, securityCodeFromFingerprint(hub.fingerprint()));
    QCOMPARE(code.size(), 14);
    QCOMPARE(code.at(4), QLatin1Char('-'));
    conn_->rejectTrust();
    QVERIFY(profiles_->profiles().isEmpty());  // nothing saved when the person says it does not match

    hub.setupRequired = true;
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);
    QVERIFY(conn_->setupRequired());
  }

  // ---- sign-in ----

  void signInSuccess() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    QSignalSpy errors(conn_.get(), &HubConnection::errorOccurred);
    signInAs(hub, QStringLiteral("anna"));
    QVERIFY(errors.isEmpty());

    const QJsonObject body = lastBodyOf(hub, QStringLiteral("/api/v1/auth/sign-in"));
    QCOMPARE(body.value(QStringLiteral("username")).toString(), QStringLiteral("anna"));
    QCOMPARE(body.value(QStringLiteral("device_id")).toString(), profiles_->installId());
    QCOMPARE(body.value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QVERIFY(!body.value(QStringLiteral("app_version")).toString().isEmpty());  // the field is app_version, not player_version
    QVERIFY(!body.contains(QStringLiteral("player_version")));
    QCOMPARE(body.value(QStringLiteral("protocol_version")).toInt(), 1);
    QVERIFY(!body.value(QStringLiteral("device_name")).toString().isEmpty());

    const auto p = profiles_->profile(hub.hubId);
    QCOMPARE(p->hubUserId, QStringLiteral("u_anna"));
    QCOMPARE(p->userDisplayName, QStringLiteral("anna"));  // from the handshake
    QCOMPARE(p->deviceId, profiles_->installId());
    QCOMPARE(p->accounts.size(), 1);
    QCOMPARE(p->activeUserId, QStringLiteral("u_anna"));
    QCOMPARE(readCredential(*creds_, p->credentialRef).value(), QString::fromLatin1(FakeHub::kDeviceCredential));
    QCOMPARE(hub.callerDeviceId, profiles_->installId());
    // The password is in no file.
    QFile f(profiles_->profilesFilePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(!f.readAll().contains(kPassword.toUtf8()));
  }

  void signInErrorsKeepTheState() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);

    { // wrong password and unknown name look the same
      ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("anna"), QStringLiteral("wrong password!"));
      QVERIFY(e.waitFor(QStringLiteral("invalid_credentials")));
      QVERIFY(conn_->state() == State::NeedsPairing);
      QCOMPARE(conn_->errorCode(), QStringLiteral("invalid_credentials"));
      QVERIFY(profiles_->profile(hub.hubId)->credentialRef.isEmpty());
    }
    { // empty fields are not sent
      const int before = hub.signInBodies.size();
      ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("  "), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("invalid_input")));
      QCOMPARE(hub.signInBodies.size(), before);
    }
    { // too many tries, with the wait the server names
      hub.rateLimitSignIn = true;
      hub.rateLimitRetryAfter = 292;
      ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("anna"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("rate_limited")));
      QCOMPARE(conn_->errorRetryAfter(), 292);
      QVERIFY(conn_->state() == State::NeedsPairing);
      hub.rateLimitSignIn = false;
      // A later error without a wait resets it.
      ErrorSpy e2(conn_.get());
      conn_->signIn(QStringLiteral("anna"), QStringLiteral("nope nope nope"));
      QVERIFY(e2.waitFor(QStringLiteral("invalid_credentials")));
      QCOMPARE(conn_->errorRetryAfter(), 0);
    }
    { // account turned off
      hub.disabledUsers.append(QStringLiteral("ben"));
      ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("ben"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("user_disabled")));
      QVERIFY(conn_->state() == State::NeedsPairing);
    }
    { // a server without the endpoint
      hub.signInUnsupported = true;
      ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("anna"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("hub_too_old")));
      hub.signInUnsupported = false;
    }
    { // no answer
      hub.holdSignIn = true;
      conn_->setRequestTimeoutMs(300);
      ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("anna"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("unreachable")));
      QVERIFY(conn_->state() == State::NeedsPairing);  // the form stays open
      QVERIFY(conn_->errorMessage().contains(QLatin1String("seconds")));
      hub.holdSignIn = false;
    }
    // And after all that the right password still works.
    signInAs(hub, QStringLiteral("anna"));
  }

  void signInConflictTakesAFreshDeviceId() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    hub.conflictDevices.append(profiles_->installId());  // the install ID is taken by another person's device
    signInAs(hub, QStringLiteral("anna"));
    QCOMPARE(hub.signInBodies.size(), 2);
    const QString second = hub.signInBodies.last().value(QStringLiteral("device_id")).toString();
    QVERIFY(second != profiles_->installId());
    QCOMPARE(hub.signInBodies.last().value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QCOMPARE(profiles_->profile(hub.hubId)->deviceId, second);
    QCOMPARE(hub.callerDeviceId, second);
  }

  void signInAfterRevokedCredentialKeepsThePinLockRules() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    LocalPin pin(profiles_.get(), creds_.get());
    QVERIFY(pin.setPin(hub.hubId, QStringLiteral("u_anna"), QStringLiteral("1234")));
    for (int i = 0; i < 3; ++i) pin.verify(hub.hubId, QStringLiteral("u_anna"), QStringLiteral("0000"));
    QVERIFY(pin.isLocked(hub.hubId, QStringLiteral("u_anna")));

    // The server revokes the device: the person stays in the list without a credential.
    conn_->disconnectFromHub();
    hub.revoked = true;
    conn_->connectToProfile(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    const auto a = profiles_->account(hub.hubId, QStringLiteral("u_anna"));
    QVERIFY(a.has_value());
    QVERIFY(a->credentialRef.isEmpty());
    QVERIFY(a->hasPin);
    QVERIFY(pin.isLocked(hub.hubId, QStringLiteral("u_anna")));  // revoked, not unlocked

    // A password sign-in with the same device ID unlocks the PIN, which stays.
    hub.revoked = false;
    signInAs(hub, QStringLiteral("anna"));
    QVERIFY(!pin.isLocked(hub.hubId, QStringLiteral("u_anna")));
    QVERIFY(pin.verify(hub.hubId, QStringLiteral("u_anna"), QStringLiteral("1234")).ok());
    QCOMPARE(profiles_->accounts(hub.hubId).size(), 1);
  }

  // ---- set-up with a code ----

  void setupWithCodeCreatesTheAdmin() {
    FakeHub hub(QStringLiteral("a"));
    hub.setupRequired = true;
    QVERIFY(hub.start());
    trust(hub);
    QVERIFY(conn_->setupRequired());
    conn_->setupWithCode(QStringLiteral(" 7h4k-2q "), QStringLiteral("max"), kPassword);
    WAIT_STATE(*conn_, State::Connected);
    QVERIFY(!conn_->setupRequired());
    const QJsonObject body = lastBodyOf(hub, QStringLiteral("/api/v1/setup"));
    QCOMPARE(body.value(QStringLiteral("setup_code")).toString(), QStringLiteral("7H4K2Q"));  // normalized
    QCOMPARE(body.value(QStringLiteral("username")).toString(), QStringLiteral("max"));
    QCOMPARE(body.value(QStringLiteral("device_id")).toString(), profiles_->installId());
    QCOMPARE(body.value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QVERIFY(!body.value(QStringLiteral("player_version")).toString().isEmpty());
    QCOMPARE(conn_->userRole(), QStringLiteral("admin"));
    QCOMPARE(profiles_->profile(hub.hubId)->accounts.size(), 1);
  }

  void setupWithCodeErrors() {
    FakeHub hub(QStringLiteral("a"));
    hub.setupRequired = true;
    QVERIFY(hub.start());
    trust(hub);
    { ErrorSpy e(conn_.get());
      conn_->setupWithCode(QStringLiteral("AAAAAA"), QStringLiteral("max"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("invalid_setup_code"))); }
    { ErrorSpy e(conn_.get());
      conn_->setupWithCode(QStringLiteral("7H4K2Q"), QStringLiteral("max"), QStringLiteral("short"));
      QVERIFY(e.waitFor(QStringLiteral("bad_request"))); }
    { hub.setupCodeExpired = true;
      ErrorSpy e(conn_.get());
      conn_->setupWithCode(QStringLiteral("7H4K2Q"), QStringLiteral("max"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("setup_code_expired")));
      hub.setupCodeExpired = false; }
    { hub.rateLimitSetup = true;
      hub.rateLimitRetryAfter = 30;
      ErrorSpy e(conn_.get());
      conn_->setupWithCode(QStringLiteral("7H4K2Q"), QStringLiteral("max"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("rate_limited")));
      QCOMPARE(conn_->errorRetryAfter(), 30);
      hub.rateLimitSetup = false; }
    { ErrorSpy e(conn_.get());
      conn_->setupWithCode(QString(), QStringLiteral("max"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("invalid_input"))); }
    QVERIFY(conn_->state() == State::NeedsPairing);  // every error keeps the form
    QVERIFY(profiles_->profile(hub.hubId)->accounts.isEmpty());
    // The server is set up by somebody else meanwhile: the app still believes it is open, the server answers already_set_up.
    hub.setupRequired = false;
    { ErrorSpy e(conn_.get());
      conn_->setupWithCode(QStringLiteral("7H4K2Q"), QStringLiteral("max"), kPassword);
      QVERIFY(e.waitFor(QStringLiteral("already_set_up")));
      QCOMPARE(hub.setupBodies.last().value(QStringLiteral("username")).toString(), QStringLiteral("max")); }
  }

  void setupWithCodeOnAServerThatIsSetUpSendsNothing() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());  // setup_required false
    trust(hub);
    ErrorSpy e(conn_.get());
    conn_->setupWithCode(QStringLiteral("7H4K2Q"), QStringLiteral("max"), kPassword);
    QVERIFY(e.waitFor(QStringLiteral("already_set_up")));
    QCOMPARE(hub.setupBodies.size(), 0);
    QCOMPARE(hub.count(QStringLiteral("/api/v1/setup")), 0);
  }

  // ---- invites ----

  void redeemInviteWithAPassword() {
    FakeHub hub(QStringLiteral("a"));
    hub.inviteCode = QStringLiteral("UZ-8XK2-M4QD");
    QVERIFY(hub.start());
    trust(hub);
    conn_->redeemInvite(QStringLiteral("uz 8xk2 m4qd"), QStringLiteral("Lena"), kPassword);
    WAIT_STATE(*conn_, State::Connected);
    QJsonObject body = hub.lastInviteBody;
    QCOMPARE(body.value(QStringLiteral("code")).toString(), QStringLiteral("UZ-8XK2-M4QD"));
    QCOMPARE(body.value(QStringLiteral("password")).toString(), kPassword);
    QCOMPARE(body.value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QCOMPARE(body.value(QStringLiteral("device_id")).toString(), profiles_->installId());
    QCOMPARE(profiles_->profile(hub.hubId)->hubUserId, QStringLiteral("u_invited_1"));
  }

  void redeemInviteWithoutPasswordSendsNone() {
    FakeHub hub(QStringLiteral("a"));
    hub.inviteCode = QStringLiteral("UZ-8XK2-M4QD");
    QVERIFY(hub.start());
    trust(hub);
    conn_->redeemInvite(QStringLiteral("UZ-8XK2-M4QD"), QStringLiteral("Lena"));
    WAIT_STATE(*conn_, State::Connected);
    QVERIFY(!hub.lastInviteBody.contains(QStringLiteral("password")));
    QCOMPARE(hub.lastInviteBody.value(QStringLiteral("install_id")).toString(), profiles_->installId());
  }

  void pairingRequestCarriesTheInstallId() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    trust(hub);
    hub.decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    WAIT_STATE(*conn_, State::Connected);
    const QJsonObject body = lastBodyOf(hub, QStringLiteral("/api/v1/pairing/requests"));
    QCOMPARE(body.value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QCOMPARE(body.value(QStringLiteral("device_id")).toString(), profiles_->installId());
  }

  // ---- invite link: the security code is in the link ----

  void expectedFingerprintMatchSkipsTheComparison() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    QSignalSpy states(conn_.get(), &HubConnection::stateChanged);
    // Any notation of the fingerprint works.
    conn_->setExpectedFingerprint(QString(hub.fingerprint()).remove(QLatin1Char(':')).toLower());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsPairing);
    for (const auto& s : states) {
      QVERIFY(s.at(0).value<State>() != State::NeedsTrustConfirmation);
    }
    QVERIFY(conn_->trustedByLink());
    QCOMPARE(profiles_->profile(hub.hubId)->pinnedFingerprint, hub.fingerprint());
    QCOMPARE(conn_->securityCode(), securityCodeFromFingerprint(hub.fingerprint()));
  }

  void expectedFingerprintMismatchIsAHardStop() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    QSignalSpy errors(conn_.get(), &HubConnection::errorOccurred);
    conn_->setExpectedFingerprint(QString(64, QLatin1Char('A')));  // not this server's certificate
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::Disconnected);
    QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 8000);
    QCOMPARE(errors.first().at(0).toString(), QStringLiteral("security_code_mismatch"));
    QCOMPARE(conn_->errorCode(), QStringLiteral("security_code_mismatch"));
    QCOMPARE(hub.requests.size(), 1);  // only the info endpoint was asked
    QVERIFY(profiles_->profiles().isEmpty());
    QVERIFY(!conn_->trustedByLink());
    QVERIFY(!conn_->observedFingerprint().isEmpty());  // for the "Details" view
    conn_->retry();  // no way around it
    QVERIFY(conn_->state() == State::Disconnected);
  }

  void expectedFingerprintIsDroppedOnDisconnect() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    conn_->setExpectedFingerprint(hub.fingerprint());
    conn_->disconnectFromHub();
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsTrustConfirmation);  // back to comparing by eye
    QVERIFY(!conn_->trustedByLink());
    // It also applies to one connect only.
    conn_->setExpectedFingerprint(hub.fingerprint());
    conn_->connectToAddress(hub.address());
    WAIT_STATE(*conn_, State::NeedsPairing);
    conn_->disconnectFromHub();
    QVERIFY(!conn_->trustedByLink());
    conn_->connectToProfile(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    QVERIFY(!conn_->trustedByLink());
  }

  void expectedFingerprintAlsoChecksAKnownServer() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    trust(hub);
    conn_->disconnectFromHub();
    conn_->setExpectedFingerprint(QString(64, QLatin1Char('B')));
    conn_->connectToAddress(hub.address());  // pinned certificate matches, the link's code does not
    WAIT_STATE(*conn_, State::Disconnected);
    QCOMPARE(conn_->errorCode(), QStringLiteral("security_code_mismatch"));
  }

  // ---- several people on one PC ----

  void aSecondPersonOnTheSamePc() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    const QString annaDevice = profiles_->installId();
    const int tokensBefore = hub.tokenRequests();

    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    QVERIFY(conn_->addingAccount());
    QCOMPARE(hub.tokenRequests(), tokensBefore);  // identified only, nobody signed in
    // The stored accounts are untouched until the new person is in.
    QCOMPARE(profiles_->profile(hub.hubId)->activeUserId, QStringLiteral("u_anna"));
    QVERIFY(!profiles_->profile(hub.hubId)->credentialRef.isEmpty());
    QVERIFY(conn_->deviceId() != annaDevice);
    QVERIFY(!QUuid::fromString(conn_->deviceId()).isNull());

    // A wrong password does not change that either.
    { ErrorSpy e(conn_.get());
      conn_->signIn(QStringLiteral("ben"), QStringLiteral("not the password"));
      QVERIFY(e.waitFor(QStringLiteral("invalid_credentials"))); }
    QCOMPARE(profiles_->accounts(hub.hubId).size(), 1);

    signInAs(hub, QStringLiteral("ben"));
    const QJsonObject benBody = hub.signInBodies.last();
    const QString benDevice = benBody.value(QStringLiteral("device_id")).toString();
    QVERIFY(benDevice != annaDevice);
    QCOMPARE(benBody.value(QStringLiteral("install_id")).toString(), profiles_->installId());  // one PC
    QCOMPARE(hub.signInBodies.first().value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QVERIFY(!conn_->addingAccount());

    const auto p = profiles_->profile(hub.hubId);
    QCOMPARE(p->accounts.size(), 2);
    QCOMPARE(p->activeUserId, QStringLiteral("u_ben"));  // the new person is active
    QCOMPARE(p->hubUserId, QStringLiteral("u_ben"));
    QCOMPARE(p->deviceId, benDevice);
    QCOMPARE(profiles_->account(hub.hubId, QStringLiteral("u_anna"))->deviceId, annaDevice);
    QCOMPARE(profiles_->account(hub.hubId, QStringLiteral("u_ben"))->displayName, QStringLiteral("ben"));
    QCOMPARE(hub.callerDeviceId, benDevice);
    // Two credentials, one per device.
    QVERIFY(readCredential(*creds_, profiles_->account(hub.hubId, QStringLiteral("u_anna"))->credentialRef).has_value());
    QVERIFY(readCredential(*creds_, profiles_->account(hub.hubId, QStringLiteral("u_ben"))->credentialRef).has_value());
    QVERIFY(profiles_->account(hub.hubId, QStringLiteral("u_anna"))->credentialRef !=
            profiles_->account(hub.hubId, QStringLiteral("u_ben"))->credentialRef);

    // Connect as each of them.
    conn_->connectToProfileAs(hub.hubId, QStringLiteral("u_anna"));
    WAIT_STATE(*conn_, State::Connected);
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_anna"));
    QCOMPARE(conn_->userDisplayName(), QStringLiteral("anna"));
    QCOMPARE(hub.callerDeviceId, annaDevice);
    QCOMPARE(profiles_->profile(hub.hubId)->activeUserId, QStringLiteral("u_anna"));
    conn_->connectToProfileAs(hub.hubId, QStringLiteral("u_ben"));
    WAIT_STATE(*conn_, State::Connected);
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_ben"));
    QCOMPARE(hub.callerDeviceId, benDevice);
    // The plain connect uses the active one.
    conn_->disconnectFromHub();
    conn_->connectToProfile(hub.hubId);
    WAIT_STATE(*conn_, State::Connected);
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_ben"));

    // Unknown person.
    conn_->connectToProfileAs(hub.hubId, QStringLiteral("u_nobody"));
    QVERIFY(conn_->state() == State::Disconnected);
    QCOMPARE(conn_->errorCode(), QStringLiteral("unknown_account"));
  }

  void abandoningAddAccountChangesNothing() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    const HubProfile before = *profiles_->profile(hub.hubId);
    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    conn_->disconnectFromHub();
    const HubProfile after = *profiles_->profile(hub.hubId);
    QCOMPARE(after.activeUserId, before.activeUserId);
    QCOMPARE(after.credentialRef, before.credentialRef);
    QCOMPARE(after.deviceId, before.deviceId);
    QCOMPARE(after.accounts.size(), 1);
    conn_->connectToProfile(hub.hubId);
    WAIT_STATE(*conn_, State::Connected);
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_anna"));
  }

  void aSecondPersonByApproval() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    hub.decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    WAIT_STATE(*conn_, State::Connected);
    const QJsonObject body = lastBodyOf(hub, QStringLiteral("/api/v1/pairing/requests"));
    QVERIFY(body.value(QStringLiteral("device_id")).toString() != profiles_->installId());
    QCOMPARE(body.value(QStringLiteral("install_id")).toString(), profiles_->installId());
    QCOMPARE(profiles_->accounts(hub.hubId).size(), 2);
    QCOMPARE(profiles_->profile(hub.hubId)->hubUserId, QStringLiteral("u_test_1"));
  }

  // ---- removing a person from this PC ----

  void removeTheConnectedPerson() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    signInAs(hub, QStringLiteral("ben"));
    LocalPin pin(profiles_.get(), creds_.get());
    QVERIFY(pin.setPin(hub.hubId, QStringLiteral("u_ben"), QStringLiteral("2468")));
    const HubAccount ben = *profiles_->account(hub.hubId, QStringLiteral("u_ben"));
    QVERIFY(creds_->read(pinTarget(hub.hubId, ben.deviceId)).has_value());
    QSignalSpy removed(conn_.get(), &HubConnection::accountRemoved);

    QCOMPARE(conn_->removeAccountFromThisPc(hub.hubId, QStringLiteral("u_ben")), HubConnection::RemoveResult::Revoking);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed.first().at(1).toString(), QStringLiteral("u_ben"));
    QTRY_VERIFY_WITH_TIMEOUT(hub.revoked, 8000);  // the device revoked itself on the server
    QVERIFY(conn_->state() == State::NeedsPairing);
    QVERIFY(!readCredential(*creds_, ben.credentialRef).has_value());
    QVERIFY(!creds_->read(pinTarget(hub.hubId, ben.deviceId)).has_value());
    const auto p = profiles_->profile(hub.hubId);
    QCOMPARE(p->accounts.size(), 1);
    QCOMPARE(p->accounts.first().hubUserId, QStringLiteral("u_anna"));
    QVERIFY(p->activeUserId.isEmpty());
    QVERIFY(p->credentialRef.isEmpty());
    QCOMPARE(conn_->removeAccountFromThisPc(hub.hubId, QStringLiteral("u_ben")), HubConnection::RemoveResult::Unknown);
    // Anna is untouched and can be connected.
    hub.revoked = false;
    conn_->connectToProfileAs(hub.hubId, QStringLiteral("u_anna"));
    WAIT_STATE(*conn_, State::Connected);
  }

  void removeAnotherPersonAndPendingSaves() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    signInAs(hub, QStringLiteral("ben"));
    const HubAccount anna = *profiles_->account(hub.hubId, QStringLiteral("u_anna"));

    // A save of Anna's that is not on the server yet.
    const QString gameDir = SaveStore::gameDir(*profiles_, hub.hubId, QStringLiteral("u_anna"), QStringLiteral("game-1"));
    QVERIFY(!gameDir.isEmpty());
    QVERIFY(QDir().mkpath(gameDir));
    SyncState st;
    st.pending = true;
    QVERIFY(SaveStore::saveState(gameDir, st));
    QCOMPARE(conn_->pendingSaves(hub.hubId, QStringLiteral("u_anna")), 1);
    QCOMPARE(conn_->pendingSaves(hub.hubId, QStringLiteral("u_ben")), 0);

    QCOMPARE(conn_->removeAccountFromThisPc(hub.hubId, QStringLiteral("u_anna")), HubConnection::RemoveResult::PendingSaves);
    QCOMPARE(profiles_->accounts(hub.hubId).size(), 2);  // refused, nothing changed
    QVERIFY(readCredential(*creds_, anna.credentialRef).has_value());
    QCOMPARE(conn_->removeAccountFromThisPc(hub.hubId, QStringLiteral("u_anna"), /*force=*/true), HubConnection::RemoveResult::Removed);
    QCOMPARE(profiles_->accounts(hub.hubId).size(), 1);
    QVERIFY(!readCredential(*creds_, anna.credentialRef).has_value());
    // The connection of the other person is not disturbed, and the saves stay on disk.
    QVERIFY(conn_->state() == State::Connected);
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_ben"));
    QVERIFY(QFileInfo::exists(gameDir));
    QVERIFY(!hub.revoked);
  }

  void passwordUnlocksAPinLockedPersonAndKeepsThePin() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    signInAs(hub, QStringLiteral("ben"));
    const QString anna = QStringLiteral("u_anna");

    LocalPin pin(profiles_.get(), creds_.get());
    QVERIFY(pin.setPin(hub.hubId, anna, QStringLiteral("4821")));
    for (int i = 0; i < kLocalPinMaxTries; ++i) QVERIFY(!pin.verify(hub.hubId, anna, QStringLiteral("0000")).ok());
    QVERIFY(pin.isLocked(hub.hubId, anna));
    const QString deviceBefore = profiles_->account(hub.hubId, anna)->deviceId;

    conn_->beginSignInAs(hub.hubId, anna);
    WAIT_STATE(*conn_, State::NeedsPairing);
    QVERIFY(pin.isLocked(hub.hubId, anna));  // nothing changes before the sign-in succeeds
    signInAs(hub, QStringLiteral("anna"));
    QCOMPARE(conn_->hubUserId(), anna);
    QVERIFY(!pin.isLocked(hub.hubId, anna));
    QVERIFY(pin.hasPin(hub.hubId, anna));
    QCOMPARE(profiles_->account(hub.hubId, anna)->deviceId, deviceBefore);  // the PIN hash is bound to this device ID
    QVERIFY(pin.verify(hub.hubId, anna, QStringLiteral("4821")).ok());
  }

  void removingTheProfileForgetsEveryone() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    conn_->beginAddAccount(hub.hubId);
    WAIT_STATE(*conn_, State::NeedsPairing);
    signInAs(hub, QStringLiteral("ben"));
    const HubAccount anna = *profiles_->account(hub.hubId, QStringLiteral("u_anna"));
    LocalPin pin(profiles_.get(), creds_.get());
    QVERIFY(pin.setPin(hub.hubId, QStringLiteral("u_anna"), QStringLiteral("1357")));
    conn_->removeProfile(hub.hubId);
    QVERIFY(!profiles_->profile(hub.hubId).has_value());
    QVERIFY(!readCredential(*creds_, anna.credentialRef).has_value());
    QVERIFY(!creds_->read(pinTarget(hub.hubId, anna.deviceId)).has_value());
  }

  // ---- the account API ----

  void accountApiNotConnected() {
    AccountApi api(conn_.get());
    const AccountApiResult r = call([&](AccountApi::Callback cb) { api.getAccount(cb); });
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, QStringLiteral("not_connected"));
  }

  void accountApiReadAndRename() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    AccountApi api(conn_.get());

    AccountApiResult r = call([&](AccountApi::Callback cb) { api.getAccount(cb); });
    QVERIFY(r.ok);
    QCOMPARE(r.account.userId, QStringLiteral("u_anna"));
    QCOMPARE(r.account.username, QStringLiteral("anna"));
    QCOMPARE(r.account.role, QStringLiteral("user"));
    QVERIFY(r.account.hasPassword);
    QVERIFY(!r.account.passwordRequired);
    QVERIFY(!r.account.mustChangePassword);
    QVERIFY(!r.account.confirmedUntil.isValid());

    r = call([&](AccountApi::Callback cb) { api.updateDisplayName(QStringLiteral("  Anna K.  "), cb); });
    QVERIFY(r.ok);
    QCOMPARE(r.account.displayName, QStringLiteral("Anna K."));
    r = call([&](AccountApi::Callback cb) { api.updateDisplayName(QStringLiteral("BEN"), cb); });
    QVERIFY(!r.ok);
    QCOMPARE(r.errorCode, QStringLiteral("display_name_taken"));
    QCOMPARE(r.status, 409);
    r = call([&](AccountApi::Callback cb) { api.updateDisplayName(QStringLiteral("   "), cb); });
    QCOMPARE(r.errorCode, QStringLiteral("bad_request"));
  }

  void accountApiPasswords() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    AccountApi api(conn_.get());
    const QString next = QStringLiteral("a brand new password");

    AccountApiResult r = call([&](AccountApi::Callback cb) { api.setPassword(QStringLiteral("wrong"), next, cb); });
    QCOMPARE(r.errorCode, QStringLiteral("invalid_credentials"));
    QVERIFY(conn_->state() == State::Connected);  // a wrong password is no stale token
    r = call([&](AccountApi::Callback cb) { api.setPassword(kPassword, QStringLiteral("short"), cb); });
    QCOMPARE(r.errorCode, QStringLiteral("bad_request"));
    r = call([&](AccountApi::Callback cb) { api.setPassword(kPassword, next, cb); });
    QVERIFY(r.ok);
    QCOMPARE(hub.accountsByUser[QStringLiteral("u_anna")].password, next);

    r = call([&](AccountApi::Callback cb) { api.removePassword(kPassword, cb); });  // the old one no longer fits
    QCOMPARE(r.errorCode, QStringLiteral("invalid_credentials"));
    hub.requirePasswords = true;
    r = call([&](AccountApi::Callback cb) { api.removePassword(next, cb); });
    QCOMPARE(r.errorCode, QStringLiteral("password_required"));
    hub.requirePasswords = false;
    r = call([&](AccountApi::Callback cb) { api.removePassword(next, cb); });
    QVERIFY(r.ok);
    r = call([&](AccountApi::Callback cb) { api.removePassword(QString(), cb); });
    QCOMPARE(r.errorCode, QStringLiteral("password_not_set"));
    // Without a password the first one needs no current password.
    r = call([&](AccountApi::Callback cb) { api.setPassword(QString(), next, cb); });
    QVERIFY(r.ok);
  }

  void accountApiAdminCannotRemoveThePassword() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("max"), kPassword, QStringLiteral("admin"));
    trust(hub);
    signInAs(hub, QStringLiteral("max"));
    AccountApi api(conn_.get());
    AccountApiResult r = call([&](AccountApi::Callback cb) { api.getAccount(cb); });
    QVERIFY(r.account.isAdmin());
    QVERIFY(r.account.passwordRequired);
    r = call([&](AccountApi::Callback cb) { api.removePassword(kPassword, cb); });
    QCOMPARE(r.errorCode, QStringLiteral("password_required"));
    QCOMPARE(r.status, 403);
  }

  void accountApiUnknownDevice() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    AccountApi api(conn_.get());
    const AccountApiResult r = call([&](AccountApi::Callback cb) { api.signOutDevice(QStringLiteral("nope"), cb); });
    QCOMPARE(r.errorCode, QStringLiteral("not_found"));
    QCOMPARE(r.status, 404);
  }

  void accountApiDevices() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    trust(hub);
    signInAs(hub, QStringLiteral("anna"));
    const auto device = [&](const QString& id, const QString& name, bool current, const QString& status) {
      return QJsonObject{{QStringLiteral("device_id"), id},       {QStringLiteral("name"), name},
                         {QStringLiteral("platform"), QStringLiteral("linux")}, {QStringLiteral("arch"), QStringLiteral("x86_64")},
                         {QStringLiteral("app_version"), QStringLiteral("0.18.0")},
                         {QStringLiteral("created_at"), QStringLiteral("2026-10-20T09:00:00Z")},
                         {QStringLiteral("last_seen_at"), current ? QJsonValue(QStringLiteral("2026-10-22T11:58:00Z")) : QJsonValue()},
                         {QStringLiteral("sign_in_kind"), QStringLiteral("password")}, {QStringLiteral("status"), status},
                         {QStringLiteral("install_id"), profiles_->installId()}, {QStringLiteral("current"), current}};
    };
    hub.accountDevices = {device(profiles_->installId(), QStringLiteral("Anna-Laptop"), true, QStringLiteral("trusted")),
                          device(QStringLiteral("dev-2"), QStringLiteral("Anna-Desktop"), false, QStringLiteral("trusted")),
                          device(QStringLiteral("dev-3"), QStringLiteral("Old phone"), false, QStringLiteral("revoked"))};
    AccountApi api(conn_.get());

    AccountApiResult r = call([&](AccountApi::Callback cb) { api.listDevices(cb); });
    QVERIFY(r.ok);
    QCOMPARE(r.devices.size(), 3);
    QCOMPARE(r.devices.at(0).name, QStringLiteral("Anna-Laptop"));
    QVERIFY(r.devices.at(0).current);
    QVERIFY(r.devices.at(0).lastSeenAt.isValid());
    QVERIFY(!r.devices.at(1).lastSeenAt.isValid());
    QCOMPARE(r.devices.at(1).signInKind, QStringLiteral("password"));
    QVERIFY(r.devices.at(2).revoked());
    QCOMPARE(r.devices.at(0).installId, profiles_->installId());

    r = call([&](AccountApi::Callback cb) { api.signOutDevice(QStringLiteral("dev-2"), cb); });
    QVERIFY(r.ok);
    r = call([&](AccountApi::Callback cb) { api.listDevices(cb); });
    QVERIFY(r.devices.at(1).revoked());
    r = call([&](AccountApi::Callback cb) { api.signOutDevice(QStringLiteral("dev-404"), cb); });
    QCOMPARE(r.errorCode, QStringLiteral("not_found"));

    hub.accountDevices[1].insert(QStringLiteral("status"), QStringLiteral("trusted"));
    r = call([&](AccountApi::Callback cb) { api.signOutAll(false, cb); });
    QVERIFY(r.ok);
    QCOMPARE(r.signedOut, 1);  // this device stays
    r = call([&](AccountApi::Callback cb) { api.signOutAll(true, cb); });
    QCOMPARE(r.signedOut, 1);  // now it goes too
  }
};

QTEST_GUILESS_MAIN(AccountsTest)
#include "accounts_test.moc"
