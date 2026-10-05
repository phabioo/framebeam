#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPair>
#include <QSslCertificate>
#include <QString>
#include <QUrl>

namespace framebeam {

// Result of a finished request (read only in the finished slot with resultOf()).
struct HttpResult {
  int status = 0;  // 0 = no HTTP response
  QByteArray body;
  bool networkError = false;  // no HTTP response received
  QString errorString;
  bool certMismatch = false;  // certificate does not match the pin (request was never sent)
  QString observedFingerprint;
  QString apiErrorCode;  // from {"error":{"code","message"}}
  QString apiErrorMessage;

  QJsonObject json() const;
  bool ok() const { return !networkError && status >= 200 && status < 300; }
};

using HttpHeaders = QList<QPair<QByteArray, QByteArray>>;

// HTTP(S) client with certificate pinning. System CAs are deliberately NOT used: every
// certificate (even one trusted by the system, e.g. a reverse proxy) is accepted only via the leaf fingerprint.
// Without a pin, a certificate is accepted only if allowUnpinned is set (first contact, info endpoint only).
class HubHttp : public QObject {
  Q_OBJECT
 public:
  HubHttp(const QUrl& baseUrl, const QString& pinnedFingerprint, QObject* parent = nullptr);

  // SHA-256 over leaf DER, uppercase hex with colons (like server/internal/tlsutil Fingerprint).
  static QString fingerprint(const QSslCertificate& cert);
  static bool fingerprintsEqual(const QString& a, const QString& b);
  static bool isLoopbackHost(const QString& host);
  // https always; http only for localhost/127.0.0.1/::1 or allowHttp (dev flag).
  static bool isSchemeAllowed(const QUrl& url, bool allowHttp);
  static HttpResult resultOf(QNetworkReply* reply);

  void setPinnedFingerprint(const QString& fp);
  QString pinnedFingerprint() const { return pin_; }
  const QUrl& baseUrl() const { return base_; }

  QNetworkReply* get(const QString& path, const QByteArray& bearer = {}, const HttpHeaders& headers = {},
                     bool allowUnpinned = false);
  QNetworkReply* postJson(const QString& path, const QJsonObject& body, const QByteArray& bearer = {});
  // Arbitrary method with a raw body (e.g. PUT of a save file).
  QNetworkReply* send(const QByteArray& method, const QString& path, const QByteArray& body, const QByteArray& bearer,
                      const HttpHeaders& headers = {}, const QByteArray& contentType = "application/octet-stream");

 private:
  QNetworkRequest makeRequest(const QString& path, const QByteArray& bearer, const HttpHeaders& headers) const;
  void attach(QNetworkReply* reply, bool allowUnpinned);

  QUrl base_;
  QString pin_;
  QNetworkAccessManager nam_;
};

}  // namespace framebeam
