#pragma once

// In-process fake FrameBeam Hub for tests (HTTPS with a test certificate from tests/testdata, or HTTP).
// Test code only; tokens/credentials are dummy values.

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>

class QSslSocket;

// Fake save slot of the Hub (D3 of the phase 3 spec): current checkpoint + conflicts. Test code only.
struct FakeConflict {
  QString id;
  QString status = QStringLiteral("open");  // open | resolved_hub | resolved_local
  int hubRevision = 0;
  QString hubSha;
  QString hubDeviceId, hubDeviceName;
  QByteArray securedContent;
  int securedVersion = 0;
  int securedBase = 0;
  QString securedDeviceId, securedDeviceName;
};
struct FakeSlot {
  int revision = 0;
  QByteArray content;
  QString deviceId, deviceName, reason = QStringLiteral("checkpoint");
  int nextVersion = 1;
  QList<FakeConflict> conflicts;
};

struct FakeRequest {
  QByteArray method;
  QString path;
  QHash<QString, QByteArray> headers;  // names lowercase
  QByteArray body;
};

class FakeHub : public QTcpServer {
  Q_OBJECT
 public:
  enum class Decision { Pending, Approve, Deny, Expire };

  // certName: "a" or "b" (tests/testdata/test-cert-<x>.pem); empty = HTTP without TLS.
  explicit FakeHub(const QString& certName, QObject* parent = nullptr);

  bool start();
  QString address() const;      // e.g. https://127.0.0.1:PORT
  QString fingerprint() const;  // same format as hub settings; empty for HTTP
  bool setCertificate(const QString& certName);  // "a"|"b"|"c": new connections use this certificate (renewal)

  // Configuration
  QString hubId = QStringLiteral("hub-test-1");
  QString name = QStringLiteral("Test Hub");
  int protocolVersion = 1;
  int minProtocolVersion = 1;
  Decision decision = Decision::Pending;
  bool rateLimitPairing = false;
  bool revoked = false;
  int tokenLifetime = 900;
  QJsonObject handshakeExtra;      // merged into the handshake response (e.g. problems)
  QJsonObject games;               // {"games":[...]} (raw response)
  QMap<QString, QByteArray> roms;  // sha256 -> content
  qint64 truncateFirstRomAt = -1;  // first ROM response aborts after this many body bytes
  bool ignoreRange = false;        // always responds 200 with the full content

  // Phase 5: invites (users_v1), uploads (uploads_v1), systems/firmware (firmware_v1)
  QString inviteCode = QStringLiteral("FB-TEST-CODE");  // the only valid code (normalized to upper case)
  bool inviteDirect = true;                              // true: 200 approved; false: 202 pending (poll like pairing)
  QStringList takenNames;                                // display names -> 409 display_name_taken (case-insensitive)
  bool rateLimitInvites = false;
  QJsonObject lastInviteBody;
  QJsonObject lastHandshakeBody;  // request body of the last POST /handshake
  bool userDisabled = false;                             // token endpoint answers 401 user_disabled
  bool uploadsAllowed = true;                            // false: POST /games -> 403 uploads_disabled
  struct Upload {
    QString filename, title, sha256;
    qint64 size = 0;
  };
  QList<Upload> uploads;
  QJsonObject systems;                                   // {"systems":[...]} (raw response of GET /systems)
  QMap<QString, QByteArray> firmwareFiles;               // "<system>/<file_id>" -> bytes
  int firmwareDownloads = 0;
  int count(const QString& pathPrefix, const QByteArray& method) const;

  // Saves (saves_v1)
  QStringList features{QStringLiteral("saves_v1")};  // handshake features
  QMap<QString, FakeSlot> saves;                     // game_id -> slot "default"
  int failSaveRequests = 0;                          // next N save requests answer 503
  QString callerDeviceId;                            // device_id of the last token request
  // Simulates another device that uploaded a new checkpoint.
  void setHubSave(const QString& gameId, const QByteArray& content, const QString& deviceId = QStringLiteral("other-device"),
                  const QString& deviceName = QStringLiteral("Laptop Office"));

  // Sessions (sessions_v1): minimal in-memory REST + WSS (own WebSocket implementation on the same TLS port)
  QMap<QString, QJsonObject> sessions;  // session_id -> Session JSON (as REST)
  QJsonArray fakeUsers;                 // GET /users (empty: only the test user)
  bool publishCapabilityMissing = false;
  bool joinFull = false;
  bool refuseWs = false;                  // WSS upgrade answered with 401
  QList<QJsonObject> wsReceived;          // envelopes received from the Player (all connections)
  QByteArray lastWsAuthorization;
  int wsConnections = 0;                  // upgrades accepted so far
  int wsOpen() const { return static_cast<int>(wsClients_.size()); }
  void sendWs(const QString& type, const QJsonObject& payload);  // to every open WSS connection
  void closeWsClients();                                         // drops the connections (reconnect tests)

  // Observation
  QList<FakeRequest> requests;
  int count(const QString& pathPrefix) const;
  int tokenRequests() const { return tokenRequests_; }
  QList<QByteArray> issuedAccessTokens;

  static const QByteArray kDeviceCredential;
  static const QByteArray kPollToken;

 protected:
  void incomingConnection(qintptr handle) override;

 private:
  void handle(QSslSocket* sock, const FakeRequest& req);
  void respond(QSslSocket* sock, int status, const QByteArray& body, const QByteArray& contentType = "application/json",
               const QList<QPair<QByteArray, QByteArray>>& extra = {}, qint64 truncateAt = -1);
  void respondError(QSslSocket* sock, int status, const QString& code);
  void handleSaves(QSslSocket* sock, const FakeRequest& req);
  void handleSessions(QSslSocket* sock, const FakeRequest& req);
  void upgradeWs(QSslSocket* sock, const FakeRequest& req);
  void onWsData(QSslSocket* sock);
  void wsWrite(QSslSocket* sock, quint8 opcode, const QByteArray& payload);
  QList<QSslSocket*> wsClients_;
  QHash<QSslSocket*, QByteArray> wsBuffers_;
  QJsonObject slotJson(const QString& gameId, const FakeSlot& s) const;
  QJsonObject conflictJson(const QString& gameId, const FakeSlot& s, const FakeConflict& c) const;
  bool bearerIs(const FakeRequest& req, const QByteArray& prefix) const;

  QString certName_;
  QSslCertificate cert_;
  QSslKey key_;
  bool approvedDelivered_ = false;
  int tokenRequests_ = 0;
  bool truncated_ = false;
};
