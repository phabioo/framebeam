#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

#include "credentialstore.h"
#include "hubhttp.h"
#include "hubprotocol.h"
#include "profilestore.h"

namespace framebeam {

// Connection state machine for exactly one hub (identification, TOFU/pinning, approval pairing,
// token, handshake). The UI binds to stateChanged()/errorOccurred(); actions are slots.
class HubConnection : public QObject {
  Q_OBJECT
 public:
  enum class State {
    Disconnected,
    Identifying,
    NeedsTrustConfirmation,  // first contact: show observedFingerprint(), confirmTrust()/rejectTrust()
    CertificateChanged,      // certificate differs from the pin: blocked, never silently accepted
    Incompatible,            // incompatibleReason()
    Unreachable,             // errorCode()/errorMessage()
    NeedsPairing,            // requestPairing()
    AwaitingApproval,        // polling in progress, cancelPairing()
    Denied,
    Expired,
    Authenticating,
    UserDisabled,            // the Hub reports user_disabled (401): credential kept, no retry loop; retry() tries once more
    Connected
  };
  Q_ENUM(State)

  enum class IncompatibleReason { None, PlayerTooOld, HubTooOld };
  Q_ENUM(IncompatibleReason)

  HubConnection(ProfileStore* profiles, CredentialStore* credentials, QObject* parent = nullptr);
  ~HubConnection() override;

  static QString stateName(State s);

  // Handshake data (including cores); set before connecting.
  void setHandshakeInfo(const HandshakeInfo& info) { handshake_ = info; }
  void setPollIntervalMs(int ms) { pollIntervalMs_ = ms; }

  State state() const { return state_; }
  IncompatibleReason incompatibleReason() const { return incompatible_; }
  QString errorCode() const { return errorCode_; }
  QString errorMessage() const { return errorMessage_; }
  QString address() const { return address_; }
  const HubInfo& hubInfo() const { return hubInfo_; }
  std::optional<HubProfile> profile() const { return profile_; }
  QString observedFingerprint() const { return observedFp_; }  // for NeedsTrustConfirmation/CertificateChanged
  QString expectedFingerprint() const { return pin_; }         // pinned value (CertificateChanged)
  QList<HandshakeProblem> handshakeProblems() const { return problems_; }
  // Feature flags advertised by the hub in the handshake (e.g. "saves_v1"); empty until Connected.
  bool hubHasFeature(const QString& feature) const { return features_.contains(feature); }
  QString hubId() const { return hubInfo_.hubId; }
  QString hubUserId() const { return profile_ ? profile_->hubUserId : QString(); }
  QString userDisplayName() const { return profile_ ? profile_->userDisplayName : QString(); }
  QString deviceId() const { return profile_ ? profile_->deviceId : QString(); }

  // Exactly one active hub: calls first disconnect the old connection.
  void connectToAddress(const QString& address, bool allowHttp = false);
  void connectToProfile(const QString& hubId);
  void disconnectFromHub();

  void confirmTrust();  // NeedsTrustConfirmation: pin the fingerprint and continue
  void rejectTrust();   // -> Disconnected
  // CertificateChanged: replaces the stored pin by exactly `confirmedFingerprint` (must equal observedFingerprint(),
  // otherwise nothing happens and false is returned), keeps the credential and identifies again. No token has been
  // sent to the new certificate; if the hub presents yet another certificate on the reconnect it is a mismatch again.
  bool confirmCertificateChange(const QString& confirmedFingerprint);
  void requestPairing();  // NeedsPairing/Denied/Expired -> AwaitingApproval
  // Onboarding invite (POST /invites/redeem) from NeedsPairing/Denied/Expired: 200 -> credential stored, signs in
  // directly; 202 -> AwaitingApproval (same polling as requestPairing). Errors (invite_invalid, display_name_taken,
  // rate_limited, ...) keep the state and are reported via errorOccurred(code, message).
  void redeemInvite(const QString& code, const QString& displayName);
  void cancelPairing();   // AwaitingApproval -> NeedsPairing (local; the hub request expires on its own)
  void retry();           // Unreachable/UserDisabled: identify again
  void revokeSelf();      // Connected: the device revokes itself, credential is deleted -> NeedsPairing
  // Removes the profile and its credential (not the server-side revocation).
  void removeProfile(const QString& hubId);

