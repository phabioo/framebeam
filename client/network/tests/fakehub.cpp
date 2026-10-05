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
          approvedDelivered_ = true;  // credential exactly once
          respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("approved")},
                                   {QStringLiteral("hub_id"), hubId},
                                   {QStringLiteral("user_id"), QStringLiteral("u_test_1")},
                                   {QStringLiteral("device_credential"), QString::fromLatin1(kDeviceCredential)}}));
        }
        break;
    }
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/auth/token")) {
    ++tokenRequests_;
    callerDeviceId = body.value(QStringLiteral("device_id")).toString();
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
    QJsonArray feats;
    for (const QString& f : features) {
      feats.append(f);
    }
    res.insert(QStringLiteral("features"), feats);
    for (auto it = handshakeExtra.begin(); it != handshakeExtra.end(); ++it) {
      res.insert(it.key(), it.value());
    }
    respond(sock, 200, json(res));
  } else if (req.path == QLatin1String("/api/v1/saves") || req.path.startsWith(QLatin1String("/api/v1/games/"))) {
    handleSaves(sock, req);
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

// ---------------------------------------------------------------- saves

void FakeHub::setHubSave(const QString& gameId, const QByteArray& content, const QString& deviceId, const QString& deviceName) {
  FakeSlot& s = saves[gameId];
  s.revision += 1;
  s.content = content;
  s.deviceId = deviceId;
  s.deviceName = deviceName;
  s.reason = QStringLiteral("checkpoint");
}

QJsonObject FakeHub::slotJson(const QString& gameId, const FakeSlot& s) const {
  QJsonArray conflicts;
  for (const FakeConflict& c : s.conflicts) {
    if (c.status == QLatin1String("open")) {
      conflicts.append(conflictJson(gameId, s, c));
    }
  }
  return {{QStringLiteral("game_id"), gameId},
          {QStringLiteral("slot"), QStringLiteral("default")},
          {QStringLiteral("current"),
           QJsonObject{{QStringLiteral("revision"), s.revision},
                       {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(s.content, QCryptographicHash::Sha256).toHex())},
                       {QStringLiteral("size"), s.content.size()},
                       {QStringLiteral("device_id"), s.deviceId},
                       {QStringLiteral("device_name"), s.deviceName},
                       {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:00:00Z")},
                       {QStringLiteral("reason"), s.reason}}},
          {QStringLiteral("open_conflicts"), conflicts}};
}

QJsonObject FakeHub::conflictJson(const QString& gameId, const FakeSlot&, const FakeConflict& c) const {
  return {{QStringLiteral("id"), c.id},
          {QStringLiteral("game_id"), gameId},
          {QStringLiteral("slot"), QStringLiteral("default")},
          {QStringLiteral("status"), c.status},
          {QStringLiteral("hub"), QJsonObject{{QStringLiteral("revision"), c.hubRevision},
                                              {QStringLiteral("sha256"), c.hubSha},
                                              {QStringLiteral("device_id"), c.hubDeviceId},
                                              {QStringLiteral("device_name"), c.hubDeviceName},
                                              {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:00:00Z")}}},
          {QStringLiteral("secured"),
           QJsonObject{{QStringLiteral("version"), c.securedVersion},
                       {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(c.securedContent, QCryptographicHash::Sha256).toHex())},
                       {QStringLiteral("base_revision"), c.securedBase},
                       {QStringLiteral("device_id"), c.securedDeviceId},
                       {QStringLiteral("device_name"), c.securedDeviceName},
                       {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:05:00Z")}}},
          {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:05:00Z")}};
}

void FakeHub::handleSaves(QSslSocket* sock, const FakeRequest& req) {
  const auto json = [](const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
  if (failSaveRequests > 0) {
    --failSaveRequests;
    respondError(sock, 503, QStringLiteral("internal"));
    return;
  }
  const QString sha256Hdr = QString::fromLatin1(req.headers.value(QStringLiteral("x-framebeam-content-sha256")));
  if (req.path == QLatin1String("/api/v1/saves") && req.method == "GET") {
    QJsonArray arr;
    for (auto it = saves.cbegin(); it != saves.cend(); ++it) {
      QJsonObject o = slotJson(it.key(), it.value());
      o.insert(QStringLiteral("open_conflict_count"), o.value(QStringLiteral("open_conflicts")).toArray().size());
      o.remove(QStringLiteral("open_conflicts"));
      arr.append(o);
    }
    respond(sock, 200, json({{QStringLiteral("saves"), arr}}));
    return;
  }
  // /api/v1/games/{id}/saves/{slot}[/content|/conflicts/{cid}/resolve]
  const QStringList parts = req.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (parts.size() < 6 || parts.at(4) != QLatin1String("saves")) {
    respondError(sock, 404, QStringLiteral("not_found"));
    return;
  }
  const QString gameId = parts.at(3);
  const bool exists = saves.contains(gameId) && saves.value(gameId).revision > 0;
  if (parts.size() == 6 && req.method == "GET") {
    exists ? respond(sock, 200, json(slotJson(gameId, saves.value(gameId)))) : respondError(sock, 404, QStringLiteral("not_found"));
  } else if (parts.size() == 7 && parts.at(6) == QLatin1String("content") && req.method == "GET") {
    if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    const FakeSlot& s = saves.value(gameId);
    const QByteArray sha = QCryptographicHash::hash(s.content, QCryptographicHash::Sha256).toHex();
    respond(sock, 200, s.content, "application/octet-stream",
            {{"ETag", "\"" + sha + "\""}, {"X-FrameBeam-Save-Revision", QByteArray::number(s.revision)}});
  } else if (parts.size() == 6 && req.method == "PUT") {
    const QString actual = QString::fromLatin1(QCryptographicHash::hash(req.body, QCryptographicHash::Sha256).toHex());
    if (actual != sha256Hdr) {
      respondError(sock, 400, QStringLiteral("bad_request"));
      return;
    }
    const int base = req.headers.value(QStringLiteral("x-framebeam-base-revision")).toInt();
    FakeSlot& s = saves[gameId];
    const QString curSha = QString::fromLatin1(QCryptographicHash::hash(s.content, QCryptographicHash::Sha256).toHex());
    if (s.revision > 0 && curSha == actual) {
      respond(sock, 200, json(slotJson(gameId, s)));
    } else if (base == s.revision) {
      s.revision += 1;
      s.content = req.body;
      s.deviceId = callerDeviceId;
      s.deviceName = QStringLiteral("Test Device");
      s.reason = QString::fromLatin1(req.headers.value(QStringLiteral("x-framebeam-sync-reason")));
      respond(sock, 200, json(slotJson(gameId, s)));
    } else {
      FakeConflict* c = nullptr;
      for (FakeConflict& x : s.conflicts) {
        if (x.status == QLatin1String("open") && x.securedDeviceId == callerDeviceId) {
          c = &x;
        }
      }
      if (c == nullptr) {
        s.conflicts.append(FakeConflict{});
        c = &s.conflicts.last();
        c->id = QStringLiteral("c%1").arg(s.conflicts.size());
      }
      c->hubRevision = s.revision;
      c->hubSha = curSha;
      c->hubDeviceId = s.deviceId;
      c->hubDeviceName = s.deviceName;
      c->securedContent = req.body;
      c->securedVersion = s.nextVersion++;
      c->securedBase = base;
      c->securedDeviceId = callerDeviceId;
      c->securedDeviceName = QStringLiteral("Test Device");
      respond(sock, 409,
              json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("save_conflict")},
                                                          {QStringLiteral("message"), QStringLiteral("stale")}}},
                    {QStringLiteral("conflict"), conflictJson(gameId, s, *c)}}));
    }
  } else if (parts.size() == 9 && parts.at(6) == QLatin1String("conflicts") && parts.at(8) == QLatin1String("resolve") &&
             req.method == "POST") {
    const QJsonObject body = QJsonDocument::fromJson(req.body).object();
    FakeSlot& s = saves[gameId];
    FakeConflict* c = nullptr;
    for (FakeConflict& x : s.conflicts) {
      if (x.id == parts.at(7) && x.status == QLatin1String("open")) {
        c = &x;
      }
    }
    if (c == nullptr || body.value(QStringLiteral("expected_revision")).toInt() != s.revision) {
      respondError(sock, 409, QStringLiteral("save_conflict_stale"));
      return;
    }
    if (body.value(QStringLiteral("resolution")).toString() == QLatin1String("use_local")) {
      s.revision += 1;
      s.content = c->securedContent;
      s.deviceId = c->securedDeviceId;
      s.deviceName = c->securedDeviceName;
      c->status = QStringLiteral("resolved_local");
    } else {
      c->status = QStringLiteral("resolved_hub");
    }
    respond(sock, 200, json(slotJson(gameId, s)));
  } else {
    respondError(sock, 404, QStringLiteral("not_found"));
  }
}
