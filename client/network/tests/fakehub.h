#pragma once

// In-process fake FrameBeam Hub for tests (HTTPS with a test certificate from tests/testdata, or HTTP).
// Test code only; tokens/credentials are dummy values.

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSslCertificate>
#include <QSslKey>
#include <QPointer>
#include <QTcpServer>

class QSslSocket;

// Fake save slot of the Hub: current checkpoint + conflicts. Test code only.
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
// History version of a slot (saves_v2: labels, before_restore, manual_snapshot).
struct FakeVersion {
  int version = 0;
  int revision = 0;
  QByteArray content;
  QString deviceId, deviceName, reason = QStringLiteral("session_end");
  QString label;
};
struct FakeSlot {
  QList<FakeVersion> history;  // oldest first
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
  qint64 arrivalMs = 0;  // monotonic ms (QElapsedTimer clock) when the request was complete
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

  // Invites (users_v1), uploads (uploads_v1), systems/firmware (firmware_v1)
  QString inviteCode = QStringLiteral("FB-TEST-CODE");  // the only valid code (normalized to upper case)
  bool inviteDirect = true;                              // true: 200 approved; false: 202 pending (poll like pairing)
  QStringList takenNames;                                // display names -> 409 display_name_taken (case-insensitive)
  bool rateLimitInvites = false;
  QJsonObject lastInviteBody;
  // Local setup (local_setup_v1): /local/status, /local/setup, /local/pair, PUT /local/settings
  bool localAdminExists = false;
  bool localNetworkSharing = false;
  QString localImportDir;
  QJsonObject lastLocalBody;       // body of the last /local/setup or /local/pair
  QJsonObject lastLocalSettings;   // body of the last PUT /local/settings
  QString localImportDirError;     // non-empty: PUT import_dir answers 400 import_dir_unreadable with this message
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
  // Core packages (cores_v1): "<core>/<version>/<platform>" -> CorePackage JSON as served; files "<core>/<version>/<platform>/<name>"
  // -> bytes (a missing entry answers 404 core_file_not_available).
  QMap<QString, QJsonObject> corePackages;
  QMap<QString, QByteArray> coreFiles;
  int coreFileDownloads = 0;
  // Plugins (plugins_v1): GET /plugins answers {"plugins": pluginList}; files "<id>/<version>" -> bytes (missing: 404).
  QJsonArray pluginList;
  QMap<QString, QByteArray> pluginFiles;
  int pluginFileDownloads = 0;
  int pluginListRequests = 0;
  bool pluginListFails = false;  // GET /plugins answers 500
  int corePackageRequests = 0;
  int count(const QString& pathPrefix, const QByteArray& method) const;

  // Local ROMs (local_roms_v1): POST /games/register, DELETE /games/{id}/registration, resumable /uploads.
  struct ResumableUpload {
    QString id, filename, title, system, sha256;
    qint64 size = 0;
    QByteArray data;  // received so far
  };
  QList<QJsonObject> registerBodies;           // bodies of POST /games/register
  QStringList unregisteredGames;               // game ids of DELETE /games/{id}/registration
  QMap<QString, ResumableUpload> uploadSessions;
  QStringList putRanges;                       // Content-Range headers of every PUT, in order
  qint64 uploadChunkSize = 1024;               // chunk_size announced to the Player
  int failPutNumber = -1;                      // the Nth (1-based) chunk PUT answers 503 and is not stored
  int uploadSessionsCreated = 0;

  // Saves (saves_v1)
  QStringList features{QStringLiteral("saves_v1")};  // handshake features
  // game_id -> slot "default"; other slots under the key "<game_id>/<slot>" (slotKey()).
  QMap<QString, FakeSlot> saves;
  static QString slotKey(const QString& gameId, const QString& slot) {
    return slot == QLatin1String("default") ? gameId : gameId + QLatin1Char('/') + slot;
  }
  // Adds a history version to a slot (creates the slot object if needed; revision of the slot is not changed).
  void addHistory(const QString& gameId, const QString& slot, const QByteArray& content, const QString& reason,
                  const QString& label = QString(), const QString& deviceName = QStringLiteral("Laptop Office"));
  int uploadCount = 0;   // save file uploads that succeeded
  qint64 uploadLimit = 64LL * 1024 * 1024;  // 413 above this
  int deleteCount = 0;   // snapshot deletions that succeeded
  int restoreCount = 0;  // restore requests that succeeded
  int renameCount = 0;       // slot renames that succeeded (saves_v5)
  int slotDeleteCount = 0;   // slot deletions that succeeded (saves_v5)
  QStringList sessionRunningGames;  // game ids with a running Session: upload / rename / delete -> 409 session_running
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
  QJsonArray helloTurnServers;            // `turn_servers` of hello_ack (omitted when empty)
  QJsonArray joinTurnServers;             // `turn_servers` of the join response (omitted when empty)
  bool refuseWs = false;                  // WSS upgrade answered with 401
  QList<QJsonObject> wsReceived;          // envelopes received from the Player (all connections)
  QByteArray lastWsAuthorization;
  int wsConnections = 0;                  // upgrades accepted so far
  int wsOpen() const { return static_cast<int>(wsClients_.size()); }
  void sendWs(const QString& type, const QJsonObject& payload);  // to every open WSS connection
  // GET /sessions/<id> answers are held (body of that moment, visibility optionally overridden) until released:
  // models a request served before a later PATCH whose answer arrives after the PATCH answer.
  bool holdSessionGet = false;
  int heldSessionGets() const { return static_cast<int>(heldGets_.size()); }
  void releaseHeldSessionGets(const QString& visibilityOverride = {});
  void closeWsClients();                                         // drops the connections (reconnect tests)

