#include "fakehub.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>

#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
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

bool FakeHub::setCertificate(const QString& certName) {
  const QString dir = QStringLiteral(FB_TEST_DATA_DIR);
  QFile c(dir + QStringLiteral("/test-cert-") + certName + QStringLiteral(".pem"));
  QFile k(dir + QStringLiteral("/test-key-") + certName + QStringLiteral(".pem"));
  if (!c.open(QIODevice::ReadOnly) || !k.open(QIODevice::ReadOnly)) {
    return false;
  }
  cert_ = QSslCertificate(&c, QSsl::Pem);
  key_ = QSslKey(&k, QSsl::Ec, QSsl::Pem);
  certName_ = certName;
  return !cert_.isNull() && !key_.isNull();
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
    req.arrivalMs = QDeadlineTimer::current().deadlineNSecs() / 1000000;
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
  } else if (req.method == "GET" && req.path == QLatin1String("/api/v1/local/status")) {
    respond(sock, 200, json({{QStringLiteral("admin_exists"), localAdminExists},
                             {QStringLiteral("network_sharing"), localNetworkSharing},
                             {QStringLiteral("import_dir"), localImportDir},
                             {QStringLiteral("hub_version"), QStringLiteral("0.9.0")}}));
  } else if (req.method == "POST" && (req.path == QLatin1String("/api/v1/local/setup") || req.path == QLatin1String("/api/v1/local/pair"))) {
    lastLocalBody = body;
    const bool setup = req.path.endsWith(QLatin1String("/setup"));
    if (setup && localAdminExists) {
      respondError(sock, 409, QStringLiteral("admin_exists"));
    } else if (!setup && body.value(QStringLiteral("password")).toString() != QLatin1String("correct horse")) {
      respondError(sock, 401, QStringLiteral("invalid_credentials"));
    } else {
      if (setup) localAdminExists = true;
      approvedDelivered_ = true;
      respond(sock, 200, json({{QStringLiteral("status"), QStringLiteral("approved")},
                               {QStringLiteral("hub_id"), hubId},
                               {QStringLiteral("user_id"), QStringLiteral("u_admin_1")},
                               {QStringLiteral("device_credential"), QString::fromLatin1(kDeviceCredential)}}));
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
  } else if (req.method == "PUT" && req.path == QLatin1String("/api/v1/local/settings")) {
    lastLocalSettings = body;
    if (body.contains(QStringLiteral("import_dir")) && !localImportDirError.isEmpty()) {
      respond(sock, 400, json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("import_dir_unreadable")},
                                                                    {QStringLiteral("message"), localImportDirError}}}}));
      return;
    }
    if (body.contains(QStringLiteral("network_sharing"))) localNetworkSharing = body.value(QStringLiteral("network_sharing")).toBool();
    if (body.contains(QStringLiteral("import_dir"))) localImportDir = body.value(QStringLiteral("import_dir")).toString();
    respond(sock, 200, json({{QStringLiteral("admin_exists"), localAdminExists},
                             {QStringLiteral("network_sharing"), localNetworkSharing},
                             {QStringLiteral("import_dir"), localImportDir},
                             {QStringLiteral("hub_version"), QStringLiteral("0.9.0")}}));
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/handshake")) {
    lastHandshakeBody = body;
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
  } else if (req.path == QLatin1String("/api/v1/runs") || req.path.startsWith(QLatin1String("/api/v1/runs/"))) {
    handleRuns(sock, req);
  } else if (req.method == "POST" && req.path == QLatin1String("/api/v1/reports")) {
    if (reportUnauthorizedNext > 0) {
      --reportUnauthorizedNext;
      respondError(sock, 401, QStringLiteral("unauthorized"));
      return;
    }
    if (holdReports) {
      ++reportsHeld;
      return;
    }
    if (reportDropNext > 0) {
      --reportDropNext;
      sock->abort();
      return;
    }
    if (reportFailNext > 0) {
      --reportFailNext;
      respondError(sock, reportFailStatus, reportFailStatus == 429 ? QStringLiteral("rate_limited") : reportFailStatus == 413 ? QStringLiteral("payload_too_large") : QStringLiteral("internal"));
      return;
    }
    // multipart/form-data: split on the boundary of the Content-Type header.
    const QByteArray ctype = req.headers.value(QStringLiteral("content-type"));
    const qsizetype bi = ctype.indexOf("boundary=");
    if (!ctype.startsWith("multipart/form-data") || bi < 0) {
      respondError(sock, 400, QStringLiteral("bad_request"));
      return;
    }
    const QByteArray delim = "--" + ctype.mid(bi + 9);
    ReceivedReport rep;
    rep.authorization = req.headers.value(QStringLiteral("authorization"));
    bool first = true;
    qsizetype pos = req.body.indexOf(delim);
    while (pos >= 0) {
      pos += delim.size();
      if (req.body.mid(pos, 2) == "--") break;
      pos += 2;  // CRLF
      const qsizetype hdrEnd = req.body.indexOf("\r\n\r\n", pos);
      const qsizetype next = req.body.indexOf("\r\n" + delim, hdrEnd);
      if (hdrEnd < 0 || next < 0) {
        respondError(sock, 400, QStringLiteral("bad_request"));
        return;
      }
      const QByteArray headers = req.body.mid(pos, hdrEnd - pos);
      const QByteArray content = req.body.mid(hdrEnd + 4, next - hdrEnd - 4);
      if (first) {
        if (!headers.contains("name=\"meta\"")) {
          respondError(sock, 400, QStringLiteral("bad_request"));
          return;
        }
        rep.meta = QJsonDocument::fromJson(content).object();
        first = false;
      } else {
        const qsizetype fi = headers.indexOf("filename=\"");
        const QByteArray rest = headers.mid(fi + 10);
        rep.files.insert(QString::fromUtf8(rest.left(rest.indexOf('"'))), content);
      }
      pos = next + 2;
    }
    reports.append(rep);
    respond(sock, 201, json({{QStringLiteral("id"), reports.size()}}));
  } else if (handleLocalRoms(sock, req)) {
    // answered
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
  } else if (req.method == "GET" && req.path.startsWith(QLatin1String("/api/v1/cores/"))) {
    // /api/v1/cores/<core>/packages/<version>/<platform>[/files/<name>]
    const QStringList parts = req.path.mid(14).split(QLatin1Char('/'));
    if (parts.size() < 4 || parts.at(1) != QLatin1String("packages")) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    const QString key = parts.at(0) + QLatin1Char('/') + parts.at(2) + QLatin1Char('/') + parts.at(3);
    if (parts.size() == 4) {
      ++corePackageRequests;
      if (!corePackages.contains(key)) {
        respondError(sock, 404, QStringLiteral("core_package_not_found"));
        return;
      }
      respond(sock, 200, json(corePackages.value(key)));
    } else if (parts.size() == 6 && parts.at(4) == QLatin1String("files")) {
      const QString fkey = key + QLatin1Char('/') + parts.at(5);
      if (!coreFiles.contains(fkey)) {
        respondError(sock, 404, QStringLiteral("core_file_not_available"));
        return;
      }
      if (holdCoreFiles) {  // never answered: the test checks that the client drops the connection
        ++coreFilesHeld;
        connect(sock, &QSslSocket::disconnected, this, [this]() { ++coreFilesAbandoned; });
        return;
      }
      ++coreFileDownloads;
      respond(sock, 200, coreFiles.value(fkey), "application/octet-stream");
    } else {
      respondError(sock, 404, QStringLiteral("not_found"));
    }
  } else if (req.method == "GET" && req.path == QLatin1String("/api/v1/plugins")) {
    ++pluginListRequests;
    if (pluginListFails) {
      respondError(sock, 500, QStringLiteral("internal"));
      return;
    }
    respond(sock, 200, json({{QStringLiteral("plugins"), pluginList}}));
  } else if (req.method == "GET" && req.path.startsWith(QLatin1String("/api/v1/plugins/")) && req.path.endsWith(QLatin1String("/file"))) {
    // /api/v1/plugins/<id>/<version>/file
    const QStringList parts = req.path.mid(16).split(QLatin1Char('/'));
    const QString key = parts.size() == 3 ? parts.at(0) + QLatin1Char('/') + parts.at(1) : QString();
    if (key.isEmpty() || !pluginFiles.contains(key)) {
      respondError(sock, 404, QStringLiteral("plugin_not_found"));
      return;
    }
    ++pluginFileDownloads;
    respond(sock, 200, pluginFiles.value(key), "application/zip");
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

void FakeHub::addHistory(const QString& gameId, const QString& slot, const QByteArray& content, const QString& reason, const QString& label,
                         const QString& deviceName) {
  FakeSlot& s = saves[slotKey(gameId, slot)];
  FakeVersion v;
  v.version = s.nextVersion++;
  v.revision = std::max(1, s.revision);
  v.content = content;
  v.deviceId = QStringLiteral("other-device");
  v.deviceName = deviceName;
  v.reason = reason;
  v.label = label;
  s.history.append(v);
}

QJsonObject FakeHub::versionJson(const FakeVersion& v) const {
  return {{QStringLiteral("version"), v.version},
          {QStringLiteral("revision"), v.revision},
          {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(v.content, QCryptographicHash::Sha256).toHex())},
          {QStringLiteral("size"), v.content.size()},
          {QStringLiteral("device_id"), v.deviceId},
          {QStringLiteral("device_name"), v.deviceName},
          {QStringLiteral("created_at"), QStringLiteral("2026-01-01T12:%1:00Z").arg(v.version % 60, 2, 10, QLatin1Char('0'))},
          {QStringLiteral("reason"), v.reason},
          {QStringLiteral("label"), v.label.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(v.label)}};
}

