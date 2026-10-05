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

// Ergebnis einer beendeten Anfrage (nur im finished-Slot mit resultOf() lesen).
struct HttpResult {
  int status = 0;  // 0 = keine HTTP-Antwort
  QByteArray body;
  bool networkError = false;  // keine HTTP-Antwort erhalten
  QString errorString;
  bool certMismatch = false;  // Zertifikat entspricht nicht dem Pin (Anfrage wurde nie gesendet)
  QString observedFingerprint;
  QString apiErrorCode;  // aus {"error":{"code","message"}}
  QString apiErrorMessage;

  QJsonObject json() const;
  bool ok() const { return !networkError && status >= 200 && status < 300; }
};

using HttpHeaders = QList<QPair<QByteArray, QByteArray>>;

// HTTP(S)-Client mit Certificate Pinning. Die System-CAs werden bewusst NICHT verwendet: jedes
// Zertifikat (auch ein vom System vertrautes, z. B. Reverse Proxy) wird nur ueber den Leaf-Fingerprint
// akzeptiert. Ohne Pin wird nur akzeptiert, wenn allowUnpinned gesetzt ist (Erstkontakt, nur Info-Endpunkt).
class HubHttp : public QObject {
  Q_OBJECT
 public:
  HubHttp(const QUrl& baseUrl, const QString& pinnedFingerprint, QObject* parent = nullptr);

  // SHA-256 ueber Leaf-DER, Hex gross mit Doppelpunkten (wie server/internal/tlsutil Fingerprint).
  static QString fingerprint(const QSslCertificate& cert);
  static bool fingerprintsEqual(const QString& a, const QString& b);
  static bool isLoopbackHost(const QString& host);
  // https immer; http nur bei localhost/127.0.0.1/::1 oder allowHttp (Dev-Flag).
  static bool isSchemeAllowed(const QUrl& url, bool allowHttp);
  static HttpResult resultOf(QNetworkReply* reply);

  void setPinnedFingerprint(const QString& fp);
  QString pinnedFingerprint() const { return pin_; }
  const QUrl& baseUrl() const { return base_; }

  QNetworkReply* get(const QString& path, const QByteArray& bearer = {}, const HttpHeaders& headers = {},
                     bool allowUnpinned = false);
  QNetworkReply* postJson(const QString& path, const QJsonObject& body, const QByteArray& bearer = {});

 private:
  QNetworkRequest makeRequest(const QString& path, const QByteArray& bearer, const HttpHeaders& headers) const;
  void attach(QNetworkReply* reply, bool allowUnpinned);

  QUrl base_;
  QString pin_;
  QNetworkAccessManager nam_;
};

}  // namespace framebeam
