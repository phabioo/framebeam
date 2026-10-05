#include "fakehub.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSslConfiguration>
#include <QSslSocket>
#include <memory>

#include "hubhttp.h"

const QByteArray FakeHub::kDeviceCredential = "fbd_TEST_DUMMY_CREDENTIAL";
const QByteArray FakeHub::kPollToken = "fbp_TEST_DUMMY_POLL";

FakeHub::FakeHub(const QString& certName, QObject* parent) : QTcpServer(parent), certName_(certName) {
  games = QJsonObject{{QStringLiteral("games"), QJsonArray()}};
  if (!certName.isEmpty()) {
    const QString dir = QStringLiteral(FB_TEST_DATA_DIR);
    QFile c(dir + QStringLiteral("/test-cert-") + certName + QStringLiteral(".pem"));
    QFile k(dir + QStringLiteral("/test-key-") + certName + QStringLiteral(".pem"));
    if (c.open(QIODevice::ReadOnly) && k.open(QIODevice::ReadOnly)) {
      cert_ = QSslCertificate(&c, QSsl::Pem);
      key_ = QSslKey(&k, QSsl::Ec, QSsl::Pem);
    }
  }
}

bool FakeHub::start() {
  if (!certName_.isEmpty() && (cert_.isNull() || key_.isNull())) {
    return false;
  }
  return listen(QHostAddress::LocalHost, 0);
}

QString FakeHub::address() const {
  return QStringLiteral("%1://127.0.0.1:%2").arg(certName_.isEmpty() ? QStringLiteral("http") : QStringLiteral("https")).arg(serverPort());
}

QString FakeHub::fingerprint() const { return framebeam::HubHttp::fingerprint(cert_); }

int FakeHub::count(const QString& pathPrefix) const {
  int n = 0;
  for (const FakeRequest& r : requests) {
    if (r.path.startsWith(pathPrefix)) {
      ++n;
    }
  }
  return n;
}

void FakeHub::incomingConnection(qintptr descriptor) {
  auto* sock = new QSslSocket(this);
  if (!sock->setSocketDescriptor(descriptor)) {
    delete sock;
    return;
  }
  connect(sock, &QSslSocket::disconnected, sock, &QObject::deleteLater);
  auto buffer = std::make_shared<QByteArray>();
  connect(sock, &QSslSocket::readyRead, this, [this, sock, buffer]() {
    buffer->append(sock->readAll());
    const int headEnd = buffer->indexOf("\r\n\r\n");
    if (headEnd < 0) {
      return;
    }
    const QList<QByteArray> lines = buffer->left(headEnd).split('\n');
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2) {
      sock->disconnectFromHost();
      return;
    }
    FakeRequest req;
    req.method = first.at(0);
    req.path = QString::fromLatin1(first.at(1));
    for (qsizetype i = 1; i < lines.size(); ++i) {
      const int colon = lines.at(i).indexOf(':');
      if (colon > 0) {
        req.headers.insert(QString::fromLatin1(lines.at(i).left(colon).trimmed().toLower()), lines.at(i).mid(colon + 1).trimmed());
      }
    }
    const qint64 len = req.headers.value(QStringLiteral("content-length"), "0").toLongLong();
    if (buffer->size() - (headEnd + 4) < len) {
      return;
    }
    req.body = buffer->mid(headEnd + 4, static_cast<qsizetype>(len));
    buffer->clear();
    requests.append(req);
    handle(sock, req);
  });
  if (!certName_.isEmpty()) {
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    cfg.setLocalCertificate(cert_);
    cfg.setPrivateKey(key_);
    cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
    sock->setSslConfiguration(cfg);
    sock->startServerEncryption();
  }
}

bool FakeHub::bearerIs(const FakeRequest& req, const QByteArray& prefix) const {
  return req.headers.value(QStringLiteral("authorization")).startsWith("Bearer " + prefix);
}

void FakeHub::respond(QSslSocket* sock, int status, const QByteArray& body, const QByteArray& contentType,
                      const QList<QPair<QByteArray, QByteArray>>& extra, qint64 truncateAt) {
  QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: " + QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n";
  if (!body.isEmpty()) {
    head += "Content-Type: " + contentType + "\r\n";
  }
  for (const auto& h : extra) {
    head += h.first + ": " + h.second + "\r\n";
  }
  head += "\r\n";
  sock->write(head);
  sock->write(truncateAt >= 0 ? body.left(static_cast<qsizetype>(truncateAt)) : body);
  sock->disconnectFromHost();
}

void FakeHub::respondError(QSslSocket* sock, int status, const QString& code) {
  respond(sock, status,
          QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), code}}}})
              .toJson(QJsonDocument::Compact));
}