QJsonObject FakeHub::slotJson(const QString& key, const FakeSlot& s) const {
  const QString gameId = key.section(QLatin1Char('/'), 0, 0);
  const QString slot = key.contains(QLatin1Char('/')) ? key.section(QLatin1Char('/'), 1) : QStringLiteral("default");
  QJsonArray conflicts;
  for (const FakeConflict& c : s.conflicts) {
    if (c.status == QLatin1String("open")) {
      conflicts.append(conflictJson(key, s, c));
    }
  }
  return {{QStringLiteral("game_id"), gameId},
          {QStringLiteral("slot"), slot},
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

QJsonObject FakeHub::conflictJson(const QString& key, const FakeSlot&, const FakeConflict& c) const {
  return {{QStringLiteral("id"), c.id},
          {QStringLiteral("game_id"), key.section(QLatin1Char('/'), 0, 0)},
          {QStringLiteral("slot"), key.contains(QLatin1Char('/')) ? key.section(QLatin1Char('/'), 1) : QStringLiteral("default")},
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

bool FakeHub::handleLocalRoms(QSslSocket* sock, const FakeRequest& req) {
  const auto json = [](const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
  const QString P = QStringLiteral("/api/v1/");
  const auto gameWithSha = [this](const QString& sha) {
    for (const QJsonValue& v : games.value(QStringLiteral("games")).toArray()) {
      for (const QJsonValue& r : v.toObject().value(QStringLiteral("roms")).toArray()) {
        if (r.toObject().value(QStringLiteral("sha256")).toString() == sha) return v.toObject();
      }
      if (v.toObject().value(QStringLiteral("rom")).toObject().value(QStringLiteral("sha256")).toString() == sha) return v.toObject();
    }
    return QJsonObject();
  };
  if (req.method == "POST" && req.path == P + QStringLiteral("games/register")) {
    const QJsonObject body = QJsonDocument::fromJson(req.body).object();
    registerBodies.append(body);
    const QString sha = body.value(QStringLiteral("sha256")).toString();
    const QJsonObject existing = gameWithSha(sha);
    if (!existing.isEmpty()) {
      respond(sock, 200, json(existing));
      return true;
    }
    const QJsonObject rom{{QStringLiteral("sha256"), sha},
                          {QStringLiteral("size"), body.value(QStringLiteral("size"))},
                          {QStringLiteral("filename"), body.value(QStringLiteral("filename"))},
                          {QStringLiteral("stored"), false}};
    QJsonObject game{{QStringLiteral("id"), QStringLiteral("reg-%1").arg(registerBodies.size())},
                     {QStringLiteral("title"), QFileInfo(body.value(QStringLiteral("filename")).toString()).completeBaseName()},
                     {QStringLiteral("system"), body.value(QStringLiteral("system")).toString(QStringLiteral("nds"))},
                     {QStringLiteral("rom"), rom},
                     {QStringLiteral("roms"), QJsonArray{rom}},
                     {QStringLiteral("on_hub"), false},
                     {QStringLiteral("uploaded_by"), QStringLiteral("u_test_1")},
                     {QStringLiteral("added_at"), QStringLiteral("2026-01-01T12:00:00Z")}};
    if (body.contains(QStringLiteral("game_code"))) game.insert(QStringLiteral("game_code"), body.value(QStringLiteral("game_code")));
    if (body.contains(QStringLiteral("game_version"))) game.insert(QStringLiteral("game_version"), body.value(QStringLiteral("game_version")));
    QJsonArray list = games.value(QStringLiteral("games")).toArray();
    list.append(game);
    games.insert(QStringLiteral("games"), list);
    respond(sock, 201, json(game));
    return true;
  }
  if (req.method == "DELETE" && req.path.startsWith(P + QStringLiteral("games/")) && req.path.endsWith(QLatin1String("/registration"))) {
    const QString id = req.path.mid((P + QStringLiteral("games/")).size()).section(QLatin1Char('/'), 0, 0);
    unregisteredGames.append(id);
    QJsonArray out;
    for (const QJsonValue& v : games.value(QStringLiteral("games")).toArray()) {
      if (v.toObject().value(QStringLiteral("id")).toString() != id) out.append(v);
    }
    games.insert(QStringLiteral("games"), out);
    respond(sock, 204, {});
    return true;
  }
  if (req.path == P + QStringLiteral("uploads") && req.method == "POST") {
    const QJsonObject body = QJsonDocument::fromJson(req.body).object();
    const QString sha = body.value(QStringLiteral("sha256")).toString();
    const QJsonObject stored = gameWithSha(sha);
    if (!stored.isEmpty() && stored.value(QStringLiteral("on_hub")).toBool(true)) {
      respond(sock, 409, json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("conflict")}}},
                               {QStringLiteral("existing_game_id"), stored.value(QStringLiteral("id")).toString()}}));
      return true;
    }
    for (const ResumableUpload& u : std::as_const(uploadSessions)) {
      if (u.sha256 == sha) {
        respond(sock, 201, json({{QStringLiteral("upload_id"), u.id}, {QStringLiteral("offset"), u.data.size()}, {QStringLiteral("chunk_size"), uploadChunkSize}}));
        return true;
      }
    }
    ResumableUpload u;
    u.id = QStringLiteral("up%1").arg(++uploadSessionsCreated);
    u.filename = body.value(QStringLiteral("filename")).toString();
    u.title = body.value(QStringLiteral("title")).toString();
    u.system = body.value(QStringLiteral("system")).toString();
    u.sha256 = sha;
    u.size = body.value(QStringLiteral("size")).toVariant().toLongLong();
    uploadSessions.insert(u.id, u);
    respond(sock, 201, json({{QStringLiteral("upload_id"), u.id}, {QStringLiteral("offset"), 0}, {QStringLiteral("chunk_size"), uploadChunkSize}}));
    return true;
  }
  if (req.path.startsWith(P + QStringLiteral("uploads/"))) {
    const QString id = req.path.mid((P + QStringLiteral("uploads/")).size());
    if (!uploadSessions.contains(id)) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return true;
    }
    ResumableUpload& u = uploadSessions[id];
    if (req.method == "GET") {
      respond(sock, 200, json({{QStringLiteral("upload_id"), id}, {QStringLiteral("offset"), u.data.size()}, {QStringLiteral("size"), u.size}}));
      return true;
    }
    if (req.method == "DELETE") {
      uploadSessions.remove(id);
      respond(sock, 204, {});
      return true;
    }
    if (req.method == "PUT") {
      const QString range = QString::fromLatin1(req.headers.value(QStringLiteral("content-range")));
      putRanges.append(range);
      if (failPutNumber == putRanges.size()) {
        respondError(sock, 503, QStringLiteral("internal"));
        return true;
      }
      // "bytes a-b/total"
      const QString spec = range.mid(6);
      const qint64 a = spec.section(QLatin1Char('-'), 0, 0).toLongLong();
      if (a != u.data.size()) {
        respond(sock, 409, json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("offset_mismatch")}}}, {QStringLiteral("offset"), u.data.size()}}));
        return true;
      }
      u.data.append(req.body);
      if (u.data.size() < u.size) {
        respond(sock, 200, json({{QStringLiteral("offset"), u.data.size()}}));
        return true;
      }
      const QString sha = QString::fromLatin1(QCryptographicHash::hash(u.data, QCryptographicHash::Sha256).toHex());
      if (sha != u.sha256) {
        respondError(sock, 400, QStringLiteral("hash_mismatch"));
        return true;
      }
      roms.insert(sha, u.data);
      QJsonObject rom{{QStringLiteral("sha256"), sha}, {QStringLiteral("size"), u.size}, {QStringLiteral("filename"), u.filename}, {QStringLiteral("stored"), true}};
      QJsonObject game = gameWithSha(sha);
      QJsonArray list;
      bool replaced = false;
      for (const QJsonValue& v : games.value(QStringLiteral("games")).toArray()) {
        if (!game.isEmpty() && v.toObject().value(QStringLiteral("id")) == game.value(QStringLiteral("id"))) {
          game.insert(QStringLiteral("rom"), rom);
          game.insert(QStringLiteral("roms"), QJsonArray{rom});
          game.insert(QStringLiteral("on_hub"), true);  // same game_id as the local-only game
          list.append(game);
          replaced = true;
        } else {
          list.append(v);
        }
      }
      if (!replaced) {
        game = QJsonObject{{QStringLiteral("id"), QStringLiteral("new-%1").arg(id)},
                           {QStringLiteral("title"), u.title.isEmpty() ? QFileInfo(u.filename).completeBaseName() : u.title},
                           {QStringLiteral("system"), u.system.isEmpty() ? QStringLiteral("nds") : u.system},
                           {QStringLiteral("rom"), rom},
                           {QStringLiteral("roms"), QJsonArray{rom}},
                           {QStringLiteral("on_hub"), true},
                           {QStringLiteral("uploaded_by"), QStringLiteral("u_test_1")},
                           {QStringLiteral("added_at"), QStringLiteral("2026-01-01T12:00:00Z")}};
        list.append(game);
      }
      games.insert(QStringLiteral("games"), list);
      const qint64 total = u.size;
      uploadSessions.remove(id);
      respond(sock, 200, json({{QStringLiteral("offset"), total}, {QStringLiteral("game"), game}}));
      return true;
    }
  }
  return false;
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
  const QString query = req.path.section(QLatin1Char('?'), 1);
  const QStringList parts = req.path.section(QLatin1Char('?'), 0, 0).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (parts.size() < 6 || parts.at(4) != QLatin1String("saves")) {
    respondError(sock, 404, QStringLiteral("not_found"));
    return;
  }
  const QString gameId = parts.at(3);
  const QString slotName = parts.at(5);
  const QString key = slotKey(gameId, slotName);
  const bool exists = saves.contains(key) && saves.value(key).revision > 0;
  const bool sessionRunning = sessionRunningGames.contains(gameId);
  if (parts.size() == 7 && parts.at(6) == QLatin1String("rename") && req.method == "POST") {
    const QString newName = QJsonDocument::fromJson(req.body).object().value(QStringLiteral("name")).toString();
    const QString newKey = slotKey(gameId, newName);
    if (slotName == QLatin1String("default")) {
      respondError(sock, 409, QStringLiteral("save_slot_protected"));
    } else if (newName.isEmpty() || newName == slotName) {
      respondError(sock, 400, QStringLiteral("bad_request"));
    } else if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
    } else if (saves.contains(newKey) && saves.value(newKey).revision > 0) {
      respondError(sock, 409, QStringLiteral("save_slot_exists"));
    } else if (sessionRunning) {
      respondError(sock, 409, QStringLiteral("session_running"));
    } else {
      saves.insert(newKey, saves.take(key));
      ++renameCount;
      respond(sock, 200, json(slotJson(newKey, saves.value(newKey))));
    }
  } else if (parts.size() == 6 && req.method == "DELETE") {
    int confirm = -1;
    for (const QString& kv : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
      if (kv.startsWith(QLatin1String("confirm_revision="))) {
        confirm = kv.mid(17).toInt();
      }
    }
    if (slotName == QLatin1String("default") && !features.contains(QStringLiteral("saves_v6"))) {
      respondError(sock, 409, QStringLiteral("save_slot_protected"));
    } else if (confirm < 0) {
      respondError(sock, 400, QStringLiteral("bad_request"));
    } else if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
    } else if (sessionRunning) {
      respondError(sock, 409, QStringLiteral("session_running"));
    } else if (confirm != saves.value(key).revision) {
      respondError(sock, 409, QStringLiteral("save_conflict_stale"));
    } else {
      bool open = false;
      for (const FakeConflict& c : saves.value(key).conflicts) {
        open = open || c.status == QLatin1String("open");
      }
      if (open) {
        respondError(sock, 409, QStringLiteral("conflict"));
      } else {
        saves.remove(key);
        ++slotDeleteCount;
        respond(sock, 204, {});
      }
    }
  } else if (parts.size() == 6 && req.method == "GET") {
    exists ? respond(sock, 200, json(slotJson(key, saves.value(key)))) : respondError(sock, 404, QStringLiteral("not_found"));
  } else if (parts.size() == 7 && parts.at(6) == QLatin1String("content") && req.method == "GET") {
    if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    const FakeSlot& s = saves.value(key);
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
    FakeSlot& s = saves[key];
    const QString curSha = QString::fromLatin1(QCryptographicHash::hash(s.content, QCryptographicHash::Sha256).toHex());
    if (s.revision > 0 && curSha == actual) {
      respond(sock, 200, json(slotJson(key, s)));
    } else if (base == s.revision) {
      s.revision += 1;
      s.content = req.body;
      s.deviceId = callerDeviceId;
      s.deviceName = QStringLiteral("Test Device");
      s.reason = QString::fromLatin1(req.headers.value(QStringLiteral("x-framebeam-sync-reason")));
      respond(sock, 200, json(slotJson(key, s)));
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
                    {QStringLiteral("conflict"), conflictJson(key, s, *c)}}));
    }
  } else if (parts.size() == 7 && parts.at(6) == QLatin1String("history") && req.method == "GET") {
    if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    QJsonArray versions;
    const FakeSlot& s = saves.value(key);
    for (qsizetype k = s.history.size() - 1; k >= 0; --k) {
      versions.append(versionJson(s.history.at(k)));
    }
    respond(sock, 200, json({{QStringLiteral("versions"), versions}}));
  } else if (parts.size() == 7 && parts.at(6) == QLatin1String("snapshots") && req.method == "POST") {
    if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    FakeSlot& s = saves[key];
    FakeVersion v;
    v.version = s.nextVersion++;
    v.revision = s.revision;
    v.content = s.content;
    v.deviceId = s.deviceId;
    v.deviceName = s.deviceName;
    v.reason = QStringLiteral("manual_snapshot");
    v.label = QJsonDocument::fromJson(req.body).object().value(QStringLiteral("label")).toString();
    s.history.append(v);
    respond(sock, 201, json(versionJson(v)));
  } else if (parts.size() == 7 && parts.at(6) == QLatin1String("upload") && req.method == "POST") {
    if (req.body.size() > uploadLimit) {
      respondError(sock, 413, QStringLiteral("payload_too_large"));
      return;
    }
    const QString actual = QString::fromLatin1(QCryptographicHash::hash(req.body, QCryptographicHash::Sha256).toHex());
    if (req.body.isEmpty() || actual != sha256Hdr) {
      respondError(sock, 400, QStringLiteral("bad_request"));
      return;
    }
    if (sessionRunning) {
      respondError(sock, 409, QStringLiteral("session_running"));
      return;
    }
    FakeSlot& s = saves[key];
    if (req.headers.value(QStringLiteral("x-framebeam-expected-revision")).toInt() != (exists ? s.revision : 0)) {
      respondError(sock, 409, QStringLiteral("save_conflict_stale"));
      return;
    }
    if (exists) {
      FakeVersion before;
      before.version = s.nextVersion++;
      before.revision = s.revision;
      before.content = s.content;
      before.deviceId = s.deviceId;
      before.deviceName = s.deviceName;
      before.reason = QStringLiteral("before_upload");
      s.history.append(before);
    }
    s.revision += 1;
    s.content = req.body;
    s.deviceId = callerDeviceId;
    s.deviceName = QStringLiteral("Test Device");
    s.reason = QStringLiteral("upload");
    ++uploadCount;
    respond(sock, 200, json(slotJson(key, s)));
  } else if (parts.size() == 8 && parts.at(6) == QLatin1String("history") && req.method == "DELETE") {
    if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    FakeSlot& s = saves[key];
    const int want = parts.at(7).toInt();
    qsizetype found = -1;
    for (qsizetype k = 0; k < s.history.size(); ++k) {
      if (s.history.at(k).version == want) {
        found = k;
      }
    }
    if (found < 0) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    if (s.history.at(found).reason != QLatin1String("manual_snapshot") && !features.contains(QStringLiteral("saves_v6"))) {
      respondError(sock, 409, QStringLiteral("save_not_snapshot"));
      return;
    }
    s.history.removeAt(found);
    ++deleteCount;
    respond(sock, 204, {});
  } else if (parts.size() == 9 && parts.at(6) == QLatin1String("history") && parts.at(8) == QLatin1String("restore") &&
             req.method == "POST") {
    if (!exists) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    FakeSlot& s = saves[key];
    const int want = parts.at(7).toInt();
    const FakeVersion* src = nullptr;
    for (const FakeVersion& v : s.history) {
      if (v.version == want) {
        src = &v;
      }
    }
    if (src == nullptr) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    if (QJsonDocument::fromJson(req.body).object().value(QStringLiteral("expected_revision")).toInt() != s.revision) {
      respondError(sock, 409, QStringLiteral("save_conflict_stale"));
      return;
    }
    FakeVersion before;
    before.version = s.nextVersion++;
    before.revision = s.revision;
    before.content = s.content;
    before.deviceId = s.deviceId;
    before.deviceName = s.deviceName;
    before.reason = QStringLiteral("before_restore");
    const QByteArray restored = src->content;
    s.history.append(before);
    s.revision += 1;
    s.content = restored;
    s.deviceId = callerDeviceId;
    s.deviceName = QStringLiteral("Test Device");
    s.reason = QStringLiteral("restore");
    ++restoreCount;
    respond(sock, 200, json(slotJson(key, s)));
  } else if (parts.size() == 9 && parts.at(6) == QLatin1String("conflicts") && parts.at(8) == QLatin1String("resolve") &&
             req.method == "POST") {
    const QJsonObject body = QJsonDocument::fromJson(req.body).object();
    FakeSlot& s = saves[key];
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
    respond(sock, 200, json(slotJson(key, s)));
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
      if (holdSessionGet) {
        heldGets_.append({sock, s});
      } else {
        respond(sock, 200, json(s));
      }
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
    QJsonObject joinResponse{{QStringLiteral("viewer_id"), QStringLiteral("11111111-1111-4111-8111-111111111111")},
                             {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("view_video"), true},
                                                                         {QStringLiteral("hear_audio"), true},
                                                                         {QStringLiteral("send_input"), false}}},
                             {QStringLiteral("ice_servers"), QJsonArray{QStringLiteral("stun:stun.example.org:3478")}}};
    if (!joinTurnServers.isEmpty()) {
      joinResponse.insert(QStringLiteral("turn_servers"), joinTurnServers);
    }
    respond(sock, 201, json(joinResponse));
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
        QJsonObject ackPayload{{QStringLiteral("protocol_version"), protocolVersion},
                               {QStringLiteral("hub_version"), QStringLiteral("0.1.0")},
                               {QStringLiteral("features"), wsFeatures()},
                               {QStringLiteral("ice_servers"), QJsonArray()}};
        if (!helloTurnServers.isEmpty()) {
          ackPayload.insert(QStringLiteral("turn_servers"), helloTurnServers);
        }
        const QJsonObject ack{{QStringLiteral("type"), QStringLiteral("hello_ack")}, {QStringLiteral("payload"), ackPayload}};
        wsWrite(sock, 0x1, QJsonDocument(ack).toJson(QJsonDocument::Compact));
        sendRunCatchUp(sock, env.value(QStringLiteral("payload")).toObject());
      }
    }
  }
}