  // Authenticated requests relative to api_base (e.g. "/games"); nullptr if not Connected.
  // Replies are aborted on disconnect. The token never leaves this class.
  QNetworkReply* authorizedGet(const QString& apiPath, const HttpHeaders& headers = {});
  // Same for other methods with a raw body (PUT/POST); nullptr if not Connected.
  QNetworkReply* authorizedSend(const QByteArray& method, const QString& apiPath, const QByteArray& body,
                                const HttpHeaders& headers = {}, const QByteArray& contentType = "application/octet-stream");
  // Same with a streamed body (device stays open until the reply finishes; the caller parents it to the reply).
  QNetworkReply* authorizedSendStream(const QByteArray& method, const QString& apiPath, QIODevice* body,
                                      const HttpHeaders& headers = {}, const QByteArray& contentType = "application/octet-stream");
  // Caller reports 401: the token is renewed immediately, or -> NeedsPairing if the credential is invalid.
  void noteUnauthorized();

  // Upgrade request for the WSS endpoint (apiPath relative to api_base, e.g. "/ws"): wss/ws URL of the hub, Bearer
  // header and the pinned TLS configuration (no system CAs). Default-constructed (empty URL) if not Connected.
  // The token stays inside the request. HubSocket additionally verifies the leaf against pinnedFingerprint().
  QNetworkRequest webSocketRequest(const QString& apiRelPath) const;
  QString pinnedFingerprint() const { return pin_; }

 signals:
  void stateChanged(framebeam::HubConnection::State state);
  void errorOccurred(const QString& code, const QString& message);

 private:
  using Handler = std::function<void(const HttpResult&)>;

  void reset();
  void setState(State s);
  void fail(State s, const QString& code, const QString& message);
  void startIdentify(const QString& addressInput, bool allowHttp, std::optional<HubProfile> existing);
  void track(QNetworkReply* reply, Handler handler);
  QString apiPath(const QString& rel) const;

  void onIdentified(const HttpResult& r);
  void trusted();
  void authenticate();
  void onTokenResult(const HttpResult& r, bool initial);
  void doHandshake();
  void onCredentialInvalid(const QString& code, const QString& message);
  void scheduleRefresh(int expiresInSec);
  void refreshToken();
  void pollPairing();
  void onPairingApproved(const QJsonObject& obj);
  bool handleCommonFailure(const HttpResult& r);
  bool handleUserDisabled(const HttpResult& r);
  void onPairingAccepted(const QJsonObject& o);

  ProfileStore* profiles_;
  CredentialStore* credentials_;
  HandshakeInfo handshake_;
  int pollIntervalMs_ = 2000;

  State state_ = State::Disconnected;
  IncompatibleReason incompatible_ = IncompatibleReason::None;
  QString errorCode_;
  QString errorMessage_;
  QString address_;
  bool allowHttp_ = false;
  QString pin_;
  QString observedFp_;
  HubInfo hubInfo_;
  std::optional<HubProfile> profile_;
  QList<HandshakeProblem> problems_;
  QStringList features_;

  quint64 gen_ = 0;
  HubHttp* http_ = nullptr;
  QSet<QNetworkReply*> inflight_;
  QByteArray accessToken_;
  QTimer refreshTimer_;
  QTimer pollTimer_;
  QString pairingRequestId_;
  QByteArray pollToken_;
  bool pollInFlight_ = false;
  bool refreshInFlight_ = false;
};

}  // namespace framebeam
