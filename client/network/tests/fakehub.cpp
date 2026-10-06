#include "fakehub.h"

#include <QCryptographicHash>

#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
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

int FakeHub::count(const QString& pathPrefix, const QByteArray& method) const {
  int n = 0;
  for (const FakeRequest& r : requests) {
    if (r.method == method && r.path.startsWith(pathPrefix)) {
      ++n;
    }
  }
  return n;
}

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
  connect(sock, &QSslSocket::disconnected, this, [this, sock]() {
    wsClients_.removeAll(sock);
    wsBuffers_.remove(sock);
  });
  connect(sock, &QSslSocket::readyRead, this, [this, sock, buffer]() {
    if (wsBuffers_.contains(sock)) {
      wsBuffers_[sock].append(sock->readAll());
      onWsData(sock);
      return;
    }
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
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/invites/redeem")) {
    lastInviteBody = body;
    if (rateLimitInvites) {
      respondError(sock, 429, QStringLiteral("rate_limited"));
      return;
    }
    if (body.value(QStringLiteral("code")).toString() != inviteCode) {
      respondError(sock, 404, QStringLiteral("invite_invalid"));
      return;
    }
    const QString dn = body.value(QStringLiteral("display_name")).toString();
    for (const QString& t : std::as_const(takenNames)) {
      if (t.compare(dn, Qt::CaseInsensitive) == 0) {
        respondError(sock, 409, QStringLiteral("display_name_taken"));
        return;
      }
    }
    approvedDelivered_ = false;
    if (inviteDirect) {
      respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("approved")},
                               {QStringLiteral("hub_id"), hubId},
                               {QStringLiteral("user_id"), QStringLiteral("u_invited_1")},
                               {QStringLiteral("device_credential"), QString::fromLatin1(kDeviceCredential)}}));
    } else {
      respond(sock, 202, json({{QStringLiteral("request_id"), QStringLiteral("req-1")},
                               {QStringLiteral("poll_token"), QString::fromLatin1(kPollToken)},
                               {QStringLiteral("status"), QStringLiteral("pending")},
                               {QStringLiteral("expires_in"), 600}}));
    }
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/auth/token")) {
    ++tokenRequests_;
    callerDeviceId = body.value(QStringLiteral("device_id")).toString();
    if (userDisabled) {
      respondError(sock, 401, QStringLiteral("user_disabled"));
    } else if (revoked) {
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
  } else if (req.path == QLatin1String("/api/v1/ws")) {
    upgradeWs(sock, req);
  } else if (req.path == QLatin1String("/api/v1/sessions") || req.path.startsWith(QLatin1String("/api/v1/sessions/")) ||
             req.path == QLatin1String("/api/v1/users")) {
    handleSessions(sock, req);
  } else if (req.path == QLatin1String("/api/v1/saves") || req.path.startsWith(QLatin1String("/api/v1/games/"))) {
    handleSaves(sock, req);
  } else if (req.method == "GET" && req.path == QLatin1String("/api/v1/games")) {
    respond(sock, 200, json(games));
  } else if (req.method == "POST" && req.path.startsWith(QLatin1String("/api/v1/games?"))) {
    const QUrlQuery q(req.path.mid(req.path.indexOf(QLatin1Char('?')) + 1));
    if (!uploadsAllowed) {
      respondError(sock, 403, QStringLiteral("uploads_disabled"));
      return;
    }
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(req.body, QCryptographicHash::Sha256).toHex());
    const QString filename = q.queryItemValue(QStringLiteral("filename"), QUrl::FullyDecoded);
    const QString title = q.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded);
    QJsonArray list = games.value(QStringLiteral("games")).toArray();
    for (const QJsonValue& v : std::as_const(list)) {
      if (v.toObject().value(QStringLiteral("rom")).toObject().value(QStringLiteral("sha256")).toString() == sha) {
        QJsonObject e{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("conflict")},
                                                            {QStringLiteral("message"), QStringLiteral("This ROM is already in the library")}}},
                      {QStringLiteral("existing_game_id"), v.toObject().value(QStringLiteral("id")).toString()}};
        respond(sock, 409, json(e));
        return;
      }
    }
    uploads.append({filename, title, sha, req.body.size()});
    const QJsonObject game{{QStringLiteral("id"), QStringLiteral("up-%1").arg(uploads.size())},
                           {QStringLiteral("title"), title.isEmpty() ? QFileInfo(filename).completeBaseName() : title},
                           {QStringLiteral("system"), QStringLiteral("nds")},
                           {QStringLiteral("rom"), QJsonObject{{QStringLiteral("sha256"), sha},
                                                               {QStringLiteral("size"), req.body.size()},
                                                               {QStringLiteral("filename"), filename}}},
                           {QStringLiteral("uploaded_by"), QStringLiteral("u_test_1")},
                           {QStringLiteral("added_at"), QStringLiteral("2026-01-01T12:00:00Z")}};
    list.append(game);
    games.insert(QStringLiteral("games"), list);
    respond(sock, 201, json(game));
  } else if (req.method == "GET" && req.path == QLatin1String("/api/v1/systems")) {
    respond(sock, 200, json(systems));
  } else if (req.method == "GET" && req.path.startsWith(QLatin1String("/api/v1/systems/"))) {
    // /api/v1/systems/<system>/firmware/<file>
    const QStringList parts = req.path.mid(16).split(QLatin1Char('/'));
    if (parts.size() != 3 || parts.at(1) != QLatin1String("firmware") || !firmwareFiles.contains(parts.at(0) + QLatin1Char('/') + parts.at(2))) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    ++firmwareDownloads;
    respond(sock, 200, firmwareFiles.value(parts.at(0) + QLatin1Char('/') + parts.at(2)), "application/octet-stream");
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

// ---------------------------------------------------------------- sessions (REST)

void FakeHub::handleSessions(QSslSocket* sock, const FakeRequest& req) {
  const auto json = [](const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
  const QJsonObject body = QJsonDocument::fromJson(req.body).object();
  const QString path = req.path.mid(QStringLiteral("/api/v1").size());
  if (path == QLatin1String("/users") && req.method == "GET" && !fakeUsers.isEmpty()) {
    respond(sock, 200, json({{QStringLiteral("users"), fakeUsers}}));
    return;
  }
  if (path == QLatin1String("/users") && req.method == "GET") {
    respond(sock, 200, json({{QStringLiteral("users"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("u_test_1")},
                                                                               {QStringLiteral("display_name"), QStringLiteral("Tester")},
                                                                               {QStringLiteral("online"), true}}}}}));
    return;
  }
  if (path == QLatin1String("/sessions")) {
    if (req.method == "GET") {
      QJsonArray arr;
      for (const QJsonObject& s : std::as_const(sessions)) {
        arr.append(s);
      }
      respond(sock, 200, json({{QStringLiteral("sessions"), arr}}));
    } else if (req.method == "POST") {
      if (publishCapabilityMissing) {
        respondError(sock, 409, QStringLiteral("capability_missing"));
        return;
      }
      const QString id = QStringLiteral("00000000-0000-4000-8000-%1").arg(sessions.size() + 1, 12, 10, QLatin1Char('0'));
      QJsonObject s{{QStringLiteral("session_id"), id},
                    {QStringLiteral("game_id"), body.value(QStringLiteral("game_id"))},
                    {QStringLiteral("game_title"), QStringLiteral("Demo Homebrew")},
                    {QStringLiteral("owner"), QJsonObject{{QStringLiteral("user_id"), QStringLiteral("u_test_1")},
                                                          {QStringLiteral("display_name"), QStringLiteral("Tester")},
                                                          {QStringLiteral("device_name"), QStringLiteral("Test Device")}}},
                    {QStringLiteral("visibility"), body.value(QStringLiteral("visibility"))},
                    {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:00:00Z")},
                    {QStringLiteral("viewer_count"), 0},
                    {QStringLiteral("is_owner"), true},
                    {QStringLiteral("invited"), false}};
      sessions.insert(id, s);
      respond(sock, 201, json(s));
    } else {
      respondError(sock, 404, QStringLiteral("not_found"));
    }
    return;
  }
  const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);  // sessions, id, [sub, arg]
  if (parts.size() < 2 || parts.at(0) != QLatin1String("sessions")) {
    respondError(sock, 404, QStringLiteral("not_found"));
    return;
  }
  const QString id = parts.at(1);
  if (!sessions.contains(id)) {
    respondError(sock, 404, QStringLiteral("session_not_found"));
    return;
  }
  QJsonObject& s = sessions[id];
  if (parts.size() == 2) {
    if (req.method == "GET") {
      respond(sock, 200, json(s));
    } else if (req.method == "PATCH") {
      s.insert(QStringLiteral("visibility"), body.value(QStringLiteral("visibility")));
      respond(sock, 200, json(s));
    } else if (req.method == "DELETE") {
      sessions.remove(id);
      respond(sock, 204, {});
    }
  } else if (parts.at(2) == QLatin1String("join") && req.method == "POST") {
    if (joinFull) {
      respondError(sock, 409, QStringLiteral("session_full"));
      return;
    }
    respond(sock, 201, json({{QStringLiteral("viewer_id"), QStringLiteral("11111111-1111-4111-8111-111111111111")},
                             {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("view_video"), true},
                                                                         {QStringLiteral("hear_audio"), true},
                                                                         {QStringLiteral("send_input"), false}}},
                             {QStringLiteral("ice_servers"), QJsonArray{QStringLiteral("stun:stun.example.org:3478")}}}));
  } else if (parts.at(2) == QLatin1String("viewers") && req.method == "DELETE") {
    respond(sock, 204, {});
  } else if (parts.at(2) == QLatin1String("invites") && req.method == "PUT") {
    respond(sock, 200, json(s));
  } else if ((parts.at(2) == QLatin1String("invites") && req.method == "DELETE") ||
             (parts.at(2) == QLatin1String("decline") && req.method == "POST")) {
    respond(sock, 204, {});
  } else {
    respondError(sock, 404, QStringLiteral("not_found"));
  }
}

