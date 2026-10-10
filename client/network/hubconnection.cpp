#include "hubconnection.h"

#include <QLoggingCategory>
#include <QSslConfiguration>
#include <algorithm>
#include <utility>

namespace framebeam {

Q_LOGGING_CATEGORY(lcHub, "framebeam.hub")

namespace {

constexpr const char* kDefaultApiBase = "/api/v1";

QString validApiBase(const QString& base) {
  if (base.startsWith(QLatin1Char('/')) && !base.contains(QLatin1String("//")) && !base.contains(QLatin1String("..")) &&
      !base.contains(QLatin1Char('?')) && !base.contains(QLatin1Char('#'))) {
    QString b = base;
    while (b.endsWith(QLatin1Char('/'))) {
      b.chop(1);
    }
    return b;
  }
  return QString::fromLatin1(kDefaultApiBase);
}

}  // namespace

HubConnection::HubConnection(ProfileStore* profiles, CredentialStore* credentials, QObject* parent)
    : QObject(parent), profiles_(profiles), credentials_(credentials), handshake_(HandshakeInfo::detect()) {
  refreshTimer_.setSingleShot(true);
  connect(&refreshTimer_, &QTimer::timeout, this, &HubConnection::refreshToken);
  connect(&pollTimer_, &QTimer::timeout, this, &HubConnection::pollPairing);
}

HubConnection::~HubConnection() {
  ++gen_;
  accessToken_.clear();  // aborted replies run their callbacks synchronously: authorizedGet/Send must refuse by then
  const auto replies = std::exchange(inflight_, {});
  for (QNetworkReply* r : replies) {
    r->abort();
  }
}

QString HubConnection::stateName(State s) {
  switch (s) {
    case State::Disconnected: return QStringLiteral("Disconnected");
    case State::Identifying: return QStringLiteral("Identifying");
    case State::NeedsTrustConfirmation: return QStringLiteral("NeedsTrustConfirmation");
    case State::CertificateChanged: return QStringLiteral("CertificateChanged");
    case State::Incompatible: return QStringLiteral("Incompatible");
    case State::Unreachable: return QStringLiteral("Unreachable");
    case State::NeedsPairing: return QStringLiteral("NeedsPairing");
    case State::AwaitingApproval: return QStringLiteral("AwaitingApproval");
    case State::Denied: return QStringLiteral("Denied");
    case State::Expired: return QStringLiteral("Expired");
    case State::Authenticating: return QStringLiteral("Authenticating");
    case State::UserDisabled: return QStringLiteral("UserDisabled");
    case State::Connected: return QStringLiteral("Connected");
  }
  return {};
}

void HubConnection::setState(State s) {
  if (state_ == s) {
    return;
  }
  state_ = s;
  qCInfo(lcHub) << "state" << stateName(s);
  emit stateChanged(s);
}

void HubConnection::fail(State s, const QString& code, const QString& message) {
  errorCode_ = code;
  errorMessage_ = message;
  qCWarning(lcHub) << "error" << code << message;
  setState(s);
  emit errorOccurred(code, message);
}

void HubConnection::reset() {
  ++gen_;
  refreshTimer_.stop();
  pollTimer_.stop();
  pollInFlight_ = false;
  refreshInFlight_ = false;
  // Invalidate first, abort afterwards: abort() emits finished synchronously and the callbacks of authorized replies
  // (SaveApi, RomDownloader, ...) may start new requests. With the token and http_ gone those return nullptr instead
  // of creating replies that would be dropped without ever finishing.
  accessToken_.clear();
  pollToken_.clear();
  pairingRequestId_.clear();
  HubHttp* oldHttp = std::exchange(http_, nullptr);
  const auto replies = std::exchange(inflight_, {});
  for (QNetworkReply* r : replies) {
    r->abort();
  }
  if (oldHttp != nullptr) {
    oldHttp->deleteLater();
  }
  hubInfo_ = {};
  profile_.reset();
  problems_.clear();
  features_.clear();
  incompatible_ = IncompatibleReason::None;
  errorCode_.clear();
  errorMessage_.clear();
  observedFp_.clear();
  pin_.clear();
}

void HubConnection::disconnectFromHub() {
  reset();
  setState(State::Disconnected);
}

void HubConnection::track(QNetworkReply* reply, Handler handler) {
  inflight_.insert(reply);
  const quint64 gen = gen_;
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen, handler = std::move(handler)]() {
    inflight_.remove(reply);
    reply->deleteLater();
    if (gen != gen_) {
      return;
    }
    handler(HubHttp::resultOf(reply));
  });
}