void FakeHub::releaseHeldSessionGets(const QString& visibilityOverride) {
  const QList<HeldGet> held = heldGets_;
  heldGets_.clear();
  for (HeldGet h : held) {
    if (!h.sock) {
      continue;
    }
    if (!visibilityOverride.isEmpty()) {
      h.session.insert(QStringLiteral("visibility"), visibilityOverride);
    }
    respond(h.sock, 200, QJsonDocument(h.session).toJson(QJsonDocument::Compact));
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

// ------------------------------------------------------------------ runs (runs_v1)

QString FakeHub::makeUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

namespace {
QString nowIso() { return QStringLiteral("2026-10-12T10:00:00Z"); }
}  // namespace

qint64 FakeHub::addRunEvent(const QString& runId, const QString& type, const QString& text, const QString& userId, const QString& pluginId,
                            const QString& clientId, bool push) {
  QList<QJsonObject>& list = runEvents[runId];
  const qint64 seq = list.isEmpty() ? 1 : static_cast<qint64>(list.last().value(QStringLiteral("seq")).toDouble()) + 1;
  QString userName;
  QJsonObject runObj = runs.value(runId);
  for (const QJsonValue& v : runObj.value(QStringLiteral("participants")).toArray())
    if (v.toObject().value(QStringLiteral("user_id")).toString() == userId) userName = v.toObject().value(QStringLiteral("user_name")).toString();
  const auto nullable = [](const QString& s) { return s.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s); };
  const QJsonObject e{{QStringLiteral("seq"), static_cast<double>(seq)},
                      {QStringLiteral("created_at"), nowIso()},
                      {QStringLiteral("user_id"), nullable(userId)},
                      {QStringLiteral("user_name"), userName},
                      {QStringLiteral("device_id"), userId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QStringLiteral("dev-1"))},
                      {QStringLiteral("plugin_id"), nullable(pluginId)},
                      {QStringLiteral("source"), userId.isEmpty() ? QStringLiteral("hub") : QStringLiteral("automatic")},
                      {QStringLiteral("type"), type},
                      {QStringLiteral("text"), text},
                      {QStringLiteral("payload"), QJsonObject()},
                      {QStringLiteral("client_id"), clientId.isEmpty() ? makeUuid() : clientId}};
  list.append(e);
  runObj.insert(QStringLiteral("last_seq"), static_cast<double>(seq));
  runs.insert(runId, runObj);
  if (push) sendWs(QStringLiteral("run_event"), {{QStringLiteral("run_id"), runId}, {QStringLiteral("event"), e}});
  return seq;
}