// ---------------------------------------------------------------- WSS (minimal RFC 6455 server side)

void FakeHub::upgradeWs(QSslSocket* sock, const FakeRequest& req) {
  lastWsAuthorization = req.headers.value(QStringLiteral("authorization"));
  const QByteArray key = req.headers.value(QStringLiteral("sec-websocket-key"));
  if (refuseWs || key.isEmpty()) {
    respondError(sock, 401, QStringLiteral("unauthorized"));
    return;
  }
  const QByteArray accept =
      QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
  sock->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept +
              "\r\n\r\n");
  ++wsConnections;
  wsClients_.append(sock);
  wsBuffers_.insert(sock, {});
}

void FakeHub::wsWrite(QSslSocket* sock, quint8 opcode, const QByteArray& payload) {
  QByteArray f;
  f.append(static_cast<char>(0x80 | opcode));
  if (payload.size() < 126) {
    f.append(static_cast<char>(payload.size()));
  } else {
    f.append(static_cast<char>(126));
    f.append(static_cast<char>((payload.size() >> 8) & 0xFF));
    f.append(static_cast<char>(payload.size() & 0xFF));
  }
  sock->write(f + payload);
}

void FakeHub::onWsData(QSslSocket* sock) {
  QByteArray& buf = wsBuffers_[sock];
  while (buf.size() >= 2) {
    const quint8 b0 = static_cast<quint8>(buf[0]), b1 = static_cast<quint8>(buf[1]);
    qsizetype len = b1 & 0x7F, off = 2;
    if (len == 126) {
      if (buf.size() < 4) return;
      len = (static_cast<quint8>(buf[2]) << 8) | static_cast<quint8>(buf[3]);
      off = 4;
    }
    const bool masked = (b1 & 0x80) != 0;
    if (buf.size() < off + (masked ? 4 : 0) + len) return;
    QByteArray payload = buf.mid(off + (masked ? 4 : 0), len);
    if (masked) {
      for (qsizetype i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(payload[i] ^ buf[off + (i % 4)]);
      }
    }
    buf.remove(0, off + (masked ? 4 : 0) + len);
    const quint8 op = b0 & 0x0F;
    if (op == 0x9) {
      wsWrite(sock, 0xA, payload);
    } else if (op == 0x8) {
      wsWrite(sock, 0x8, {});
      sock->disconnectFromHost();
      return;
    } else if (op == 0x1) {
      const QJsonObject env = QJsonDocument::fromJson(payload).object();
      wsReceived.append(env);
      if (env.value(QStringLiteral("type")).toString() == QLatin1String("hello")) {
        const QJsonObject ack{{QStringLiteral("type"), QStringLiteral("hello_ack")},
                              {QStringLiteral("payload"),
                               QJsonObject{{QStringLiteral("protocol_version"), protocolVersion},
                                           {QStringLiteral("hub_version"), QStringLiteral("0.1.0")},
                                           {QStringLiteral("features"), QJsonArray{QStringLiteral("saves_v1"), QStringLiteral("sessions_v1")}},
                                           {QStringLiteral("ice_servers"), QJsonArray()}}}};
        wsWrite(sock, 0x1, QJsonDocument(ack).toJson(QJsonDocument::Compact));
      }
    }
  }
}

void FakeHub::sendWs(const QString& type, const QJsonObject& payload) {
  const QByteArray msg =
      QJsonDocument(QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("payload"), payload}}).toJson(QJsonDocument::Compact);
  for (QSslSocket* c : std::as_const(wsClients_)) {
    wsWrite(c, 0x1, msg);
  }
}

void FakeHub::closeWsClients() {
  const QList<QSslSocket*> list = wsClients_;
  for (QSslSocket* c : list) {
    c->abort();
  }
}