QString HubConnection::apiPath(const QString& rel) const {
  return validApiBase(hubInfo_.apiBase) + rel;
}

// ---------------------------------------------------------------- Identification

namespace {
constexpr int kDefaultHubPort = 8443;
}  // namespace

void HubConnection::connectToAddress(const QString& address, bool allowHttp) {
  startIdentify(address, allowHttp, std::nullopt);
}

void HubConnection::connectToProfile(const QString& hubId) {
  const auto p = profiles_->profile(hubId);
  if (!p) {
    disconnectFromHub();
    fail(State::Disconnected, QStringLiteral("unknown_profile"), QStringLiteral("Unknown hub profile"));
    return;
  }
  startIdentify(p->address, p->allowHttp, p);
}

void HubConnection::startIdentify(const QString& addressInput, bool allowHttp, std::optional<HubProfile> existing) {
  if (state_ != State::Disconnected) {
    disconnectFromHub();  // disconnect the old connection first
  } else {
    reset();
  }
  QString text = addressInput.trimmed();
  if (!text.contains(QLatin1String("://"))) {
    text.prepend(QStringLiteral("https://"));
  }
  const QUrl parsed(text, QUrl::StrictMode);
  if (!parsed.isValid() || parsed.host().isEmpty() ||
      (parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http"))) {
    fail(State::Unreachable, QStringLiteral("invalid_address"), QStringLiteral("Invalid hub address"));
    return;
  }
  QUrl base;
  base.setScheme(parsed.scheme());
  base.setHost(parsed.host());
  // Without a port: hub default :8443 (docs/guides/hub-configuration.md), also for http (dev flag). Explicit ports are kept.
  base.setPort(parsed.port() > 0 ? parsed.port() : kDefaultHubPort);
  if (!HubHttp::isSchemeAllowed(base, allowHttp)) {
    fail(State::Unreachable, QStringLiteral("insecure_http"),
         QStringLiteral("HTTP is only allowed for localhost or with the dev flag"));
    return;
  }
  address_ = base.toString(QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
  allowHttp_ = allowHttp;
  if (!existing) {
    existing = profiles_->profileByAddress(address_);
  }
  profile_ = existing;
  pin_ = existing ? existing->pinnedFingerprint : QString();

  setState(State::Identifying);
  http_ = new HubHttp(base, pin_, this);
  QNetworkReply* reply = http_->get(QStringLiteral("/.well-known/framebeam"), {}, {}, /*allowUnpinned=*/true);
  track(reply, [this](const HttpResult& r) { onIdentified(r); });
}

void HubConnection::retry() {
  if ((state_ == State::Unreachable || state_ == State::UserDisabled) && !address_.isEmpty()) {
    startIdentify(address_, allowHttp_, profile_);
  }
}

bool HubConnection::handleCommonFailure(const HttpResult& r) {
  if (r.certMismatch) {
    observedFp_ = r.observedFingerprint;
    fail(State::CertificateChanged, QStringLiteral("certificate_changed"),
         QStringLiteral("The hub's certificate differs from the stored fingerprint"));
    return true;
  }
  if (r.networkError) {
    fail(State::Unreachable, QStringLiteral("unreachable"), r.errorString);
    return true;
  }
  return false;
}

void HubConnection::onIdentified(const HttpResult& r) {
  if (handleCommonFailure(r)) {
    return;
  }
  const auto info = r.status == 200 ? parseHubInfo(r.json()) : std::nullopt;
  if (!info || !ProfileStore::isValidHubId(info->hubId)) {
    fail(State::Unreachable, QStringLiteral("invalid_hub_info"),
         QStringLiteral("The address does not respond like a FrameBeam Hub"));
    return;
  }
  hubInfo_ = *info;
  observedFp_ = r.observedFingerprint;
  const bool tls = http_->baseUrl().scheme() == QLatin1String("https");

  // A different hub answers than the one of the profile this identification started with.
  if (profile_ && profile_->hubId != info->hubId) {
    if (!pin_.isEmpty()) {
      fail(State::Unreachable, QStringLiteral("hub_id_mismatch"),
           QStringLiteral("A different hub responds at this address than the one in the profile"));
      return;
    }
    profile_.reset();  // plain-HTTP profile: never reuse its credentialRef / user id for the other hub
  }
  // Profile for this hub ID (e.g. changed address): its pin applies.
  if (pin_.isEmpty()) {
    if (const auto byId = profiles_->profile(info->hubId)) {
      profile_ = byId;
      pin_ = byId->pinnedFingerprint;
      if (tls && !pin_.isEmpty() && !HubHttp::fingerprintsEqual(observedFp_, pin_)) {
        fail(State::CertificateChanged, QStringLiteral("certificate_changed"),
             QStringLiteral("The hub's certificate differs from the stored fingerprint"));
        return;
      }
      http_->setPinnedFingerprint(pin_);
    }
  }

  if (info->minProtocolVersion > handshake_.protocolVersion) {
    incompatible_ = IncompatibleReason::PlayerTooOld;
    fail(State::Incompatible, QStringLiteral("player_too_old"), QStringLiteral("The FrameBeam Player is too old for this hub"));
    return;
  }
  if (info->protocolVersion < handshake_.minProtocolVersion) {
    incompatible_ = IncompatibleReason::HubTooOld;
    fail(State::Incompatible, QStringLiteral("hub_too_old"), QStringLiteral("The FrameBeam Hub is too old for this player"));
    return;
  }

  if (tls && pin_.isEmpty()) {
    if (observedFp_.isEmpty()) {
      fail(State::Unreachable, QStringLiteral("no_certificate"), QStringLiteral("The hub did not provide a certificate"));
      return;
    }
    setState(State::NeedsTrustConfirmation);
    return;
  }
  trusted();
}

void HubConnection::confirmTrust() {
  if (state_ != State::NeedsTrustConfirmation || observedFp_.isEmpty()) {
    return;
  }
  pin_ = observedFp_;
  http_->setPinnedFingerprint(pin_);
  trusted();
}

void HubConnection::rejectTrust() {
  if (state_ == State::NeedsTrustConfirmation) {
    disconnectFromHub();
  }
}

bool HubConnection::confirmCertificateChange(const QString& confirmedFingerprint) {
  if (state_ != State::CertificateChanged || !profile_ || observedFp_.isEmpty() || confirmedFingerprint.isEmpty() ||
      !HubHttp::fingerprintsEqual(confirmedFingerprint, observedFp_)) {
    return false;
  }
  HubProfile p = *profile_;
  p.pinnedFingerprint = observedFp_;
  if (!profiles_->upsertProfile(p)) {
    qCWarning(lcHub) << "profiles.json could not be written";
    return false;
  }
  const QString address = address_;
  const bool allowHttp = allowHttp_;
  startIdentify(address, allowHttp, p);
  return true;
}

void HubConnection::trusted() {
  HubProfile p = profile_.value_or(HubProfile());
  p.hubId = hubInfo_.hubId;
  p.name = hubInfo_.name;
  p.address = address_;
  p.pinnedFingerprint = pin_;
  p.allowHttp = allowHttp_;
  p.deviceId = profiles_->deviceId();
  if (!profiles_->upsertProfile(p)) {
    qCWarning(lcHub) << "profiles.json could not be written";
  }
  profile_ = p;
  if (!p.credentialRef.isEmpty() && credentials_->read(p.credentialRef).has_value()) {
    authenticate();
  } else {
    setState(State::NeedsPairing);
  }
}

// ---------------------------------------------------------------- Pairing

void HubConnection::requestPairing() {
  if (state_ != State::NeedsPairing && state_ != State::Denied && state_ != State::Expired) {
    return;
  }
  const QJsonObject body{{QStringLiteral("device_id"), profiles_->deviceId()},
                         {QStringLiteral("device_name"), profiles_->deviceName()},
                         {QStringLiteral("platform"), handshake_.platform},
                         {QStringLiteral("arch"), handshake_.arch},
                         {QStringLiteral("player_version"), handshake_.playerVersion},
                         {QStringLiteral("protocol_version"), handshake_.protocolVersion}};
  QNetworkReply* reply = http_->postJson(apiPath(QStringLiteral("/pairing/requests")), body);
  track(reply, [this](const HttpResult& r) {
    if (handleCommonFailure(r)) {
      return;
    }
    if (r.status != 202) {
      const QString code = r.apiErrorCode.isEmpty() ? QStringLiteral("pairing_failed") : r.apiErrorCode;
      errorCode_ = code;
      errorMessage_ = r.apiErrorMessage;
      emit errorOccurred(code, r.apiErrorMessage);  // state is kept (e.g. rate_limited)
      return;
    }
    onPairingAccepted(r.json());
  });
}

void HubConnection::onPairingAccepted(const QJsonObject& o) {
  if (o.value(QStringLiteral("request_id")).toString().isEmpty() || o.value(QStringLiteral("poll_token")).toString().isEmpty()) {
    errorCode_ = QStringLiteral("pairing_failed");
    errorMessage_.clear();
    emit errorOccurred(errorCode_, errorMessage_);
    return;
  }
  pairingRequestId_ = o.value(QStringLiteral("request_id")).toString();
  pollToken_ = o.value(QStringLiteral("poll_token")).toString().toUtf8();
  setState(State::AwaitingApproval);
  pollTimer_.start(pollIntervalMs_);
}

void HubConnection::redeemInvite(const QString& code, const QString& displayName) {
  if (state_ != State::NeedsPairing && state_ != State::Denied && state_ != State::Expired) {
    return;
  }
  const QString name = displayName.trimmed();
  const QString normalized = code.trimmed().toUpper();
  if (normalized.isEmpty() || name.isEmpty() || name.size() > 32) {
    errorCode_ = QStringLiteral("invalid_input");
    errorMessage_.clear();
    emit errorOccurred(errorCode_, errorMessage_);
    return;
  }
  const QJsonObject body{{QStringLiteral("code"), normalized},
                         {QStringLiteral("display_name"), name},
                         {QStringLiteral("device_id"), profiles_->deviceId()},
                         {QStringLiteral("device_name"), profiles_->deviceName()},
                         {QStringLiteral("platform"), handshake_.platform},
                         {QStringLiteral("arch"), handshake_.arch},
                         {QStringLiteral("player_version"), handshake_.playerVersion},
                         {QStringLiteral("protocol_version"), handshake_.protocolVersion}};
  // The code is a secret until redeemed: it is never logged.
  QNetworkReply* reply = http_->postJson(apiPath(QStringLiteral("/invites/redeem")), body);
  track(reply, [this](const HttpResult& r) {
    if (handleCommonFailure(r)) {
      return;
    }
    const QJsonObject o = r.json();
    if (r.status == 200 && o.value(QStringLiteral("status")).toString() == QLatin1String("approved")) {
      onPairingApproved(o);  // credential delivered once; stored like a normal pairing, then token + handshake
      return;
    }
    if (r.status == 202) {
      onPairingAccepted(o);
      return;
    }
    const QString code = r.apiErrorCode.isEmpty() ? QStringLiteral("invite_failed") : r.apiErrorCode;
    errorCode_ = code;
    errorMessage_ = r.apiErrorMessage;
    emit errorOccurred(code, r.apiErrorMessage);  // state is kept
  });
}

bool HubConnection::isLoopbackHub() const {
  return http_ != nullptr && HubHttp::isLoopbackHost(http_->baseUrl().host());
}

void HubConnection::fetchLocalStatus(std::function<void(bool, const QJsonObject&, const QString&)> done) {
  if (http_ == nullptr || !isLoopbackHub()) {
    done(false, {}, QStringLiteral("not_local"));
    return;
  }
  QNetworkReply* reply = http_->get(apiPath(QStringLiteral("/local/status")));
  track(reply, [this, done = std::move(done)](const HttpResult& r) {
    if (r.certMismatch) {
      handleCommonFailure(r);
      done(false, {}, QStringLiteral("certificate_changed"));
      return;
    }
    done(r.ok(), r.json(), r.apiErrorMessage.isEmpty() ? r.errorString : r.apiErrorMessage);
  });
}

void HubConnection::localSetup(const QString& username, const QString& password) {
  localPairingRequest(QStringLiteral("/local/setup"), username, password);
}

void HubConnection::localPair(const QString& username, const QString& password) {
  localPairingRequest(QStringLiteral("/local/pair"), username, password);
}

void HubConnection::localPairingRequest(const QString& path, const QString& username, const QString& password) {
  if (state_ != State::NeedsPairing && state_ != State::Denied && state_ != State::Expired) {
    return;
  }
  if (!isLoopbackHub()) {
    errorCode_ = QStringLiteral("not_local");
    errorMessage_.clear();
    emit errorOccurred(errorCode_, errorMessage_);
    return;
  }
  if (username.trimmed().isEmpty() || password.isEmpty()) {
    errorCode_ = QStringLiteral("invalid_input");
    errorMessage_.clear();
    emit errorOccurred(errorCode_, errorMessage_);
    return;
  }
  const QJsonObject body{{QStringLiteral("username"), username.trimmed()},
                         {QStringLiteral("password"), password},
                         {QStringLiteral("device_id"), profiles_->deviceId()},
                         {QStringLiteral("device_name"), profiles_->deviceName()},
                         {QStringLiteral("platform"), handshake_.platform},
                         {QStringLiteral("arch"), handshake_.arch},
                         {QStringLiteral("player_version"), handshake_.playerVersion},
                         {QStringLiteral("protocol_version"), handshake_.protocolVersion}};
  // The password is a secret: it is never logged.
  QNetworkReply* reply = http_->postJson(apiPath(path), body);
  track(reply, [this](const HttpResult& r) {
    if (handleCommonFailure(r)) {
      return;
    }
    if (r.status == 200 || r.status == 201) {
      onPairingApproved(r.json());  // same payload as an approved pairing: credential stored once, token + handshake
      return;
    }
    const QString code = r.apiErrorCode.isEmpty() ? QStringLiteral("local_setup_failed") : r.apiErrorCode;
    errorCode_ = code;
    errorMessage_ = r.apiErrorMessage;
    emit errorOccurred(code, r.apiErrorMessage);  // state is kept
  });
}

void HubConnection::cancelPairing() {
  if (state_ != State::AwaitingApproval) {
    return;
  }
  pollTimer_.stop();
  pollToken_.clear();
  pairingRequestId_.clear();
  ++gen_;  // discard the in-flight poll response
  pollInFlight_ = false;
  setState(State::NeedsPairing);
}

void HubConnection::pollPairing() {
  if (state_ != State::AwaitingApproval || pollInFlight_ || http_ == nullptr) {
    return;
  }
  pollInFlight_ = true;
  QNetworkReply* reply = http_->get(apiPath(QStringLiteral("/pairing/requests/") + pairingRequestId_), pollToken_);
  track(reply, [this](const HttpResult& r) {
    pollInFlight_ = false;
    if (state_ != State::AwaitingApproval) {
      return;
    }
    if (r.certMismatch) {
      pollTimer_.stop();
      handleCommonFailure(r);
      return;
    }
    if (r.networkError) {
      return;  // the next poll tries again
    }
    if (r.status == 401 || r.status == 404) {
      pollTimer_.stop();
      pollToken_.clear();
      setState(State::Expired);
      return;
    }
    const QJsonObject o = r.json();
    const QString status = o.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("approved")) {
      pollTimer_.stop();
      onPairingApproved(o);
    } else if (status == QLatin1String("denied")) {
      pollTimer_.stop();
      pollToken_.clear();
      setState(State::Denied);
    } else if (status == QLatin1String("expired")) {
      pollTimer_.stop();
      pollToken_.clear();
      setState(State::Expired);
    }
  });
}

void HubConnection::onPairingApproved(const QJsonObject& obj) {
  pollToken_.clear();
  const QString hubId = obj.value(QStringLiteral("hub_id")).toString();
  const QString userId = obj.value(QStringLiteral("user_id")).toString();
  const QString credential = obj.value(QStringLiteral("device_credential")).toString();
  if (hubId != hubInfo_.hubId || credential.isEmpty()) {
    fail(State::NeedsPairing, QStringLiteral("pairing_invalid_response"), QStringLiteral("Invalid approval response from the hub"));
    return;
  }
  const QString target = credentialTarget(hubInfo_.hubId, profiles_->deviceId());
  if (!credentials_->write(target, credential)) {
    // The hub delivers the credential only once; pairing must be repeated.
    fail(State::NeedsPairing, QStringLiteral("credential_store_failed"),
         QStringLiteral("The device credential could not be saved to the credential store"));
    return;
  }
  HubProfile p = profile_.value_or(HubProfile());
  p.hubUserId = userId;
  p.deviceId = profiles_->deviceId();
  p.credentialRef = target;
  profiles_->upsertProfile(p);
  profile_ = p;
  authenticate();
}

// ---------------------------------------------------------------- Token / Handshake

void HubConnection::authenticate() {
  setState(State::Authenticating);
  refreshInFlight_ = true;
  const auto secret = credentials_->read(profile_->credentialRef);
  if (!secret) {
    refreshInFlight_ = false;
    setState(State::NeedsPairing);
    return;
  }
  const QJsonObject body{{QStringLiteral("device_id"), profile_->deviceId}, {QStringLiteral("device_credential"), *secret}};
  QNetworkReply* reply = http_->postJson(apiPath(QStringLiteral("/auth/token")), body);
  track(reply, [this](const HttpResult& r) { onTokenResult(r, true); });
}

void HubConnection::refreshToken() {
  if (state_ != State::Connected || refreshInFlight_ || !profile_) {
    return;
  }
  const auto secret = credentials_->read(profile_->credentialRef);
  if (!secret) {
    onCredentialInvalid(QStringLiteral("invalid_credentials"), QStringLiteral("Credential no longer available"));
    return;
  }
  refreshInFlight_ = true;
  const QJsonObject body{{QStringLiteral("device_id"), profile_->deviceId}, {QStringLiteral("device_credential"), *secret}};
  QNetworkReply* reply = http_->postJson(apiPath(QStringLiteral("/auth/token")), body);
  track(reply, [this](const HttpResult& r) { onTokenResult(r, false); });
}

bool HubConnection::handleUserDisabled(const HttpResult& r) {
  if (r.status != 401 || r.apiErrorCode != QLatin1String("user_disabled")) {
    return false;
  }
  qCWarning(lcHub) << "User is disabled on the hub";
  refreshTimer_.stop();
  accessToken_.clear();
  // The credential stays: an admin can enable the user again. No automatic retry; retry() is a user action.
  fail(State::UserDisabled, QStringLiteral("user_disabled"),
       r.apiErrorMessage.isEmpty() ? QStringLiteral("This user is disabled on the Hub") : r.apiErrorMessage);
  return true;
}

void HubConnection::onTokenResult(const HttpResult& r, bool initial) {
  refreshInFlight_ = false;
  if (r.certMismatch || r.networkError) {
    if (initial) {
      handleCommonFailure(r);
    } else if (r.certMismatch) {
      accessToken_.clear();
      handleCommonFailure(r);
    } else {
      qCWarning(lcHub) << "Token renewal failed, retrying in 5 s";
      refreshTimer_.start(5000);
    }
    return;
  }
  if (handleUserDisabled(r)) {
    return;
  }
  if (r.status == 401) {
    onCredentialInvalid(r.apiErrorCode.isEmpty() ? QStringLiteral("invalid_credentials") : r.apiErrorCode,
                        r.apiErrorMessage);
    return;
  }
  const QJsonObject o = r.json();
  const QString token = o.value(QStringLiteral("access_token")).toString();
  if (r.status != 200 || token.isEmpty()) {
    if (initial) {
      fail(State::Unreachable, r.apiErrorCode.isEmpty() ? QStringLiteral("token_failed") : r.apiErrorCode,
           r.apiErrorMessage);
    } else {
      refreshTimer_.start(5000);
    }
    return;
  }
  accessToken_ = token.toUtf8();
  scheduleRefresh(o.value(QStringLiteral("expires_in")).toInt(900));
  if (initial) {
    doHandshake();
  }
}

void HubConnection::scheduleRefresh(int expiresInSec) {
  const int lifetimeMs = std::max(1, expiresInSec) * 1000;
  const int marginMs = std::min(60000, lifetimeMs / 5);
  refreshTimer_.start(std::max(100, lifetimeMs - marginMs));
}

void HubConnection::doHandshake() {
  QNetworkReply* reply = http_->postJson(apiPath(QStringLiteral("/handshake")), handshake_.toJson(), accessToken_);
  track(reply, [this](const HttpResult& r) {
    if (handleCommonFailure(r)) {
      return;
    }
    if (handleUserDisabled(r)) {
      return;
    }
    if (r.status == 401) {
      onCredentialInvalid(r.apiErrorCode.isEmpty() ? QStringLiteral("unauthorized") : r.apiErrorCode, r.apiErrorMessage);
      return;
    }
    const auto res = r.status == 200 ? parseHandshakeResult(r.json()) : std::nullopt;
    if (!res) {
      fail(State::Unreachable, r.apiErrorCode.isEmpty() ? QStringLiteral("handshake_failed") : r.apiErrorCode,
           r.apiErrorMessage);
      return;
    }
    problems_ = res->problems;
    features_ = res->features;
    for (const HandshakeProblem& p : res->problems) {
      if (p.code == QLatin1String("player_too_old") || p.code == QLatin1String("hub_too_old")) {
        incompatible_ = p.code == QLatin1String("player_too_old") ? IncompatibleReason::PlayerTooOld : IncompatibleReason::HubTooOld;
        accessToken_.clear();
        refreshTimer_.stop();
        fail(State::Incompatible, p.code, p.detail);
        return;
      }
    }
    // Core/capability problems do not block the connection; handshakeProblems() is for the UI.
    HubProfile p = profile_.value_or(HubProfile());
    p.lastConnected = QDateTime::currentDateTimeUtc();
    p.userDisplayName = res->userDisplayName;  // cleared when an older Hub omits `user`
    profiles_->upsertProfile(p);
    profiles_->setLastHubId(p.hubId);
    profile_ = p;
    setState(State::Connected);
  });
}

void HubConnection::onCredentialInvalid(const QString& code, const QString& message) {
  qCWarning(lcHub) << "Credential invalid/revoked:" << code;
  refreshTimer_.stop();
  accessToken_.clear();
  if (profile_ && !profile_->credentialRef.isEmpty()) {
    credentials_->remove(profile_->credentialRef);
    HubProfile p = *profile_;
    p.credentialRef.clear();
    p.hubUserId.clear();
    profiles_->upsertProfile(p);
    profile_ = p;
  }
  fail(State::NeedsPairing, code, message);
}

void HubConnection::revokeSelf() {
  if (state_ != State::Connected || http_ == nullptr) {
    return;
  }
  QNetworkReply* reply = http_->postJson(apiPath(QStringLiteral("/auth/revoke")), {}, accessToken_);
  track(reply, [this](const HttpResult& r) {
    if (r.status == 204 || r.status == 401) {
      onCredentialInvalid(QStringLiteral("device_revoked"), QStringLiteral("Device was revoked"));
      return;
    }
    if (!handleCommonFailure(r)) {
      emit errorOccurred(r.apiErrorCode, r.apiErrorMessage);
    }
  });
}

void HubConnection::removeProfile(const QString& hubId) {
  if (profile_ && profile_->hubId == hubId) {
    disconnectFromHub();
  }
  if (const auto p = profiles_->profile(hubId)) {
    if (!p->credentialRef.isEmpty()) {
      credentials_->remove(p->credentialRef);
    }
  }
  profiles_->removeProfile(hubId);
}

QNetworkReply* HubConnection::authorizedGet(const QString& apiRelPath, const HttpHeaders& headers) {
  if (state_ != State::Connected || http_ == nullptr || accessToken_.isEmpty()) {
    return nullptr;
  }
  QNetworkReply* reply = http_->get(apiPath(apiRelPath), accessToken_, headers);
  inflight_.insert(reply);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() { inflight_.remove(reply); });
  return reply;
}

