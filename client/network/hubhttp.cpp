#include "hubhttp.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>

namespace framebeam {

namespace {
constexpr const char* kMismatchProp = "fb_cert_mismatch";
constexpr const char* kObservedProp = "fb_observed_fp";
constexpr int kTransferTimeoutMs = 30000;
}  // namespace

QJsonObject HttpResult::json() const {
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
  return err.error == QJsonParseError::NoError ? doc.object() : QJsonObject();
}

HubHttp::HubHttp(const QUrl& baseUrl, const QString& pinnedFingerprint, QObject* parent)
    : QObject(parent), base_(baseUrl), pin_(pinnedFingerprint), nam_(this) {}

QString HubHttp::fingerprint(const QSslCertificate& cert) {
  if (cert.isNull()) {
    return {};
  }
  const QByteArray digest = QCryptographicHash::hash(cert.toDer(), QCryptographicHash::Sha256);
  return QString::fromLatin1(digest.toHex(':').toUpper());
}

bool HubHttp::fingerprintsEqual(const QString& a, const QString& b) {
  return !a.isEmpty() && a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

bool HubHttp::isLoopbackHost(const QString& host) {
  if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  QHostAddress addr;
  return addr.setAddress(host) && addr.isLoopback();
}

bool HubHttp::isSchemeAllowed(const QUrl& url, bool allowHttp) {
  const QString scheme = url.scheme().toLower();
  if (scheme == QLatin1String("https")) {
    return true;
  }
  return scheme == QLatin1String("http") && (allowHttp || isLoopbackHost(url.host()));
}

void HubHttp::setPinnedFingerprint(const QString& fp) {
  pin_ = fp;
  nam_.clearConnectionCache();
}

QNetworkRequest HubHttp::makeRequest(const QString& path, const QByteArray& bearer, const HttpHeaders& headers) const {
  QUrl url = base_;
  url.setPath(path);
  QNetworkRequest req(url);
  req.setTransferTimeout(kTransferTimeoutMs);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  req.setRawHeader("Accept", "application/json, application/octet-stream");
  if (!bearer.isEmpty()) {
    req.setRawHeader("Authorization", "Bearer " + bearer);
  }
  for (const auto& h : headers) {
    req.setRawHeader(h.first, h.second);
  }
  if (url.scheme().toLower() == QLatin1String("https")) {
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    cfg.setCaCertificates({});  // no system CAs: only the pin decides
    cfg.setPeerVerifyMode(QSslSocket::VerifyPeer);
    cfg.setProtocol(QSsl::TlsV1_2OrLater);
    req.setSslConfiguration(cfg);
  }
  return req;
}

void HubHttp::attach(QNetworkReply* reply, bool allowUnpinned) {
  connect(reply, &QNetworkReply::sslErrors, this, [this, reply, allowUnpinned](const QList<QSslError>& errors) {
    QSslCertificate leaf = reply->sslConfiguration().peerCertificate();
    if (leaf.isNull() && !errors.isEmpty()) {
      leaf = errors.first().certificate();
    }
    const QString fp = fingerprint(leaf);
    reply->setProperty(kObservedProp, fp);
    const bool pinMatches = !pin_.isEmpty() && fingerprintsEqual(fp, pin_);
    const bool firstContact = pin_.isEmpty() && allowUnpinned && !fp.isEmpty();
    if (pinMatches || firstContact) {
      reply->ignoreSslErrors();
    } else {
      // Do not ignore errors: the handshake fails before any request data is sent.
      reply->setProperty(kMismatchProp, true);
    }
  });
}

QNetworkReply* HubHttp::get(const QString& path, const QByteArray& bearer, const HttpHeaders& headers,
                            bool allowUnpinned) {
  QNetworkReply* reply = nam_.get(makeRequest(path, bearer, headers));
  attach(reply, allowUnpinned);
  return reply;
}

QNetworkReply* HubHttp::postJson(const QString& path, const QJsonObject& body, const QByteArray& bearer) {
  QNetworkRequest req = makeRequest(path, bearer, {});
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  QNetworkReply* reply = nam_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
  attach(reply, false);
  return reply;
}

HttpResult HubHttp::resultOf(QNetworkReply* reply) {
  HttpResult r;
  r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  r.body = reply->readAll();
  r.networkError = r.status == 0;
  r.errorString = reply->errorString();
  r.certMismatch = reply->property(kMismatchProp).toBool();
  r.observedFingerprint = reply->property(kObservedProp).toString();
  if (r.observedFingerprint.isEmpty()) {
    r.observedFingerprint = fingerprint(reply->sslConfiguration().peerCertificate());
  }
  if (!r.networkError && !r.body.isEmpty() && r.body.size() < 65536) {
    const QJsonObject err = r.json().value(QStringLiteral("error")).toObject();
    r.apiErrorCode = err.value(QStringLiteral("code")).toString();
    r.apiErrorMessage = err.value(QStringLiteral("message")).toString();
  }
  return r;
}

}  // namespace framebeam