void FakeHub::handle(QSslSocket* sock, const FakeRequest& req) {
  const auto json = [](const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
  const QJsonObject body = QJsonDocument::fromJson(req.body).object();

  if (req.method == "GET" && req.path == QLatin1String("/.well-known/framebeam")) {
    respond(sock, 200, json({{QStringLiteral("hub_id"), hubId},
                             {QStringLiteral("name"), name},
                             {QStringLiteral("hub_version"), QStringLiteral("0.1.0")},
                             {QStringLiteral("protocol_version"), protocolVersion},
                             {QStringLiteral("min_protocol_version"), minProtocolVersion},
                             {QStringLiteral("api_base"), QStringLiteral("/api/v1")}}));
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/pairing/requests")) {
    if (rateLimitPairing) {
      respondError(sock, 429, QStringLiteral("rate_limited"));
      return;
    }
    approvedDelivered_ = false;
    respond(sock, 202, json({{QStringLiteral("request_id"), QStringLiteral("req-1")},
                             {QStringLiteral("poll_token"), QString::fromLatin1(kPollToken)},
                             {QStringLiteral("status"), QStringLiteral("pending")},
                             {QStringLiteral("expires_in"), 600}}));
  } else if (req.method == "GET" && req.path == QLatin1String("/api/v1/pairing/requests/req-1")) {
    if (!bearerIs(req, kPollToken)) {
      respondError(sock, 401, QStringLiteral("unauthorized"));
      return;
    }
    switch (decision) {
      case Decision::Pending: respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("pending")}})); break;
      case Decision::Deny: respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("denied")}})); break;
      case Decision::Expire: respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("expired")}})); break;
      case Decision::Approve:
        if (approvedDelivered_) {
          respondError(sock, 404, QStringLiteral("not_found"));
        } else {
          approvedDelivered_ = true;  // Credential genau einmal
          respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("approved")},
                                   {QStringLiteral("hub_id"), hubId},
                                   {QStringLiteral("user_id"), QStringLiteral("u_test_1")},
                                   {QStringLiteral("device_credential"), QString::fromLatin1(kDeviceCredential)}}));
        }
        break;
    }
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/auth/token")) {
    ++tokenRequests_;
    if (revoked) {
      respondError(sock, 401, QStringLiteral("device_revoked"));
    } else if (body.value(QStringLiteral("device_credential")).toString().toUtf8() != kDeviceCredential) {
      respondError(sock, 401, QStringLiteral("invalid_credentials"));
    } else {
      const QByteArray token = "fba_TEST_" + QByteArray::number(tokenRequests_);
      issuedAccessTokens.append(token);
      respond(sock, 200, json({{QStringLiteral("access_token"), QString::fromLatin1(token)},
                               {QStringLiteral("token_type"), QStringLiteral("Bearer")},
                               {QStringLiteral("expires_in"), tokenLifetime}}));
    }
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/auth/revoke")) {
    if (!bearerIs(req, "fba_")) {
      respondError(sock, 401, QStringLiteral("unauthorized"));
      return;
    }
    revoked = true;
    respond(sock, 204, {});
  } else if (!bearerIs(req, "fba_")) {
    respondError(sock, 401, QStringLiteral("unauthorized"));
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/handshake")) {
    QJsonObject res{{QStringLiteral("hub_version"), QStringLiteral("0.1.0")},
                    {QStringLiteral("protocol_version"), protocolVersion},
                    {QStringLiteral("min_protocol_version"), minProtocolVersion},
                    {QStringLiteral("compatible"), true},
                    {QStringLiteral("problems"), QJsonArray()}};
    for (auto it = handshakeExtra.begin(); it != handshakeExtra.end(); ++it) {
      res.insert(it.key(), it.value());
    }
    respond(sock, 200, json(res));
  } else if (req.method == "GET" && req.path == QLatin1String("/api/v1/games")) {
    respond(sock, 200, json(games));
  } else if (req.method == "GET" && req.path.startsWith(QLatin1String("/api/v1/roms/"))) {
    const QByteArray data = roms.value(req.path.mid(13));
    if (data.isEmpty()) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    const QByteArray range = req.headers.value(QStringLiteral("range"));
    if (!ignoreRange && range.startsWith("bytes=") && range.endsWith("-")) {
      const qint64 from = range.mid(6, range.size() - 7).toLongLong();
      if (from >= data.size()) {
        respondError(sock, 416, QStringLiteral("bad_request"));
        return;
      }
      respond(sock, 206, data.mid(static_cast<qsizetype>(from)), "application/octet-stream",
              {{"Content-Range", "bytes " + QByteArray::number(from) + "-" + QByteArray::number(data.size() - 1) + "/" +
                                     QByteArray::number(data.size())}});
      return;
    }
    qint64 cut = -1;
    if (truncateFirstRomAt >= 0 && !truncated_) {
      truncated_ = true;
      cut = truncateFirstRomAt;
    }
    respond(sock, 200, data, "application/octet-stream", {}, cut);
  } else {
    respondError(sock, 404, QStringLiteral("not_found"));
  }
}