QNetworkReply* HubConnection::authorizedSend(const QByteArray& method, const QString& apiRelPath, const QByteArray& body,
                                             const HttpHeaders& headers, const QByteArray& contentType) {
  if (state_ != State::Connected || http_ == nullptr || accessToken_.isEmpty()) {
    return nullptr;
  }
  QNetworkReply* reply = http_->send(method, apiPath(apiRelPath), body, accessToken_, headers, contentType);
  inflight_.insert(reply);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() { inflight_.remove(reply); });
  return reply;
}

QNetworkReply* HubConnection::authorizedSendStream(const QByteArray& method, const QString& apiRelPath, QIODevice* body,
                                                   const HttpHeaders& headers, const QByteArray& contentType) {
  if (state_ != State::Connected || http_ == nullptr || accessToken_.isEmpty()) {
    return nullptr;
  }
  QNetworkReply* reply = http_->sendStream(method, apiPath(apiRelPath), body, accessToken_, headers, contentType);
  inflight_.insert(reply);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() { inflight_.remove(reply); });
  return reply;
}

QNetworkRequest HubConnection::webSocketRequest(const QString& apiRelPath) const {
  if (state_ != State::Connected || http_ == nullptr || accessToken_.isEmpty()) {
    return QNetworkRequest();
  }
  QUrl url = http_->baseUrl();
  const bool tls = url.scheme().toLower() == QLatin1String("https");
  url.setScheme(tls ? QStringLiteral("wss") : QStringLiteral("ws"));
  url.setPath(apiPath(apiRelPath));
  QNetworkRequest req(url);
  req.setRawHeader("Authorization", "Bearer " + accessToken_);
  if (tls) {
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    cfg.setCaCertificates({});  // only the pin decides
    cfg.setPeerVerifyMode(QSslSocket::VerifyPeer);
    cfg.setProtocol(QSsl::TlsV1_2OrLater);
    req.setSslConfiguration(cfg);
  }
  return req;
}

void HubConnection::noteUnauthorized() {
  if (state_ == State::Connected && !refreshInFlight_) {
    refreshTimer_.stop();
    refreshToken();
  }
}

}  // namespace framebeam
