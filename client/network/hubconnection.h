#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
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

// Verbindungs-Zustandsmaschine zu genau einem Hub (Identifikation, TOFU/Pinning, Approval-Pairing,
// Token, Handshake). Die UI bindet sich an stateChanged()/errorOccurred(); Aktionen sind Slots.
class HubConnection : public QObject {
  Q_OBJECT
 public:
  enum class State {
    Disconnected,
    Identifying,
    NeedsTrustConfirmation,  // Erstkontakt: observedFingerprint() anzeigen, confirmTrust()/rejectTrust()
    CertificateChanged,      // Zertifikat weicht vom Pin ab: blockiert, nie still uebernommen
    Incompatible,            // incompatibleReason()
    Unreachable,             // errorCode()/errorMessage()
    NeedsPairing,            // requestPairing()
    AwaitingApproval,        // Poll laeuft, cancelPairing()
    Denied,
    Expired,
    Authenticating,
    Connected
  };
  Q_ENUM(State)

  enum class IncompatibleReason { None, PlayerTooOld, HubTooOld };
  Q_ENUM(IncompatibleReason)

  HubConnection(ProfileStore* profiles, CredentialStore* credentials, QObject* parent = nullptr);
  ~HubConnection() override;

  static QString stateName(State s);

  // Handshake-Daten (u. a. cores); vor dem Verbinden setzen.
  void setHandshakeInfo(const HandshakeInfo& info) { handshake_ = info; }
  void setPollIntervalMs(int ms) { pollIntervalMs_ = ms; }

  State state() const { return state_; }
  IncompatibleReason incompatibleReason() const { return incompatible_; }
  QString errorCode() const { return errorCode_; }
  QString errorMessage() const { return errorMessage_; }
  QString address() const { return address_; }
  const HubInfo& hubInfo() const { return hubInfo_; }
  std::optional<HubProfile> profile() const { return profile_; }
  QString observedFingerprint() const { return observedFp_; }  // bei NeedsTrustConfirmation/CertificateChanged
  QString expectedFingerprint() const { return pin_; }         // gepinnter Wert (CertificateChanged)
  QList<HandshakeProblem> handshakeProblems() const { return problems_; }

  // Genau ein aktiver Hub: Aufrufe trennen zuerst die alte Verbindung.
  void connectToAddress(const QString& address, bool allowHttp = false);
  void connectToProfile(const QString& hubId);
  void disconnectFromHub();

  void confirmTrust();  // NeedsTrustConfirmation: Fingerprint pinnen und fortfahren
  void rejectTrust();   // -> Disconnected
  void requestPairing();  // NeedsPairing/Denied/Expired -> AwaitingApproval
  void cancelPairing();   // AwaitingApproval -> NeedsPairing (lokal; der Hub-Request verfaellt von selbst)
  void retry();           // Unreachable: erneut identifizieren
  void revokeSelf();      // Connected: Gerät widerruft sich, Credential wird geloescht -> NeedsPairing
  // Entfernt Profil und dessen Credential (nicht den serverseitigen Widerruf).
  void removeProfile(const QString& hubId);

  // Authentifizierte Anfragen relativ zu api_base (z. B. "/games"); nullptr, wenn nicht Connected.
  // Antworten werden beim Trennen abgebrochen. Der Token verlaesst diese Klasse nicht.
  QNetworkReply* authorizedGet(const QString& apiPath, const HttpHeaders& headers = {});
  // Aufrufer meldet 401: Token wird sofort erneuert bzw. bei ungueltigem Credential -> NeedsPairing.
  void noteUnauthorized();

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
