#pragma once

// In-Process-Fake-FrameBeam-Hub fuer Tests (HTTPS mit Test-Zertifikat aus tests/testdata, oder HTTP).
// Nur Testcode; Tokens/Credentials sind Dummy-Werte.

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>

class QSslSocket;

struct FakeRequest {
  QByteArray method;
  QString path;
  QHash<QString, QByteArray> headers;  // Namen klein
  QByteArray body;
};

class FakeHub : public QTcpServer {
  Q_OBJECT
 public:
  enum class Decision { Pending, Approve, Deny, Expire };

  // certName: "a" oder "b" (tests/testdata/test-cert-<x>.pem); leer = HTTP ohne TLS.
  explicit FakeHub(const QString& certName, QObject* parent = nullptr);

  bool start();
  QString address() const;      // z. B. https://127.0.0.1:PORT
  QString fingerprint() const;  // Format wie Hub-Settings; leer bei HTTP

  // Konfiguration
  QString hubId = QStringLiteral("hub-test-1");
  QString name = QStringLiteral("Test-Hub");
  int protocolVersion = 1;
  int minProtocolVersion = 1;
  Decision decision = Decision::Pending;
  bool rateLimitPairing = false;
  bool revoked = false;
  int tokenLifetime = 900;
  QJsonObject handshakeExtra;      // wird in die Handshake-Antwort gemischt (z. B. problems)
  QJsonObject games;               // {"games":[...]} (Rohantwort)
  QMap<QString, QByteArray> roms;  // sha256 -> Inhalt
  qint64 truncateFirstRomAt = -1;  // erste ROM-Antwort bricht nach so vielen Body-Bytes ab
  bool ignoreRange = false;        // antwortet immer 200 mit vollem Inhalt

  // Beobachtung
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
  bool bearerIs(const FakeRequest& req, const QByteArray& prefix) const;

  QString certName_;
  QSslCertificate cert_;
  QSslKey key_;
  bool approvedDelivered_ = false;
  int tokenRequests_ = 0;
  bool truncated_ = false;
};