  // Runs (runs_v1): in-memory REST + WSS model. Tests seed `runs` (Run JSON as served) and `runEvents`, or let the
  // routes create them. The caller is `runsMe`.
  QString runsMe = QStringLiteral("u_test_1");
  QString runsMeName = QStringLiteral("Tester");
  QMap<QString, QJsonObject> runs;               // run_id -> Run JSON
  QMap<QString, QList<QJsonObject>> runEvents;   // run_id -> events, ascending, seq from 1
  QMap<QString, QJsonObject> runBoards;          // "<run_id>/<plugin_id>" -> tree
  int runStateConflicts = 0;                     // next N state PUTs answer 409 state_conflict
  qint64 runConflictRev = 7;                     // the state_rev such a 409 reports
  int runEventDropResponses = 0;                 // next N event POSTs are stored but the connection is dropped unanswered
  int runFailNext = 0;                           // next N run requests answer runFailStatus
  int runFailStatus = 503;
  int runHelloCatchUpMax = 500;                  // more missing events than this: none are sent after hello
  QJsonObject lastRunBody;                       // body of the last run POST/PUT
  QList<QJsonObject> runStatePuts;               // bodies of the state PUTs (accepted or answered 409)
  QList<QJsonObject> runEventPosts;              // bodies of every event POST (also retried ones)
  int runBoardPuts = 0;
  static QString makeUuid();
  // Appends an event (Hub event when userId is empty) and pushes run_event to the WSS clients; returns the seq.
  qint64 addRunEvent(const QString& runId, const QString& type, const QString& text, const QString& userId = {},
                     const QString& pluginId = {}, const QString& clientId = {}, bool push = true);
  void pushRun(const QString& runId);            // run_updated to the WSS clients
  QJsonObject makeRun(const QString& name, const QStringList& userIds, const QString& creator, const QJsonArray& plugins = {});

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
  struct HeldGet {
    QPointer<QSslSocket> sock;
    QJsonObject session;
  };
  QList<HeldGet> heldGets_;
  void handle(QSslSocket* sock, const FakeRequest& req);
  void respond(QSslSocket* sock, int status, const QByteArray& body, const QByteArray& contentType = "application/json",
               const QList<QPair<QByteArray, QByteArray>>& extra = {}, qint64 truncateAt = -1);
  void respondError(QSslSocket* sock, int status, const QString& code);
  void handleSaves(QSslSocket* sock, const FakeRequest& req);
  bool handleLocalRoms(QSslSocket* sock, const FakeRequest& req);
  void handleSessions(QSslSocket* sock, const FakeRequest& req);
  void handleRuns(QSslSocket* sock, const FakeRequest& req);
  void sendRunCatchUp(QSslSocket* sock, const QJsonObject& hello);
  void upgradeWs(QSslSocket* sock, const FakeRequest& req);
  void onWsData(QSslSocket* sock);
  void wsWrite(QSslSocket* sock, quint8 opcode, const QByteArray& payload);
  QList<QSslSocket*> wsClients_;
  QHash<QSslSocket*, QByteArray> wsBuffers_;
  QJsonObject slotJson(const QString& key, const FakeSlot& s) const;  // key: slotKey()
  QJsonObject conflictJson(const QString& key, const FakeSlot& s, const FakeConflict& c) const;
  QJsonObject versionJson(const FakeVersion& v) const;
  QJsonArray wsFeatures() const {
    QJsonArray a;
    for (const QString& f : features) {
      a.append(f);
    }
    if (!features.contains(QStringLiteral("sessions_v1"))) {
      a.append(QStringLiteral("sessions_v1"));
    }
    return a;
  }
  bool bearerIs(const FakeRequest& req, const QByteArray& prefix) const;

  QString certName_;
  QSslCertificate cert_;
  QSslKey key_;
  bool approvedDelivered_ = false;
  int tokenRequests_ = 0;
  bool truncated_ = false;
};