void FakeHub::pushRun(const QString& runId) {
  if (runs.contains(runId)) sendWs(QStringLiteral("run_updated"), {{QStringLiteral("run"), runs.value(runId)}});
}

QJsonObject FakeHub::makeRun(const QString& name, const QStringList& userIds, const QString& creator, const QJsonArray& plugins) {
  QJsonArray parts;
  int color = 0;
  for (const QString& u : userIds) {
    const bool joined = u == creator;
    parts.append(QJsonObject{{QStringLiteral("user_id"), u},
                             {QStringLiteral("user_name"), u == runsMe ? runsMeName : u},
                             {QStringLiteral("status"), joined ? QStringLiteral("joined") : QStringLiteral("invited")},
                             {QStringLiteral("game_id"), joined ? QJsonValue(QStringLiteral("00000000-0000-4000-8000-000000000001")) : QJsonValue(QJsonValue::Null)},
                             {QStringLiteral("color"), joined ? QJsonValue(color++) : QJsonValue(QJsonValue::Null)},
                             {QStringLiteral("states"), QJsonObject()}});
  }
  const QString id = makeUuid();
  const QJsonObject run{{QStringLiteral("run_id"), id},
                        {QStringLiteral("name"), name},
                        {QStringLiteral("status"), QStringLiteral("starting")},
                        {QStringLiteral("created_by"), creator},
                        {QStringLiteral("created_at"), nowIso()},
                        {QStringLiteral("updated_at"), nowIso()},
                        {QStringLiteral("status_by"), creator},
                        {QStringLiteral("settings"), QJsonObject()},
                        {QStringLiteral("plugins"), plugins},
                        {QStringLiteral("participants"), parts},
                        {QStringLiteral("last_seq"), 0}};
  runs.insert(id, run);
  addRunEvent(id, QStringLiteral("run.created"), name, {}, {}, {}, false);
  return runs.value(id);
}

void FakeHub::sendRunCatchUp(QSslSocket* sock, const QJsonObject& hello) {
  if (!features.contains(QStringLiteral("runs_v1"))) return;
  const auto write = [&](const QString& type, const QJsonObject& payload) {
    wsWrite(sock, 0x1, QJsonDocument(QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("payload"), payload}}).toJson(QJsonDocument::Compact));
  };
  QMap<QString, qint64> listed;
  for (const QJsonValue& v : hello.value(QStringLiteral("runs")).toArray())
    listed.insert(v.toObject().value(QStringLiteral("run_id")).toString(), static_cast<qint64>(v.toObject().value(QStringLiteral("last_seq")).toDouble()));
  for (auto it = runs.begin(); it != runs.end(); ++it) write(QStringLiteral("run_updated"), {{QStringLiteral("run"), it.value()}});
  for (auto it = listed.begin(); it != listed.end(); ++it) {
    QList<QJsonObject> missing;
    for (const QJsonObject& e : runEvents.value(it.key()))
      if (static_cast<qint64>(e.value(QStringLiteral("seq")).toDouble()) > it.value()) missing.append(e);
    if (static_cast<int>(missing.size()) > runHelloCatchUpMax) continue;
    for (const QJsonObject& e : missing) write(QStringLiteral("run_event"), {{QStringLiteral("run_id"), it.key()}, {QStringLiteral("event"), e}});
  }
}

void FakeHub::handleRuns(QSslSocket* sock, const FakeRequest& req) {
  const auto json = [](const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); };
  const QJsonObject body = QJsonDocument::fromJson(req.body).object();
  const QString full = req.path.mid(QStringLiteral("/api/v1/runs").size());
  const int q = full.indexOf(QLatin1Char('?'));
  const QUrlQuery query(q >= 0 ? full.mid(q + 1) : QString());
  const QStringList parts = (q >= 0 ? full.left(q) : full).split(QLatin1Char('/'), Qt::SkipEmptyParts);

  if (runFailNext > 0) {
    --runFailNext;
    respondError(sock, runFailStatus, QStringLiteral("internal"));
    return;
  }
  if (parts.isEmpty()) {
    if (req.method == "GET") {
      QJsonArray arr;
      for (const QJsonObject& r : std::as_const(runs)) arr.append(r);
      respond(sock, 200, json({{QStringLiteral("runs"), arr}}));
    } else if (req.method == "POST") {
      lastRunBody = body;
      QStringList users{runsMe};
      for (const QJsonValue& v : body.value(QStringLiteral("invite_user_ids")).toArray()) users << v.toString();
      QJsonArray plugins;
      for (const QJsonValue& v : body.value(QStringLiteral("plugins")).toArray()) {
        const QString pid = v.toObject().value(QStringLiteral("plugin_id")).toString();
        const QString ver = v.toObject().value(QStringLiteral("version")).toString();
        QString sha(64, QLatin1Char('0'));
        for (const QJsonValue& pv : std::as_const(pluginList))
          if (pv.toObject().value(QStringLiteral("plugin_id")).toString() == pid && pv.toObject().value(QStringLiteral("version")).toString() == ver)
            sha = pv.toObject().value(QStringLiteral("sha256")).toString();
        plugins.append(QJsonObject{{QStringLiteral("plugin_id"), pid}, {QStringLiteral("version"), ver}, {QStringLiteral("sha256"), sha}});
      }
      QJsonObject run = makeRun(body.value(QStringLiteral("name")).toString(), users, runsMe, plugins);
      run.insert(QStringLiteral("settings"), body.value(QStringLiteral("settings")).toObject());
      QJsonArray ps = run.value(QStringLiteral("participants")).toArray();
      QJsonObject me = ps.at(0).toObject();
      me.insert(QStringLiteral("game_id"), body.value(QStringLiteral("game_id")).toString());
      ps.replace(0, me);
      run.insert(QStringLiteral("participants"), ps);
      const QString id = run.value(QStringLiteral("run_id")).toString();
      runs.insert(id, run);
      pushRun(id);
      respond(sock, 201, json(runs.value(id)));
    } else {
      respondError(sock, 405, QStringLiteral("method_not_allowed"));
    }
    return;
  }
  const QString runId = parts.at(0);
  if (!runs.contains(runId)) {
    respondError(sock, 404, QStringLiteral("not_found"));
    return;
  }
  QJsonObject run = runs.value(runId);
  const QString sub = parts.value(1);
  const auto myIndex = [&]() {
    const QJsonArray ps = run.value(QStringLiteral("participants")).toArray();
    for (int i = 0; i < ps.size(); ++i)
      if (ps.at(i).toObject().value(QStringLiteral("user_id")).toString() == runsMe) return i;
    return -1;
  };
  const auto updateMe = [&](const std::function<void(QJsonObject&)>& f) {
    QJsonArray ps = run.value(QStringLiteral("participants")).toArray();
    const int i = myIndex();
    QJsonObject me = ps.at(i).toObject();
    f(me);
    ps.replace(i, me);
    run.insert(QStringLiteral("participants"), ps);
  };
  const QString status = run.value(QStringLiteral("status")).toString();
  const bool writable = status == QLatin1String("starting") || status == QLatin1String("active");
  if (sub.isEmpty() && req.method == "GET") {
    respond(sock, 200, json(run));
  } else if (sub == QLatin1String("join") && req.method == "POST") {
    lastRunBody = body;
    if (myIndex() < 0) {
      respondError(sock, 404, QStringLiteral("not_found"));
      return;
    }
    int color = 0;
    for (const QJsonValue& v : run.value(QStringLiteral("participants")).toArray())
      if (v.toObject().value(QStringLiteral("status")).toString() == QLatin1String("joined")) ++color;
    updateMe([&](QJsonObject& me) {
      me.insert(QStringLiteral("status"), QStringLiteral("joined"));
      me.insert(QStringLiteral("game_id"), body.value(QStringLiteral("game_id")).toString());
      me.insert(QStringLiteral("color"), color);
    });
    runs.insert(runId, run);
    addRunEvent(runId, QStringLiteral("run.joined"), runsMeName);
    pushRun(runId);
    respond(sock, 200, json(runs.value(runId)));
  } else if ((sub == QLatin1String("decline") || sub == QLatin1String("leave")) && req.method == "POST") {
    updateMe([&](QJsonObject& me) { me.insert(QStringLiteral("status"), sub == QLatin1String("decline") ? QStringLiteral("declined") : QStringLiteral("left")); });
    runs.insert(runId, run);
    addRunEvent(runId, sub == QLatin1String("decline") ? QStringLiteral("run.declined") : QStringLiteral("run.left"), runsMeName);
    pushRun(runId);
    respond(sock, 200, json(runs.value(runId)));
  } else if (sub == QLatin1String("invite") && req.method == "POST") {
    lastRunBody = body;
    QJsonArray ps = run.value(QStringLiteral("participants")).toArray();
    for (const QJsonValue& v : body.value(QStringLiteral("user_ids")).toArray())
      ps.append(QJsonObject{{QStringLiteral("user_id"), v.toString()}, {QStringLiteral("user_name"), v.toString()}, {QStringLiteral("status"), QStringLiteral("invited")},
                            {QStringLiteral("game_id"), QJsonValue(QJsonValue::Null)}, {QStringLiteral("color"), QJsonValue(QJsonValue::Null)},
                            {QStringLiteral("states"), QJsonObject()}});
    run.insert(QStringLiteral("participants"), ps);
    runs.insert(runId, run);
    pushRun(runId);
    respond(sock, 200, json(run));
  } else if (sub == QLatin1String("status") && req.method == "POST") {
    lastRunBody = body;
    const QString to = body.value(QStringLiteral("status")).toString();
    const bool ok = (to == QLatin1String("active") && (status == QLatin1String("starting") || status == QLatin1String("paused"))) ||
                    (to == QLatin1String("paused") && status == QLatin1String("active")) ||
                    ((to == QLatin1String("finished") || to == QLatin1String("abandoned")) &&
                     (status == QLatin1String("starting") || status == QLatin1String("active") || status == QLatin1String("paused")));
    if (!ok) {
      respondError(sock, 409, QStringLiteral("conflict"));
      return;
    }
    run.insert(QStringLiteral("status"), to);
    run.insert(QStringLiteral("status_by"), runsMe);
    runs.insert(runId, run);
    addRunEvent(runId, QStringLiteral("run.status"), to);
    pushRun(runId);
    respond(sock, 200, json(runs.value(runId)));
  } else if (sub == QLatin1String("state") && req.method == "PUT" && parts.size() == 3) {
    const QString pluginId = parts.value(2);
    lastRunBody = body;
    QJsonObject logged = body;
    logged.insert(QStringLiteral("plugin_id"), pluginId);
    runStatePuts.append(logged);
    if (!writable) {
      respondError(sock, 409, QStringLiteral("run_not_active"));
      return;
    }
    const auto stateOf = [&](const QJsonObject& me) { return me.value(QStringLiteral("states")).toObject().value(pluginId).toObject(); };
    if (runStateConflicts > 0) {
      --runStateConflicts;
      const QJsonObject me = run.value(QStringLiteral("participants")).toArray().at(myIndex()).toObject();
      respond(sock, 409, json({{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("state_conflict")}, {QStringLiteral("message"), QStringLiteral("base_rev")}}},
                               {QStringLiteral("state_rev"), static_cast<double>(runConflictRev)},
                               {QStringLiteral("state"), stateOf(me).value(QStringLiteral("state")).toObject()}}));
      return;
    }
    qint64 rev = 0;
    updateMe([&](QJsonObject& me) {
      QJsonObject all = me.value(QStringLiteral("states")).toObject();
      const QJsonObject cur = all.value(pluginId).toObject();
      rev = (std::max)(static_cast<qint64>(cur.value(QStringLiteral("state_rev")).toDouble()), static_cast<qint64>(body.value(QStringLiteral("base_rev")).toDouble())) + 1;
      all.insert(pluginId, QJsonObject{{QStringLiteral("state"), body.value(QStringLiteral("state")).toObject()},
                                       {QStringLiteral("state_rev"), static_cast<double>(rev)},
                                       {QStringLiteral("updated_at"), QStringLiteral("2026-10-12T10:00:00Z")}});
      me.insert(QStringLiteral("states"), all);
    });
    runs.insert(runId, run);
    respond(sock, 200, json({{QStringLiteral("state_rev"), static_cast<double>(rev)}}));
  } else if (sub == QLatin1String("events") && req.method == "POST") {
    runEventPosts.append(body);
    if (!writable) {
      respondError(sock, 409, QStringLiteral("run_not_active"));
      return;
    }
    const QString clientId = body.value(QStringLiteral("client_id")).toString();
    for (const QJsonObject& e : runEvents.value(runId)) {
      if (e.value(QStringLiteral("client_id")).toString() == clientId) {
        respond(sock, 200, json({{QStringLiteral("seq"), e.value(QStringLiteral("seq"))}}));
        return;
      }
    }
    const qint64 seq = addRunEvent(runId, body.value(QStringLiteral("type")).toString(), body.value(QStringLiteral("text")).toString(), runsMe,
                                   body.value(QStringLiteral("plugin_id")).toString(), clientId);
    QJsonObject& stored = runEvents[runId].last();
    stored.insert(QStringLiteral("source"), body.value(QStringLiteral("source")).toString());
    stored.insert(QStringLiteral("payload"), body.value(QStringLiteral("payload")).toObject());
    if (runEventDropResponses > 0) {
      --runEventDropResponses;
      sock->abort();
      return;
    }
    respond(sock, 201, json({{QStringLiteral("seq"), static_cast<double>(seq)}}));
  } else if (sub == QLatin1String("events") && req.method == "GET") {
    const qint64 after = query.queryItemValue(QStringLiteral("after")).toLongLong();
    const int limit = query.hasQueryItem(QStringLiteral("limit")) ? query.queryItemValue(QStringLiteral("limit")).toInt() : 200;
    QJsonArray arr;
    for (const QJsonObject& e : runEvents.value(runId)) {
      if (static_cast<qint64>(e.value(QStringLiteral("seq")).toDouble()) <= after) continue;
      if (arr.size() >= limit) break;
      arr.append(e);
    }
    respond(sock, 200, json({{QStringLiteral("events"), arr}, {QStringLiteral("last_seq"), run.value(QStringLiteral("last_seq"))}}));
  } else if (sub == QLatin1String("boards") && req.method == "PUT" && parts.size() == 3) {
    ++runBoardPuts;
    if (!writable) {
      respondError(sock, 409, QStringLiteral("run_not_active"));
      return;
    }
    runBoards.insert(runId + QLatin1Char('/') + parts.at(2), body.value(QStringLiteral("tree")).toObject());
    respond(sock, 200, json({{QStringLiteral("rev"), runBoardPuts}}));
  } else {
    respondError(sock, 404, QStringLiteral("not_found"));
  }
}
